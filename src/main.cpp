#include <Arduino.h>

#include "BoardConfig.h"
#include "DriverConfig.h"
#include "Tmc2209Driver.h"

namespace {

enum class MotionAbortReason : uint8_t {
  kNone = 0,
  kTimeout,
  kManualStop,
  kEndstopTriggered,
};

enum class MotionMode : uint8_t {
  kNormal = 0,
  kHomingSeekInitial,
  kHomingClearance,
  kHomingSeekVerify,
  kHomingRetract,
};

struct HomingCycleState {
  bool active = false;
  unsigned long plannedRetractSteps = 0;
  unsigned long expectedSecondPassSteps = 0;
  int32_t expectedSecondPassMilliMm = 0;
  unsigned long actualSecondPassSteps = 0;
  int32_t actualSecondPassMilliMm = 0;
  bool verificationReady = false;
};

struct MotionState {
  bool active = false;
  bool stepPinHigh = false;
  bool logicalForward = true;
  bool physicalDir = true;
  bool checkedFirstStep = false;
  bool bounded = true;
  unsigned long totalSteps = 0;
  unsigned long completedSteps = 0;
  unsigned long startMs = 0;
  unsigned long nextStatusMs = 0;
  unsigned long nextEdgeUs = 0;
  uint16_t mscntBefore = 0;
  uint32_t stepDelayUs = 0;
  unsigned long plannedRetractSteps = 0;
  MotionMode mode = MotionMode::kNormal;
};

struct MoveContext {
  bool reportApertureOnCompletion = false;
  int32_t requestedApertureMilliMm = 0;
};

Tmc2209Driver tmc;
MotionState motion;
HomingCycleState homingCycle;
MoveContext moveContext;

bool autoDisableAfterMove = driver_config::kAutoDisableAfterMove;
bool debugMode = driver_config::kDebugMode;
bool endstopEnabled = driver_config::kEndstopEnabled;
bool motionReady = true;
bool tmcOk = false;
bool positionKnown = false;

uint16_t runCurrentMa = driver_config::kDefaultCurrentMa;
uint16_t currentMicrosteps = driver_config::kDefaultMicrosteps;
uint32_t stepDelayUs = driver_config::effectiveNormalStepDelayUsFor(currentMicrosteps);
MotionAbortReason lastMotionAbort = MotionAbortReason::kNone;
int32_t currentPositionMilliMm = driver_config::minimumPositionMilliMm();

bool hasReachedMillis(unsigned long now, unsigned long target) {
  return static_cast<long>(now - target) >= 0;
}

bool hasReachedMicros(unsigned long now, unsigned long target) {
  return static_cast<long>(now - target) >= 0;
}

bool isEndstopTriggered() {
  const int level = digitalRead(board::kEndstopPin);
  return board::kEndstopActiveHigh ? level == HIGH : level == LOW;
}

bool motionDrivesIntoMinimumEndstop(MotionMode mode, bool logicalForward) {
  switch (mode) {
    case MotionMode::kNormal:
    case MotionMode::kHomingSeekInitial:
    case MotionMode::kHomingSeekVerify:
      return !logicalForward;
    case MotionMode::kHomingClearance:
    case MotionMode::kHomingRetract:
      return false;
  }

  return false;
}

void enableDriver() {
  digitalWrite(board::kEnablePin, board::kEnableActiveLow ? LOW : HIGH);
  delay(5);
}

void disableDriver() {
  digitalWrite(board::kEnablePin, board::kEnableActiveLow ? HIGH : LOW);
  delay(5);
}

void printDivider() { Serial.println(F("----------------------------------------")); }

void resetMotionState() { motion = MotionState{}; }

void resetMoveContext() { moveContext = MoveContext{}; }

bool shouldPrintDebug() { return debugMode; }

bool shouldHonorEndstop(MotionMode mode) {
  return mode == MotionMode::kHomingSeekInitial ||
         mode == MotionMode::kHomingSeekVerify || endstopEnabled;
}

long effectiveDefaultMoveSteps() {
  return driver_config::kDefaultMoveSteps *
         static_cast<long>(currentMicrosteps);
}

int32_t clampPositionMilliMm(int32_t positionMilliMm) {
  if (positionMilliMm < driver_config::minimumPositionMilliMm()) {
    return driver_config::minimumPositionMilliMm();
  }

  if (positionMilliMm > driver_config::maximumPositionMilliMm()) {
    return driver_config::maximumPositionMilliMm();
  }

  return positionMilliMm;
}

int32_t clampApertureMilliMm(int32_t apertureMilliMm) {
  if (apertureMilliMm < driver_config::apertureIrisMinimumMilliMm()) {
    return driver_config::apertureIrisMinimumMilliMm();
  }

  if (apertureMilliMm > driver_config::apertureIrisMaximumMilliMm()) {
    return driver_config::apertureIrisMaximumMilliMm();
  }

  return apertureMilliMm;
}

unsigned long milliMmToSteps(int32_t milliMm, uint16_t microsteps) {
  const uint32_t activeStepsPerMmX1000 =
      driver_config::activeStepsPerMmX1000(microsteps);
  if (activeStepsPerMmX1000 == 0) {
    return 0;
  }

  const uint32_t absoluteMilliMm =
      static_cast<uint32_t>(milliMm >= 0 ? milliMm : -milliMm);
  const uint64_t roundedSteps =
      (static_cast<uint64_t>(absoluteMilliMm) * activeStepsPerMmX1000 + 500000ULL) /
      1000000ULL;
  return static_cast<unsigned long>(roundedSteps);
}

int32_t stepsToMilliMm(unsigned long steps, uint16_t microsteps) {
  const uint32_t activeStepsPerMmX1000 =
      driver_config::activeStepsPerMmX1000(microsteps);
  if (activeStepsPerMmX1000 == 0) {
    return 0;
  }

  const uint64_t roundedMilliMm =
      (static_cast<uint64_t>(steps) * 1000000ULL + (activeStepsPerMmX1000 / 2ULL)) /
      activeStepsPerMmX1000;
  return static_cast<int32_t>(roundedMilliMm);
}

void printMilliMm(int32_t milliMm) {
  const bool negative = milliMm < 0;
  const int32_t absoluteValue = negative ? -milliMm : milliMm;

  if (negative) {
    Serial.print(F("-"));
  }

  Serial.print(absoluteValue / 1000L);
  Serial.print(F("."));
  const int32_t fraction = absoluteValue % 1000L;
  if (fraction < 100) {
    Serial.print(F("0"));
  }
  if (fraction < 10) {
    Serial.print(F("0"));
  }
  Serial.print(fraction);
}

int32_t travelPositionToApertureMilliMm(int32_t travelPositionMilliMm) {
  const uint32_t motionStrokeMilliMm = driver_config::strokeMilliMm();
  if (motionStrokeMilliMm == 0) {
    return driver_config::apertureIrisMinimumMilliMm();
  }

  const int32_t clampedTravelMilliMm = clampPositionMilliMm(travelPositionMilliMm);
  const uint32_t normalizedTravelMilliMm = static_cast<uint32_t>(
      clampedTravelMilliMm - driver_config::minimumPositionMilliMm());
  const uint32_t apertureStrokeMilliMm = driver_config::apertureIrisStrokeMilliMm();
  const uint32_t apertureOffsetMilliMm =
      (normalizedTravelMilliMm * apertureStrokeMilliMm + (motionStrokeMilliMm / 2UL)) /
      motionStrokeMilliMm;
  return clampApertureMilliMm(driver_config::apertureIrisMinimumMilliMm() +
                              static_cast<int32_t>(apertureOffsetMilliMm));
}

int32_t apertureOpeningToTravelPositionMilliMm(int32_t apertureMilliMm) {
  const uint32_t apertureStrokeMilliMm = driver_config::apertureIrisStrokeMilliMm();
  if (apertureStrokeMilliMm == 0) {
    return driver_config::minimumPositionMilliMm();
  }

  const int32_t clampedApertureMilliMm = clampApertureMilliMm(apertureMilliMm);
  const uint32_t normalizedApertureMilliMm = static_cast<uint32_t>(
      clampedApertureMilliMm - driver_config::apertureIrisMinimumMilliMm());
  const uint32_t motionStrokeMilliMm = driver_config::strokeMilliMm();
  const uint32_t travelOffsetMilliMm =
      (normalizedApertureMilliMm * motionStrokeMilliMm + (apertureStrokeMilliMm / 2UL)) /
      apertureStrokeMilliMm;
  return clampPositionMilliMm(driver_config::minimumPositionMilliMm() +
                              static_cast<int32_t>(travelOffsetMilliMm));
}

bool parseMilliMm(const String& text, int32_t* milliMmOut) {
  if (milliMmOut == nullptr) {
    return false;
  }

  if (text.length() == 0) {
    return false;
  }

  bool negative = false;
  size_t index = 0;
  if (text.charAt(0) == '-') {
    negative = true;
    index = 1;
  } else if (text.charAt(0) == '+') {
    index = 1;
  }

  if (index >= static_cast<size_t>(text.length())) {
    return false;
  }

  long whole = 0;
  bool hasWholeDigits = false;
  while (index < static_cast<size_t>(text.length())) {
    const char c = text.charAt(index);
    if (c < '0' || c > '9') {
      break;
    }
    hasWholeDigits = true;
    whole = whole * 10L + (c - '0');
    ++index;
  }

  int32_t fraction = 0;
  if (index < static_cast<size_t>(text.length()) && text.charAt(index) == '.') {
    ++index;
    int fractionDigits = 0;
    while (index < static_cast<size_t>(text.length())) {
      const char c = text.charAt(index);
      if (c < '0' || c > '9' || fractionDigits >= 3) {
        break;
      }
      fraction = fraction * 10 + (c - '0');
      ++fractionDigits;
      ++index;
    }

    if (fractionDigits == 1) {
      fraction *= 100;
    } else if (fractionDigits == 2) {
      fraction *= 10;
    }

    while (index < static_cast<size_t>(text.length()) &&
           text.charAt(index) >= '0' && text.charAt(index) <= '9') {
      return false;
    }
  }

  if (!hasWholeDigits || index != static_cast<size_t>(text.length())) {
    return false;
  }

  int32_t milliMm = static_cast<int32_t>(whole * 1000L + fraction);
  if (negative) {
    milliMm = -milliMm;
  }

  *milliMmOut = milliMm;
  return true;
}

void updateTrackedPositionFromSteps(bool logicalForward, unsigned long stepsTaken) {
  if (!positionKnown || stepsTaken == 0) {
    return;
  }

  const int32_t deltaMilliMm = stepsToMilliMm(stepsTaken, currentMicrosteps);
  currentPositionMilliMm += logicalForward ? deltaMilliMm : -deltaMilliMm;
  currentPositionMilliMm = clampPositionMilliMm(currentPositionMilliMm);
}

void setKnownPositionToMinimum() {
  positionKnown = true;
  currentPositionMilliMm = driver_config::minimumPositionMilliMm();
}

void resetHomingCycle(unsigned long retractSteps) {
  homingCycle = HomingCycleState{};
  homingCycle.active = true;
  homingCycle.plannedRetractSteps = retractSteps;
  homingCycle.expectedSecondPassMilliMm =
      static_cast<int32_t>(driver_config::kHomingDoubleTapDistanceMm) * 1000L;
  homingCycle.expectedSecondPassSteps =
      milliMmToSteps(homingCycle.expectedSecondPassMilliMm, currentMicrosteps);
}

void printHomingVerificationSummary() {
  if (!homingCycle.verificationReady || !shouldPrintDebug()) {
    return;
  }

  const long stepDiff = static_cast<long>(homingCycle.actualSecondPassSteps) -
                        static_cast<long>(homingCycle.expectedSecondPassSteps);
  const int32_t milliMmDiff =
      homingCycle.actualSecondPassMilliMm - homingCycle.expectedSecondPassMilliMm;

  Serial.println(F("Homing verification:"));
  Serial.print(F("  expected re-home : "));
  Serial.print(homingCycle.expectedSecondPassSteps);
  Serial.print(F(" steps / "));
  printMilliMm(homingCycle.expectedSecondPassMilliMm);
  Serial.println(F(" mm"));

  Serial.print(F("  actual re-home   : "));
  Serial.print(homingCycle.actualSecondPassSteps);
  Serial.print(F(" steps / "));
  printMilliMm(homingCycle.actualSecondPassMilliMm);
  Serial.println(F(" mm"));

  Serial.print(F("  difference       : "));
  Serial.print(stepDiff);
  Serial.print(F(" steps / "));
  printMilliMm(milliMmDiff);
  Serial.println(F(" mm"));
}

void printApertureMoveResult() {
  if (!moveContext.reportApertureOnCompletion || !positionKnown) {
    return;
  }

  Serial.print(F("Aperture requested : "));
  printMilliMm(moveContext.requestedApertureMilliMm);
  Serial.println(F(" mm"));

  Serial.print(F("Aperture result    : "));
  printMilliMm(travelPositionToApertureMilliMm(currentPositionMilliMm));
  Serial.println(F(" mm"));
}

const __FlashStringHelper* motionAbortReasonText(MotionAbortReason reason) {
  switch (reason) {
    case MotionAbortReason::kNone:
      return F("none");
    case MotionAbortReason::kTimeout:
      return F("timeout");
    case MotionAbortReason::kManualStop:
      return F("manual stop");
    case MotionAbortReason::kEndstopTriggered:
      return F("endstop");
  }

  return F("unknown");
}

const __FlashStringHelper* microstepStatusText(
    Tmc2209Driver::MicrostepStatus status) {
  switch (status) {
    case Tmc2209Driver::MicrostepStatus::kOk:
      return F("OK");
    case Tmc2209Driver::MicrostepStatus::kInvalidMicrostepValue:
      return F("Invalid microstep value.");
    case Tmc2209Driver::MicrostepStatus::kUnavailable:
      return F("TMC UART unavailable.");
    case Tmc2209Driver::MicrostepStatus::kWriteFailed:
      return F("TMC write verification failed.");
  }

  return F("Unknown error.");
}

void printMicrostepRaw() {
  if (!tmcOk) {
    Serial.println(F("  UART status      : unavailable"));
    return;
  }

  const uint32_t chop = tmc.chopconf();
  const uint8_t mres = (chop >> 24) & 0x0F;
  const bool intpol = ((chop >> 28) & 0x01U) != 0;

  Serial.print(F("  CHOPCONF raw     : 0x"));
  Serial.println(chop, HEX);

  Serial.print(F("  MRES raw         : "));
  Serial.println(mres);

  Serial.print(F("  real microsteps  : "));
  Serial.println(tmc.getRealMicrosteps());

  Serial.print(F("  INTPOL           : "));
  Serial.println(intpol ? F("1") : F("0"));

  Serial.print(F("  library read     : "));
  Serial.println(tmc.libraryMicrosteps());
}

bool setMicrosteps(uint16_t microsteps) {
  const Tmc2209Driver::MicrostepStatus status = tmc.setMicrosteps(microsteps);
  if (status == Tmc2209Driver::MicrostepStatus::kInvalidMicrostepValue) {
    Serial.println(F("ERROR: Invalid microstep value."));
    Serial.println(F("Allowed: 1, 2, 4, 8, 16, 32, 64, 128, 256"));
    return false;
  }

  currentMicrosteps = microsteps;
  stepDelayUs = driver_config::effectiveNormalStepDelayUsFor(currentMicrosteps);

  if (status == Tmc2209Driver::MicrostepStatus::kOk) {
    Serial.print(F("Microsteps set to: "));
    Serial.println(currentMicrosteps);
    Serial.print(F("Step delay auto-set to: "));
    Serial.print(stepDelayUs);
    Serial.print(F(" us, homing delay: "));
    Serial.print(driver_config::homingStepDelayUsFor(currentMicrosteps));
    Serial.println(F(" us."));
    tmcOk = tmc.isConnected();
    return true;
  }

  Serial.print(F("WARNING: "));
  Serial.println(microstepStatusText(status));
  Serial.println(F("Microstep setting saved locally but not applied to the driver."));
  Serial.print(F("Step delay auto-set to: "));
  Serial.print(stepDelayUs);
  Serial.print(F(" us, homing delay: "));
  Serial.print(driver_config::homingStepDelayUsFor(currentMicrosteps));
  Serial.println(F(" us."));
  tmcOk = tmc.isConnected();
  return false;
}

void printMotionProgress() {
  Serial.print(F("Motion progress: "));
  Serial.print(motion.completedSteps);
  if (motion.bounded) {
    Serial.print(F("/"));
    Serial.print(motion.totalSteps);
  }
  Serial.println(F(" steps"));
}

void finishMove(bool aborted, MotionAbortReason reason) {
  if (!motion.active && !motion.stepPinHigh) {
    lastMotionAbort = aborted ? reason : MotionAbortReason::kNone;
    return;
  }

  const MotionMode finishedMode = motion.mode;
  const bool finishedLogicalForward = motion.logicalForward;
  const unsigned long finishedCompletedSteps = motion.completedSteps;
  digitalWrite(board::kStepPin, LOW);

  if (aborted) {
    lastMotionAbort = reason;
    Serial.print(F("ERROR: Move aborted ("));
    Serial.print(motionAbortReasonText(reason));
    Serial.println(F(")."));
  } else {
    lastMotionAbort = MotionAbortReason::kNone;
    if (shouldPrintDebug()) {
      Serial.println(finishedMode == MotionMode::kNormal ? F("Move done.")
                                                         : F("Homing complete."));
    }
  }

  resetMotionState();

  if (finishedMode == MotionMode::kNormal || finishedMode == MotionMode::kHomingClearance ||
      finishedMode == MotionMode::kHomingRetract) {
    updateTrackedPositionFromSteps(finishedLogicalForward, finishedCompletedSteps);
  }

  if ((finishedMode == MotionMode::kNormal ||
       finishedMode == MotionMode::kHomingSeekInitial ||
       finishedMode == MotionMode::kHomingSeekVerify) &&
      aborted && reason == MotionAbortReason::kEndstopTriggered &&
      !finishedLogicalForward) {
    setKnownPositionToMinimum();
  }

  if (!aborted && finishedMode == MotionMode::kHomingRetract) {
    printHomingVerificationSummary();
  }

  if (!aborted && finishedMode == MotionMode::kNormal) {
    printApertureMoveResult();
  }

  if (finishedMode != MotionMode::kNormal &&
      (aborted || finishedMode == MotionMode::kHomingRetract)) {
    homingCycle.active = false;
  }

  if (finishedMode == MotionMode::kNormal) {
    resetMoveContext();
  }

  const bool shouldDisable =
      finishedMode == MotionMode::kNormal ? (autoDisableAfterMove || aborted)
                                          : true;
  if (shouldDisable) {
    if (!aborted && finishedMode == MotionMode::kNormal) {
      delay(100);
    }

    disableDriver();
    if (shouldPrintDebug()) {
      Serial.println(finishedMode == MotionMode::kNormal
                         ? F("Driver disabled after move.")
                         : F("Driver disabled after homing."));
    }
  }

  if (!aborted && finishedMode == MotionMode::kHomingRetract &&
      isEndstopTriggered()) {
    Serial.println(F("WARNING: Endstop still active after retract."));
  }

  if (shouldPrintDebug()) {
    Serial.println();
  }
}

bool beginMotion(bool logicalForward, unsigned long totalSteps, bool bounded,
                 uint32_t requestedStepDelayUs, MotionMode mode,
                 bool allowTriggeredStart) {
  if (!motionReady) {
    Serial.println(F("Refusing move: motion baseline not ready."));
    return false;
  }

  if (motion.active) {
    Serial.println(F("Refusing move: another move is already in progress."));
    return false;
  }

  if (!allowTriggeredStart && shouldHonorEndstop(mode) && isEndstopTriggered() &&
      motionDrivesIntoMinimumEndstop(mode, logicalForward)) {
    lastMotionAbort = MotionAbortReason::kEndstopTriggered;
    Serial.println(F("Refusing move: endstop input is active for this direction."));
    return false;
  }

  motion = MotionState{};
  motion.active = true;
  motion.logicalForward = logicalForward;
  motion.physicalDir =
      board::kInvertDirection ? !motion.logicalForward : motion.logicalForward;
  motion.bounded = bounded;
  motion.totalSteps = totalSteps;
  motion.startMs = millis();
  motion.nextStatusMs =
      motion.startMs + driver_config::kMotionStatusIntervalMs;
  motion.stepDelayUs = requestedStepDelayUs;
  motion.mode = mode;

  digitalWrite(board::kDirPin, motion.physicalDir ? HIGH : LOW);
  enableDriver();

  motion.nextEdgeUs = micros() + driver_config::kDirectionSetupDelayUs;
  lastMotionAbort = MotionAbortReason::kNone;
  return true;
}

void startMove(long steps) {
  if (steps == 0) {
    Serial.println(F("Move ignored: 0 steps."));
    return;
  }

  const bool logicalForward = steps > 0;
  const unsigned long stepCount = static_cast<unsigned long>(labs(steps));
  if (!beginMotion(logicalForward, stepCount, true, stepDelayUs,
                   MotionMode::kNormal, false)) {
    return;
  }

  if (shouldPrintDebug()) {
    Serial.println();
    Serial.print(F("Move "));
    Serial.print(motion.logicalForward ? F("forward ") : F("backward "));
    Serial.print(motion.totalSteps);
    Serial.println(F(" steps"));
  }

  if (tmcOk) {
    motion.mscntBefore = tmc.microstepCounter();
    if (shouldPrintDebug()) {
      Serial.print(F("MSCNT before: "));
      Serial.println(motion.mscntBefore);
    }
  } else {
    if (shouldPrintDebug()) {
      Serial.println(F("UART offline: running STEP/DIR only."));
    }
  }
}

void startAbsolutePositionMove(int32_t targetPositionMilliMm) {
  if (!positionKnown) {
    Serial.println(F("Refusing absolute move: position unknown. Home first."));
    return;
  }

  if (targetPositionMilliMm < driver_config::minimumPositionMilliMm() ||
      targetPositionMilliMm > driver_config::maximumPositionMilliMm()) {
    Serial.println(F("Refusing absolute move: target is outside configured range."));
    return;
  }

  const int32_t deltaMilliMm = targetPositionMilliMm - currentPositionMilliMm;
  const unsigned long steps = milliMmToSteps(deltaMilliMm, currentMicrosteps);
  if (steps == 0) {
    Serial.println(F("Absolute move ignored: already at target."));
    return;
  }

  startMove(deltaMilliMm > 0 ? static_cast<long>(steps) : -static_cast<long>(steps));
}

void startApertureOpeningMove(int32_t targetApertureMilliMm) {
  resetMoveContext();

  if (!positionKnown) {
    Serial.println(F("Refusing aperture move: position unknown. Home first."));
    return;
  }

  if (targetApertureMilliMm < driver_config::apertureIrisMinimumMilliMm() ||
      targetApertureMilliMm > driver_config::apertureIrisMaximumMilliMm()) {
    Serial.print(F("Refusing aperture move: target is outside configured aperture range "));
    printMilliMm(driver_config::apertureIrisMinimumMilliMm());
    Serial.print(F(".."));
    printMilliMm(driver_config::apertureIrisMaximumMilliMm());
    Serial.println(F(" mm."));
    return;
  }

  moveContext.reportApertureOnCompletion = true;
  moveContext.requestedApertureMilliMm = targetApertureMilliMm;
  startAbsolutePositionMove(apertureOpeningToTravelPositionMilliMm(targetApertureMilliMm));

  if (!motion.active) {
    resetMoveContext();
  }
}

bool startSecondHomingSeek();

void finishHomingWithoutRetract() {
  setKnownPositionToMinimum();
  if (shouldPrintDebug()) {
    Serial.println(F("Endstop triggered."));
  }
  printHomingVerificationSummary();
  if (shouldPrintDebug()) {
    Serial.println(F("No homing retract requested."));
  }
  disableDriver();
  if (shouldPrintDebug()) {
    Serial.println(F("Homing complete."));
    Serial.println(F("Driver disabled after homing."));
  }
  lastMotionAbort = MotionAbortReason::kNone;
  homingCycle.active = false;
  if (shouldPrintDebug()) {
    Serial.println();
  }
}

void startHomingRetract(unsigned long retractSteps) {
  if (retractSteps == 0) {
    finishHomingWithoutRetract();
    return;
  }

  const bool retractForward = driver_config::kHomingDirectionNegative;
  if (!beginMotion(retractForward, retractSteps, true,
                   driver_config::homingStepDelayUsFor(currentMicrosteps),
                   MotionMode::kHomingRetract, true)) {
    homingCycle.active = false;
    return;
  }

  if (shouldPrintDebug()) {
    Serial.print(F("Endstop triggered. Retracting "));
    Serial.print(retractSteps);
    Serial.println(F(" steps away from endstop."));
  }
}

bool startDoubleTapAdvance() {
  if (homingCycle.expectedSecondPassSteps == 0) {
    Serial.println(F("ERROR: double-tap distance resolved to 0 steps."));
    homingCycle.active = false;
    return false;
  }

  const bool advanceForward = driver_config::kHomingDirectionNegative;
  if (!beginMotion(advanceForward, homingCycle.expectedSecondPassSteps, true,
                   driver_config::homingStepDelayUsFor(currentMicrosteps),
                   MotionMode::kHomingClearance, true)) {
    homingCycle.active = false;
    return false;
  }

  if (shouldPrintDebug()) {
    Serial.print(F("Endstop triggered. Advancing "));
    printMilliMm(homingCycle.expectedSecondPassMilliMm);
    Serial.print(F(" mm ("));
    Serial.print(homingCycle.expectedSecondPassSteps);
    Serial.println(F(" steps) for double-tap verification."));
  }
  return true;
}

bool startSecondHomingSeek() {
  const bool homingForward = !driver_config::kHomingDirectionNegative;
  if (isEndstopTriggered()) {
    Serial.println(F("ERROR: Endstop still active before second homing seek."));
    homingCycle.active = false;
    return false;
  }

  if (!beginMotion(homingForward, 0, false,
                   driver_config::secondSeekHomingStepDelayUsFor(currentMicrosteps),
                   MotionMode::kHomingSeekVerify, false)) {
    homingCycle.active = false;
    return false;
  }

  if (shouldPrintDebug()) {
    Serial.print(F("Second homing seek at "));
    Serial.print(driver_config::secondSeekHomingStepDelayUsFor(currentMicrosteps));
    Serial.println(F(" us per edge."));
  }
  return true;
}

void completeHomingClearance() {
  const unsigned long clearanceSteps = motion.completedSteps;
  digitalWrite(board::kStepPin, LOW);
  updateTrackedPositionFromSteps(true, clearanceSteps);
  resetMotionState();
  startSecondHomingSeek();
}

void handleHomingTrigger() {
  const MotionMode finishedMode = motion.mode;
  const unsigned long completedSteps = motion.completedSteps;

  digitalWrite(board::kStepPin, LOW);
  setKnownPositionToMinimum();
  resetMotionState();

  if (finishedMode == MotionMode::kHomingSeekInitial) {
    startDoubleTapAdvance();
    return;
  }

  homingCycle.actualSecondPassSteps = completedSteps;
  homingCycle.actualSecondPassMilliMm =
      stepsToMilliMm(homingCycle.actualSecondPassSteps, currentMicrosteps);
  homingCycle.verificationReady = true;
  startHomingRetract(homingCycle.plannedRetractSteps);
}

void homeAperture(unsigned long retractSteps) {
  if (isEndstopTriggered()) {
    lastMotionAbort = MotionAbortReason::kEndstopTriggered;
    Serial.println(F("Refusing homing: endstop input is already active."));
    return;
  }

  resetHomingCycle(retractSteps);

  const bool homingForward = !driver_config::kHomingDirectionNegative;
  if (!beginMotion(homingForward, 0, false,
                   driver_config::homingStepDelayUsFor(currentMicrosteps),
                   MotionMode::kHomingSeekInitial, false)) {
    homingCycle.active = false;
    return;
  }

  if (shouldPrintDebug()) {
    Serial.println();
    Serial.print(F("Homing "));
    Serial.print(homingForward ? F("forward") : F("backward"));
    Serial.print(F(" at "));
    Serial.print(driver_config::homingStepDelayUsFor(currentMicrosteps));
    Serial.print(F(" us per edge. Double-tap distance: "));
    printMilliMm(homingCycle.expectedSecondPassMilliMm);
    Serial.print(F(" mm, second seek: "));
    Serial.print(driver_config::secondSeekHomingStepDelayUsFor(currentMicrosteps));
    Serial.println(F(" us per edge."));
  }
}

void serviceMotion() {
  if (!motion.active) {
    return;
  }

  if (shouldHonorEndstop(motion.mode) && isEndstopTriggered() &&
      motionDrivesIntoMinimumEndstop(motion.mode, motion.logicalForward)) {
    if (motion.mode == MotionMode::kHomingSeekInitial ||
        motion.mode == MotionMode::kHomingSeekVerify) {
      handleHomingTrigger();
    } else {
      finishMove(true, MotionAbortReason::kEndstopTriggered);
    }
    return;
  }

  const unsigned long nowMs = millis();
  if (hasReachedMillis(nowMs,
                       motion.startMs + driver_config::kMaxMoveDurationMs)) {
    finishMove(true, MotionAbortReason::kTimeout);
    return;
  }

  if (driver_config::kMotionStatusIntervalMs > 0 &&
      hasReachedMillis(nowMs, motion.nextStatusMs)) {
    printMotionProgress();
    motion.nextStatusMs += driver_config::kMotionStatusIntervalMs;
  }

  const unsigned long nowUs = micros();
  if (!hasReachedMicros(nowUs, motion.nextEdgeUs)) {
    yield();
    return;
  }

  // Cooperative edge scheduling keeps loop() responsive without moving to a
  // timer ISR yet, so later timer-based migration stays isolated.
  if (!motion.stepPinHigh) {
    digitalWrite(board::kStepPin, HIGH);
    motion.stepPinHigh = true;
    motion.nextEdgeUs = nowUs + motion.stepDelayUs;
    yield();
    return;
  }

  digitalWrite(board::kStepPin, LOW);
  motion.stepPinHigh = false;
  ++motion.completedSteps;

  if (tmcOk && !motion.checkedFirstStep) {
    const uint16_t mscntAfterOne = tmc.microstepCounter();
    if (shouldPrintDebug()) {
      Serial.print(F("MSCNT after 1 step: "));
      Serial.println(mscntAfterOne);

      if (mscntAfterOne == motion.mscntBefore) {
        Serial.println(F("WARNING: MSCNT did not change after first step."));
      } else {
        Serial.println(F("OK: TMC saw STEP pulse."));
      }
    }

    motion.checkedFirstStep = true;
  }

  if (motion.bounded && motion.completedSteps >= motion.totalSteps) {
    if (motion.mode == MotionMode::kHomingClearance) {
      completeHomingClearance();
      return;
    }
    finishMove(false, MotionAbortReason::kNone);
    return;
  }

  motion.nextEdgeUs = nowUs + motion.stepDelayUs;
  yield();
}

void printStatus() {
  Serial.println();
  Serial.println(F("Nano SuperMini aperture driver status"));
  printDivider();

  Serial.print(F("  motion ready     : "));
  Serial.println(motionReady ? F("YES") : F("NO"));

  Serial.print(F("  motion state     : "));
  Serial.println(motion.active ? F("ACTIVE") : F("IDLE"));

  Serial.print(F("  endstop pin      : D"));
  Serial.println(board::kEndstopPin);

  Serial.print(F("  endstop active   : "));
  Serial.println(board::kEndstopActiveHigh ? F("HIGH") : F("LOW"));

  Serial.print(F("  endstop state    : "));
  Serial.println(isEndstopTriggered() ? F("TRIGGERED") : F("IDLE"));

  Serial.print(F("  endstop enabled  : "));
  Serial.println(endstopEnabled ? F("ON") : F("OFF"));

  Serial.print(F("  debug mode       : "));
  Serial.println(debugMode ? F("ON") : F("OFF"));

  Serial.print(F("  position known   : "));
  Serial.println(positionKnown ? F("YES") : F("NO"));

  Serial.print(F("  position mm      : "));
  if (positionKnown) {
    printMilliMm(currentPositionMilliMm);
    Serial.println();
  } else {
    Serial.println(F("unknown"));
  }

  Serial.print(F("  min position mm  : "));
  printMilliMm(driver_config::minimumPositionMilliMm());
  Serial.println();

  Serial.print(F("  max position mm  : "));
  printMilliMm(driver_config::maximumPositionMilliMm());
  Serial.println();

  Serial.print(F("  max speed mm/s   : "));
  printMilliMm(static_cast<int32_t>(driver_config::kMaximumSpeedMmPerSec) * 1000L);
  Serial.println();

  if (motion.active) {
    Serial.print(F("  move progress    : "));
    Serial.print(motion.completedSteps);
    if (motion.bounded) {
      Serial.print(F("/"));
      Serial.println(motion.totalSteps);
    } else {
      Serial.println(F(" steps"));
    }
  }

  Serial.print(F("  last abort       : "));
  Serial.println(motionAbortReasonText(lastMotionAbort));

  Serial.print(F("  UART enabled     : "));
  Serial.println(tmc.isEnabled() ? F("YES") : F("NO"));

  Serial.print(F("  TMC connected    : "));
  Serial.println(tmcOk ? F("YES") : F("NO"));

  if (tmc.isEnabled()) {
    Serial.print(F("  test_connection  : "));
    Serial.println(tmc.testConnection());

    if (tmcOk) {
      Serial.print(F("  DRV_STATUS       : 0x"));
      Serial.println(tmc.drvStatus(), HEX);

      Serial.print(F("  rms_current      : "));
      Serial.println(tmc.rmsCurrent());

      Serial.print(F("  toff             : "));
      Serial.println(tmc.toff());

      Serial.print(F("  ot overtemp      : "));
      Serial.println(tmc.overtemp() ? F("1") : F("0"));

      Serial.print(F("  standstill       : "));
      Serial.println(tmc.standstill() ? F("1") : F("0"));

      Serial.print(F("  MSCNT            : "));
      Serial.println(tmc.microstepCounter());
    }
  }

  Serial.print(F("  configured mA    : "));
  Serial.println(runCurrentMa);

  Serial.print(F("  steps/mm cfg     : "));
  printMilliMm(static_cast<int32_t>(driver_config::kStepsPerMmX1000));
  Serial.println();

  Serial.print(F("  steps/mm drv     : "));
  printMilliMm(static_cast<int32_t>(driver_config::kDerivedStepsPerMmX1000));
  Serial.println();

  Serial.print(F("  microsteps       : "));
  Serial.println(currentMicrosteps);

  Serial.print(F("  step delay us    : "));
  Serial.println(stepDelayUs);

  Serial.print(F("  speed limit us   : "));
  Serial.println(driver_config::speedLimitedStepDelayUsFor(currentMicrosteps));

  Serial.print(F("  auto step delay  : "));
  Serial.println(driver_config::effectiveNormalStepDelayUsFor(currentMicrosteps));

  Serial.print(F("  homing delay us  : "));
  Serial.println(driver_config::homingStepDelayUsFor(currentMicrosteps));

  Serial.print(F("  est max mm/s     : "));
  printMilliMm(static_cast<int32_t>(
      driver_config::estimatedMaxSpeedMilliMmPerSecForDelay(currentMicrosteps, stepDelayUs)));
  Serial.println();

  Serial.print(F("  default move 1x  : "));
  Serial.println(driver_config::kDefaultMoveSteps);

  Serial.print(F("  default move eff : "));
  Serial.println(effectiveDefaultMoveSteps());

  Serial.print(F("  step rate approx : "));
  Serial.print(1000000.0 / (2.0 * stepDelayUs));
  Serial.println(F(" steps/sec"));

  Serial.print(F("  auto disable     : "));
  Serial.println(autoDisableAfterMove ? F("ON") : F("OFF"));

  printMicrostepRaw();
  printDivider();
  Serial.println();
}

void printHelp() {
  Serial.println();
  Serial.println(F("Commands:"));
  Serial.println(F("  h              help"));
  Serial.println(F("  s              status"));
  Serial.println(F("  e              enable driver"));
  Serial.println(F("  d              disable driver"));
  Serial.println(F("  f              move forward default distance"));
  Serial.println(F("  b              move backward default distance"));
  Serial.println(F("  f 2000         move forward 2000 steps"));
  Serial.println(F("  b 2000         move backward 2000 steps"));
  Serial.println(F("  m 1000         move signed steps"));
  Serial.println(F("  m -1000        move signed steps backward"));
  Serial.println(F("  g 12.345       go to absolute position 12.345 mm"));
  Serial.println(F("  A 8.500        go to aperture opening 8.500 mm"));
  Serial.println(F("  H              home with double-tap verification"));
  Serial.println(F("  H 50           home, verify, then retract 50 steps"));
  Serial.println(F("  E              toggle endstop protection"));
  Serial.println(F("  D              toggle debug output"));
  Serial.println(F("  i 180          set run current to 180 mA RMS"));
  Serial.println(F("  u 4            set microsteps: 1,2,4,8,16,32,64,128,256"));
  Serial.println(F("  v 2000         set step delay in microseconds (temporary override)"));
  Serial.println(F("  a              toggle auto-disable after move"));
  Serial.println();
}

void handleCommand(String line) {
  line.trim();
  if (line.length() == 0) {
    return;
  }

  const char cmd = line.charAt(0);
  String arg = "";
  if (line.length() > 1) {
    arg = line.substring(1);
    arg.trim();
  }

  const long value = arg.toInt();

  switch (cmd) {
    case 'h':
    case '?':
      printHelp();
      break;

    case 's':
      printStatus();
      break;

    case 'e':
      if (motion.active) {
        Serial.println(F("Driver already enabled for active motion."));
      } else {
        enableDriver();
        Serial.println(F("Driver enabled."));
      }
      break;

    case 'd':
      if (motion.active) {
        finishMove(true, MotionAbortReason::kManualStop);
      } else {
        disableDriver();
        Serial.println(F("Driver disabled."));
      }
      break;

    case 'f':
      resetMoveContext();
      startMove(arg.length() ? value : effectiveDefaultMoveSteps());
      break;

    case 'b':
      resetMoveContext();
      startMove(arg.length() ? -value : -effectiveDefaultMoveSteps());
      break;

    case 'm':
      resetMoveContext();
      if (arg.length() == 0) {
        Serial.println(F("Usage: m 1000 or m -1000"));
      } else {
        startMove(value);
      }
      break;

    case 'g': {
      resetMoveContext();
      int32_t targetPositionMilliMm = 0;
      if (!parseMilliMm(arg, &targetPositionMilliMm)) {
        Serial.println(F("Usage: g 12.345"));
      } else {
        startAbsolutePositionMove(targetPositionMilliMm);
      }
      break;
    }

    case 'A': {
      int32_t targetApertureMilliMm = 0;
      if (!parseMilliMm(arg, &targetApertureMilliMm)) {
        Serial.println(F("Usage: A 8.500"));
      } else {
        startApertureOpeningMove(targetApertureMilliMm);
      }
      break;
    }

    case 'H':
      if (arg.length() && value < 0) {
        Serial.println(F("ERROR: homing retract must be 0 or greater."));
      } else {
        homeAperture(arg.length() ? static_cast<unsigned long>(value)
                                  : driver_config::kHomingRetractSteps);
      }
      break;

    case 'E':
      endstopEnabled = !endstopEnabled;
      Serial.print(F("Endstop protection: "));
      Serial.println(endstopEnabled ? F("ON") : F("OFF"));
      break;

    case 'D':
      debugMode = !debugMode;
      Serial.print(F("Debug mode: "));
      Serial.println(debugMode ? F("ON") : F("OFF"));
      break;

    case 'i':
      if (value <= 0 || value > 500) {
        Serial.println(F("ERROR: current must be 1..500 mA RMS."));
        Serial.println(F("For a small motor, start around 120..220 mA."));
      } else {
        runCurrentMa = static_cast<uint16_t>(value);
        if (tmcOk) {
          tmc.setRunCurrent(runCurrentMa);
        }

        Serial.print(F("Run current set to "));
        Serial.print(runCurrentMa);
        Serial.println(F(" mA RMS."));
      }
      break;

    case 'u':
      if (arg.length() == 0) {
        Serial.println(F("Usage: u 1 / u 2 / u 4 / u 8 / u 16 ..."));
      } else {
        setMicrosteps(static_cast<uint16_t>(value));
      }
      break;

    case 'v':
      if (value < 5 || value > 100000) {
        Serial.println(F("ERROR: step delay must be 5..100000 us."));
      } else {
        stepDelayUs = static_cast<uint32_t>(value);
        Serial.print(F("Step delay set to "));
        Serial.print(stepDelayUs);
        Serial.print(F(" us, approx "));
        Serial.print(1000000.0 / (2.0 * stepDelayUs));
        Serial.println(F(" steps/sec."));
      }
      break;

    case 'a':
      autoDisableAfterMove = !autoDisableAfterMove;
      Serial.print(F("Auto-disable after move: "));
      Serial.println(autoDisableAfterMove ? F("ON") : F("OFF"));
      break;

    default:
      Serial.println(F("Unknown command. Type h for help."));
      break;
  }
}

void handleSerial() {
  if (!Serial.available()) {
    return;
  }

  const String line = Serial.readStringUntil('\n');
  handleCommand(line);
}

void initPins() {
  pinMode(board::kEnablePin, OUTPUT);
  pinMode(board::kStepPin, OUTPUT);
  pinMode(board::kDirPin, OUTPUT);
  pinMode(board::kEndstopPin, INPUT_PULLUP);

  digitalWrite(board::kStepPin, LOW);
  digitalWrite(board::kDirPin, LOW);
  disableDriver();
}

void initTmcLayer() {
  tmcOk = tmc.begin(runCurrentMa, currentMicrosteps);

  if (!tmc.isEnabled()) {
    Serial.println(F("TMC UART layer is disabled in DriverConfig.h."));
    Serial.println(F("STEP/DIR baseline is active; wire UART later when ready."));
    return;
  }

  Serial.print(F("test_connection() = "));
  Serial.println(tmc.testConnection());

  if (!tmcOk) {
    if (tmc.isConnected()) {
      Serial.print(F("ERROR: TMC2209 setup failed: "));
      Serial.println(microstepStatusText(tmc.lastMicrostepStatus()));
    } else {
      Serial.println(F("ERROR: TMC2209 not detected over UART."));
      Serial.println(
          F("Check PDN_UART wiring, resistor, ground, and driver address."));
    }

    return;
  }

  currentMicrosteps = tmc.getRealMicrosteps();
  stepDelayUs = driver_config::effectiveNormalStepDelayUsFor(currentMicrosteps);
  Serial.println(F("OK: TMC2209 detected over UART."));
}

}  // namespace

void setup() {
  Serial.begin(board::kUsbSerialBaud);
  Serial.setTimeout(50);
  delay(500);

  initPins();

  Serial.println();
  Serial.println(F("Nano SuperMini aperture driver"));
  Serial.println();

  initTmcLayer();
  //printHelp();
  //printStatus();
}

void loop() {
  handleSerial();
  serviceMotion();
}
