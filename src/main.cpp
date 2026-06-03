#include <Arduino.h>
#include <avr/wdt.h>
#include <string.h>

#include "BoardConfig.h"
#include "DriverConfig.h"
#include "PersistentConfig.h"
#include "Tmc2209Driver.h"
#include "UserCommandBindings.h"
#include "UserCommands.h"

namespace app {

struct MoveContext {
  bool reportApertureOnCompletion = false;
  int32_t requestedApertureMilliMm = 0;
};

enum class MotionReadyReason : uint8_t {
  kReady = 0,
  kUartDisabled,
  kUartSetupFailed,
  kUartProbeFailed,
};

Tmc2209Driver tmc;
MotionState motion;
HomingCycleState homingCycle;
MoveContext moveContext;
uint8_t bootResetFlags __attribute__((section(".noinit")));

void disableWatchdogAtStartup() __attribute__((naked))
    __attribute__((used)) __attribute__((section(".init3")));
void disableWatchdogAtStartup() {
  bootResetFlags = MCUSR;
  MCUSR = 0;
  wdt_disable();
}

bool autoDisableAfterMove = driver_config::kAutoDisableAfterMove;
bool debugMode = driver_config::kDebugMode;
bool driverEnabled = false;
bool endstopEnabled = driver_config::kEndstopEnabled;
bool motionReady = false;
bool tmcOk = false;
bool positionKnown = false;
bool runtimeConfigDirty = false;
bool savedRuntimeConfigValid = false;

uint16_t runCurrentMa = driver_config::kDefaultCurrentMa;
uint16_t currentMicrosteps = driver_config::kDefaultMicrosteps;
uint32_t stepDelayOverrideUs = persistent_config::kNoStepDelayOverrideUs;
uint32_t stepDelayUs = driver_config::effectiveNormalStepDelayUsFor(currentMicrosteps);
int32_t apertureIrisMinMilliMm = driver_config::kApertureIrisMinMilliMm;
int32_t apertureIrisMaxMilliMm = driver_config::kApertureIrisMaxMilliMm;
char arduinoName[persistent_config::kArduinoNameCapacity] = {};
MotionAbortReason lastMotionAbort = MotionAbortReason::kNone;
int32_t currentPositionMilliMm = driver_config::minimumPositionMilliMm();
RuntimeConfigSource runtimeConfigSource = RuntimeConfigSource::kDefaults;
CliMode cliMode = CliMode::Normal;
MotionReadyReason motionReadyReason = MotionReadyReason::kUartSetupFailed;
persistent_config::LoadStatus lastRuntimeConfigLoadStatus =
    persistent_config::LoadStatus::kEmpty;
persistent_config::RuntimeConfig savedRuntimeConfig{};

bool isPersistenceEnabled() { return driver_config::kSaveConfigToEeprom; }

bool isStepDelayOverrideActive() {
  return stepDelayOverrideUs != persistent_config::kNoStepDelayOverrideUs;
}

persistent_config::RuntimeConfig makeDefaultRuntimeConfig() {
  persistent_config::RuntimeConfig config{};
  config.endstopEnabled = driver_config::kEndstopEnabled;
  config.debugMode = driver_config::kDebugMode;
  config.autoDisableAfterMove = driver_config::kAutoDisableAfterMove;
  config.runCurrentMa = driver_config::kDefaultCurrentMa;
  config.microsteps = driver_config::kDefaultMicrosteps;
  config.stepDelayOverrideUs = persistent_config::kNoStepDelayOverrideUs;
  config.apertureIrisMinMilliMm = driver_config::kApertureIrisMinMilliMm;
  config.apertureIrisMaxMilliMm = driver_config::kApertureIrisMaxMilliMm;
  strncpy(config.arduinoName, driver_config::kArduinoName,
          sizeof(config.arduinoName) - 1);
  config.arduinoName[sizeof(config.arduinoName) - 1] = '\0';
  return config;
}

persistent_config::RuntimeConfig captureRuntimeConfig() {
  persistent_config::RuntimeConfig config{};
  config.endstopEnabled = endstopEnabled;
  config.debugMode = debugMode;
  config.autoDisableAfterMove = autoDisableAfterMove;
  config.runCurrentMa = runCurrentMa;
  config.microsteps = currentMicrosteps;
  config.stepDelayOverrideUs = stepDelayOverrideUs;
  config.apertureIrisMinMilliMm = apertureIrisMinMilliMm;
  config.apertureIrisMaxMilliMm = apertureIrisMaxMilliMm;
  memcpy(config.arduinoName, arduinoName, sizeof(config.arduinoName));
  return config;
}

uint32_t effectiveStepDelayUsForConfig(
    const persistent_config::RuntimeConfig& config) {
  return config.stepDelayOverrideUs == persistent_config::kNoStepDelayOverrideUs
             ? driver_config::effectiveNormalStepDelayUsFor(config.microsteps)
             : config.stepDelayOverrideUs;
}

void refreshStepDelayUsFromCurrentSettings() {
  stepDelayUs = isStepDelayOverrideActive()
                    ? stepDelayOverrideUs
                    : driver_config::effectiveNormalStepDelayUsFor(currentMicrosteps);
}

void applyRuntimeConfigLocally(const persistent_config::RuntimeConfig& config) {
  endstopEnabled = config.endstopEnabled;
  debugMode = config.debugMode;
  autoDisableAfterMove = config.autoDisableAfterMove;
  runCurrentMa = config.runCurrentMa;
  currentMicrosteps = config.microsteps;
  stepDelayOverrideUs = config.stepDelayOverrideUs;
  apertureIrisMinMilliMm = config.apertureIrisMinMilliMm;
  apertureIrisMaxMilliMm = config.apertureIrisMaxMilliMm;
  memcpy(arduinoName, config.arduinoName, sizeof(arduinoName));
  refreshStepDelayUsFromCurrentSettings();
}

uint32_t currentApertureIrisStrokeMilliMm() {
  return static_cast<uint32_t>(apertureIrisMaxMilliMm - apertureIrisMinMilliMm);
}

void updateRuntimeConfigDirtyFromBaseline() {
  if (!isPersistenceEnabled()) {
    runtimeConfigDirty = false;
    return;
  }

  const persistent_config::RuntimeConfig currentConfig = captureRuntimeConfig();
  if (savedRuntimeConfigValid) {
    runtimeConfigDirty = !persistent_config::runtimeConfigsEqual(
        currentConfig, savedRuntimeConfig);
    return;
  }

  runtimeConfigDirty = !persistent_config::runtimeConfigsEqual(
      currentConfig, makeDefaultRuntimeConfig());
}

bool refreshSavedRuntimeConfigFromEeprom() {
  if (!isPersistenceEnabled()) {
    savedRuntimeConfigValid = false;
    return false;
  }

  const persistent_config::LoadResult loadResult =
      persistent_config::loadRuntimeConfig();
  lastRuntimeConfigLoadStatus = loadResult.status;
  if (loadResult.status != persistent_config::LoadStatus::kLoaded) {
    savedRuntimeConfigValid = false;
    return false;
  }

  savedRuntimeConfig = loadResult.config;
  savedRuntimeConfigValid = true;
  return true;
}

void setMotionReadyState(bool ready, MotionReadyReason reason) {
  motionReady = ready;
  motionReadyReason = reason;
  if (!ready) {
    tmcOk = false;
  }
}

const __FlashStringHelper* motionReadyReasonText(MotionReadyReason reason) {
  switch (reason) {
    case MotionReadyReason::kReady:
      return F("READY");
    case MotionReadyReason::kUartDisabled:
      return F("UART disabled");
    case MotionReadyReason::kUartSetupFailed:
      return F("UART setup failed");
    case MotionReadyReason::kUartProbeFailed:
      return F("UART probe failed");
  }

  return F("UNKNOWN");
}

void printMotionBlockedMessage() {
  switch (motionReadyReason) {
    case MotionReadyReason::kUartDisabled:
      Serial.println(F("Refusing move: UART disabled."));
      return;
    case MotionReadyReason::kUartSetupFailed:
      Serial.println(F("Refusing move: UART setup failed."));
      return;
    case MotionReadyReason::kUartProbeFailed:
      Serial.println(F("Refusing move: UART offline."));
      return;
    case MotionReadyReason::kReady:
      return;
  }
}

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
  driverEnabled = true;
  delay(5);
}

void disableDriver() {
  digitalWrite(board::kEnablePin, board::kEnableActiveLow ? HIGH : LOW);
  driverEnabled = false;
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
  if (apertureMilliMm < apertureIrisMinMilliMm) {
    return apertureIrisMinMilliMm;
  }

  if (apertureMilliMm > apertureIrisMaxMilliMm) {
    return apertureIrisMaxMilliMm;
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
    return apertureIrisMinMilliMm;
  }

  const int32_t clampedTravelMilliMm = clampPositionMilliMm(travelPositionMilliMm);
  const uint32_t normalizedTravelMilliMm = static_cast<uint32_t>(
      clampedTravelMilliMm - driver_config::minimumPositionMilliMm());
  const uint32_t apertureStrokeMilliMm = currentApertureIrisStrokeMilliMm();
  const uint32_t apertureOffsetMilliMm =
      (normalizedTravelMilliMm * apertureStrokeMilliMm + (motionStrokeMilliMm / 2UL)) /
      motionStrokeMilliMm;
  return clampApertureMilliMm(apertureIrisMinMilliMm +
                              static_cast<int32_t>(apertureOffsetMilliMm));
}

int32_t apertureOpeningToTravelPositionMilliMm(int32_t apertureMilliMm) {
  const uint32_t apertureStrokeMilliMm = currentApertureIrisStrokeMilliMm();
  if (apertureStrokeMilliMm == 0) {
    return driver_config::minimumPositionMilliMm();
  }

  const int32_t clampedApertureMilliMm = clampApertureMilliMm(apertureMilliMm);
  const uint32_t normalizedApertureMilliMm = static_cast<uint32_t>(
      clampedApertureMilliMm - apertureIrisMinMilliMm);
  const uint32_t motionStrokeMilliMm = driver_config::strokeMilliMm();
  const uint32_t travelOffsetMilliMm =
      (normalizedApertureMilliMm * motionStrokeMilliMm + (apertureStrokeMilliMm / 2UL)) /
      apertureStrokeMilliMm;
  return clampPositionMilliMm(driver_config::minimumPositionMilliMm() +
                              static_cast<int32_t>(travelOffsetMilliMm));
}

bool parseMilliMm(const char* text, int32_t* milliMmOut) {
  if (milliMmOut == nullptr) {
    return false;
  }

  if (text == nullptr || text[0] == '\0') {
    return false;
  }

  const size_t length = strlen(text);
  bool negative = false;
  size_t index = 0;
  if (text[0] == '-') {
    negative = true;
    index = 1;
  } else if (text[0] == '+') {
    index = 1;
  }

  if (index >= length) {
    return false;
  }

  long whole = 0;
  bool hasWholeDigits = false;
  while (index < length) {
    const char c = text[index];
    if (c < '0' || c > '9') {
      break;
    }
    hasWholeDigits = true;
    whole = whole * 10L + (c - '0');
    ++index;
  }

  int32_t fraction = 0;
  if (index < length && text[index] == '.') {
    ++index;
    int fractionDigits = 0;
    while (index < length) {
      const char c = text[index];
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

    while (index < length && text[index] >= '0' && text[index] <= '9') {
      return false;
    }
  }

  if (!hasWholeDigits || index != length) {
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

bool zeroPositionFromBootEndstop() {
  if (!isEndstopTriggered()) {
    return false;
  }

  setKnownPositionToMinimum();
  return true;
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

  if (debugMode) {
    Serial.print(F("Aperture requested : "));
    printMilliMm(moveContext.requestedApertureMilliMm);
    Serial.println(F(" mm"));
  }

  Serial.print(F("Aperture result    : "));
  printMilliMm(travelPositionToApertureMilliMm(currentPositionMilliMm));
  Serial.println(F(" mm"));
}

const __FlashStringHelper* runtimeConfigSourceText(RuntimeConfigSource source) {
  switch (source) {
    case RuntimeConfigSource::kDefaults:
      return F("DEFAULTS");
    case RuntimeConfigSource::kEeprom:
      return F("EEPROM");
  }

  return F("UNKNOWN");
}

const __FlashStringHelper* runtimeConfigLoadStatusText(
    persistent_config::LoadStatus status) {
  switch (status) {
    case persistent_config::LoadStatus::kLoaded:
      return F("loaded");
    case persistent_config::LoadStatus::kEmpty:
      return F("empty");
    case persistent_config::LoadStatus::kInvalidMagic:
      return F("invalid magic");
    case persistent_config::LoadStatus::kVersionMismatch:
      return F("version mismatch");
    case persistent_config::LoadStatus::kCrcMismatch:
      return F("crc mismatch");
    case persistent_config::LoadStatus::kValueOutOfRange:
      return F("value out of range");
    case persistent_config::LoadStatus::kStorageTooSmall:
      return F("storage too small");
  }

  return F("unknown");
}

void printStepDelayOverrideValue(uint32_t stepDelayOverride) {
  if (stepDelayOverride == persistent_config::kNoStepDelayOverrideUs) {
    Serial.print(F("AUTO"));
    return;
  }

  Serial.print(stepDelayOverride);
  Serial.print(F(" us"));
}

void printRuntimeConfigSnapshot(const __FlashStringHelper* title,
                                const persistent_config::RuntimeConfig& config) {
  Serial.println();
  Serial.println(title);
  printDivider();

  Serial.print(F("  endstop enabled  : "));
  Serial.println(config.endstopEnabled ? F("ON") : F("OFF"));

  Serial.print(F("  debug mode       : "));
  Serial.println(config.debugMode ? F("ON") : F("OFF"));

  Serial.print(F("  run current mA   : "));
  Serial.println(config.runCurrentMa);

  Serial.print(F("  microsteps       : "));
  Serial.println(config.microsteps);

  Serial.print(F("  delay override   : "));
  printStepDelayOverrideValue(config.stepDelayOverrideUs);
  Serial.println();

  Serial.print(F("  effective delay  : "));
  Serial.println(effectiveStepDelayUsForConfig(config));

  Serial.print(F("  auto disable     : "));
  Serial.println(config.autoDisableAfterMove ? F("ON") : F("OFF"));

  Serial.print(F("  iris mm          : "));
  printMilliMm(config.apertureIrisMinMilliMm);
  Serial.print(F(".."));
  printMilliMm(config.apertureIrisMaxMilliMm);
  Serial.println();

  Serial.print(F("  device name      : "));
  Serial.println(config.arduinoName);

  printDivider();
  Serial.println();
}

void finishMove(bool aborted, MotionAbortReason reason);

void rebootBoard() {
  if (motion.active || motion.stepPinHigh || homingCycle.active) {
    Serial.println(F("Stopping motion..."));
  }

  if (motion.active || motion.stepPinHigh) {
    finishMove(true, MotionAbortReason::kManualStop);
  } else if (homingCycle.active) {
    homingCycle.active = false;
    disableDriver();
  }

  Serial.println(F("Rebooting via watchdog reset..."));
  // Avoid Serial.flush() here because CDC/USB-backed serial paths can block
  // long enough to make reboot appear hung.
  delay(100);
  noInterrupts();
  wdt_reset();
  wdt_enable(WDTO_120MS);
  while (1) {} // hold until watchdog triggers a reset
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

Tmc2209Driver::MicrostepStatus applyMicrostepsSetting(uint16_t microsteps) {
  const Tmc2209Driver::MicrostepStatus status = tmc.setMicrosteps(microsteps);
  if (status == Tmc2209Driver::MicrostepStatus::kInvalidMicrostepValue) {
    return status;
  }

  currentMicrosteps = microsteps;
  if (status == Tmc2209Driver::MicrostepStatus::kOk) {
    const uint16_t realMicrosteps = tmc.getRealMicrosteps();
    if (realMicrosteps != 0) {
      currentMicrosteps = realMicrosteps;
    }
  }
  refreshStepDelayUsFromCurrentSettings();
  tmcOk = tmc.isConnected();
  return status;
}

bool refreshMotionReadyFromUartProbe() {
  if (!tmc.isEnabled()) {
    setMotionReadyState(false, MotionReadyReason::kUartDisabled);
    return false;
  }

  if (tmc.refreshConnection() != 0) {
    setMotionReadyState(false, MotionReadyReason::kUartProbeFailed);
    return false;
  }

  tmcOk = true;
  if (!tmc.setRunCurrent(runCurrentMa)) {
    setMotionReadyState(false, MotionReadyReason::kUartSetupFailed);
    return false;
  }

  const Tmc2209Driver::MicrostepStatus status =
      applyMicrostepsSetting(currentMicrosteps);
  if (status != Tmc2209Driver::MicrostepStatus::kOk) {
    setMotionReadyState(false, MotionReadyReason::kUartSetupFailed);
    return false;
  }

  setMotionReadyState(true, MotionReadyReason::kReady);
  return true;
}

bool setMicrosteps(uint16_t microsteps) {
  const uint32_t previousStepDelayOverrideUs = stepDelayOverrideUs;
  stepDelayOverrideUs = persistent_config::kNoStepDelayOverrideUs;
  const Tmc2209Driver::MicrostepStatus status =
      applyMicrostepsSetting(microsteps);
  if (status == Tmc2209Driver::MicrostepStatus::kInvalidMicrostepValue) {
    stepDelayOverrideUs = previousStepDelayOverrideUs;
    refreshStepDelayUsFromCurrentSettings();
    Serial.println(F("ERROR: Invalid microstep value."));
    Serial.println(F("Allowed: 1, 2, 4, 8, 16, 32, 64, 128, 256"));
    return false;
  }

  if (status == Tmc2209Driver::MicrostepStatus::kOk) {
    Serial.print(F("Microsteps set to: "));
    Serial.println(currentMicrosteps);
    Serial.print(F("Step delay auto-set to: "));
    Serial.print(stepDelayUs);
    Serial.print(F(" us, homing delay: "));
    Serial.print(driver_config::homingStepDelayUsFor(currentMicrosteps));
    Serial.println(F(" us."));
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
  return true;
}

void reapplyRuntimeConfigToDriver(const __FlashStringHelper* action) {
  if (!tmcOk) {
    return;
  }

  tmc.setRunCurrent(runCurrentMa);
  const Tmc2209Driver::MicrostepStatus status =
      applyMicrostepsSetting(currentMicrosteps);
  if (status == Tmc2209Driver::MicrostepStatus::kOk) {
    return;
  }

  Serial.print(F("WARNING: "));
  Serial.print(action);
  Serial.print(F(" applied locally only: "));
  Serial.println(microstepStatusText(status));
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
  if (!refreshMotionReadyFromUartProbe()) {
    printMotionBlockedMessage();
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

  if (targetApertureMilliMm < apertureIrisMinMilliMm ||
      targetApertureMilliMm > apertureIrisMaxMilliMm) {
    Serial.print(F("Refusing aperture move: target is outside configured aperture range "));
    printMilliMm(apertureIrisMinMilliMm);
    Serial.print(F(".."));
    printMilliMm(apertureIrisMaxMilliMm);
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
  Serial.println(F("Aperture driver status"));
  printDivider();

  Serial.print(F("  motion ready: "));
  Serial.println(motionReady ? F("YES") : F("NO"));

  Serial.print(F("  motion lock: "));
  Serial.println(motionReadyReasonText(motionReadyReason));

  Serial.print(F("  motion state: "));
  Serial.println(motion.active ? F("ACTIVE") : F("IDLE"));

  Serial.print(F("  endstop pin: D"));
  Serial.println(board::kEndstopPin);

  Serial.print(F("  endstop active: "));
  Serial.println(board::kEndstopActiveHigh ? F("HIGH") : F("LOW"));

  Serial.print(F("  endstop state: "));
  Serial.println(isEndstopTriggered() ? F("TRIGGERED") : F("IDLE"));

  Serial.print(F("  endstop en: "));
  Serial.println(endstopEnabled ? F("ON") : F("OFF"));

  Serial.print(F("  debug: "));
  Serial.println(debugMode ? F("ON") : F("OFF"));

  Serial.print(F("  cfg src: "));
  Serial.println(runtimeConfigSourceText(runtimeConfigSource));

  Serial.print(F("  cfg dirty: "));
  Serial.println(runtimeConfigDirty ? F("YES") : F("NO"));

  Serial.print(F("  eeprom: "));
  Serial.println(isPersistenceEnabled() ? F("ENABLED") : F("DISABLED"));

  if (isPersistenceEnabled()) {
    Serial.print(F("  saved cfg: "));
    Serial.println(savedRuntimeConfigValid ? F("YES") : F("NO"));

    Serial.print(F("  load status: "));
    Serial.println(runtimeConfigLoadStatusText(lastRuntimeConfigLoadStatus));
  }

  Serial.print(F("  pos known: "));
  Serial.println(positionKnown ? F("YES") : F("NO"));

  Serial.print(F("  pos mm: "));
  if (positionKnown) {
    printMilliMm(currentPositionMilliMm);
    Serial.println();
  } else {
    Serial.println(F("unknown"));
  }

  Serial.print(F("  min mm: "));
  printMilliMm(driver_config::minimumPositionMilliMm());
  Serial.println();

  Serial.print(F("  max mm: "));
  printMilliMm(driver_config::maximumPositionMilliMm());
  Serial.println();

  Serial.print(F("  max mm/s: "));
  printMilliMm(static_cast<int32_t>(driver_config::kMaximumSpeedMmPerSec) * 1000L);
  Serial.println();

  if (motion.active) {
    Serial.print(F("  move: "));
    Serial.print(motion.completedSteps);
    if (motion.bounded) {
      Serial.print(F("/"));
      Serial.println(motion.totalSteps);
    } else {
      Serial.println(F(" steps"));
    }
  }

  Serial.print(F("  last abort: "));
  Serial.println(motionAbortReasonText(lastMotionAbort));

  Serial.print(F("  UART en: "));
  Serial.println(tmc.isEnabled() ? F("YES") : F("NO"));

  Serial.print(F("  TMC conn: "));
  Serial.println(tmcOk ? F("YES") : F("NO"));

  if (tmc.isEnabled()) {
    Serial.print(F("  test conn: "));
    Serial.println(tmc.testConnection());

    if (tmcOk) {
      Serial.print(F("  DRV_STATUS: 0x"));
      Serial.println(tmc.drvStatus(), HEX);

      Serial.print(F("  rms current: "));
      Serial.println(tmc.rmsCurrent());

      Serial.print(F("  toff: "));
      Serial.println(tmc.toff());

      Serial.print(F("  overtemp: "));
      Serial.println(tmc.overtemp() ? F("1") : F("0"));

      Serial.print(F("  standstill: "));
      Serial.println(tmc.standstill() ? F("1") : F("0"));

      Serial.print(F("  MSCNT: "));
      Serial.println(tmc.microstepCounter());
    }
  }

  Serial.print(F("  cfg mA: "));
  Serial.println(runCurrentMa);

  Serial.print(F("  steps/mm cfg: "));
  printMilliMm(static_cast<int32_t>(driver_config::kStepsPerMmX1000));
  Serial.println();

  Serial.print(F("  steps/mm drv: "));
  printMilliMm(static_cast<int32_t>(driver_config::kDerivedStepsPerMmX1000));
  Serial.println();

  Serial.print(F("  microsteps: "));
  Serial.println(currentMicrosteps);

  Serial.print(F("  step us: "));
  Serial.println(stepDelayUs);

  Serial.print(F("  delay ovrd: "));
  printStepDelayOverrideValue(stepDelayOverrideUs);
  Serial.println();

  Serial.print(F("  speed lim us: "));
  Serial.println(driver_config::speedLimitedStepDelayUsFor(currentMicrosteps));

  Serial.print(F("  auto step us: "));
  Serial.println(driver_config::effectiveNormalStepDelayUsFor(currentMicrosteps));

  Serial.print(F("  home us: "));
  Serial.println(driver_config::homingStepDelayUsFor(currentMicrosteps));

  Serial.print(F("  est max mm/s: "));
  printMilliMm(static_cast<int32_t>(
      driver_config::estimatedMaxSpeedMilliMmPerSecForDelay(currentMicrosteps, stepDelayUs)));
  Serial.println();

  Serial.print(F("  def move 1x: "));
  Serial.println(driver_config::kDefaultMoveSteps);

  Serial.print(F("  def move eff: "));
  Serial.println(effectiveDefaultMoveSteps());

  Serial.print(F("  step/s: "));
  Serial.println((1000000UL + stepDelayUs) / (2UL * stepDelayUs));

  Serial.print(F("  auto disable: "));
  Serial.println(autoDisableAfterMove ? F("ON") : F("OFF"));

  printMicrostepRaw();
  printDivider();
  Serial.println();
}

void printRuntimeConfigStartupSummary() {
  if (!isPersistenceEnabled()) {
    Serial.println(F("EEPROM persistence disabled; using compile-time defaults."));
    return;
  }

  if (savedRuntimeConfigValid) {
    Serial.println(F("Loaded runtime config from EEPROM."));
    return;
  }

  if (lastRuntimeConfigLoadStatus == persistent_config::LoadStatus::kEmpty) {
    Serial.println(F("No saved EEPROM config; using compile-time defaults."));
    return;
  }

  Serial.print(F("EEPROM config invalid ("));
  Serial.print(runtimeConfigLoadStatusText(lastRuntimeConfigLoadStatus));
  Serial.println(F("); using compile-time defaults."));
}

void loadRuntimeConfigAtBoot() {
  applyRuntimeConfigLocally(makeDefaultRuntimeConfig());
  runtimeConfigSource = RuntimeConfigSource::kDefaults;
  runtimeConfigDirty = false;
  savedRuntimeConfigValid = false;
  lastRuntimeConfigLoadStatus = persistent_config::LoadStatus::kEmpty;

  if (refreshSavedRuntimeConfigFromEeprom()) {
    applyRuntimeConfigLocally(savedRuntimeConfig);
    runtimeConfigSource = RuntimeConfigSource::kEeprom;
  }
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
  if (!tmc.isEnabled()) {
    setMotionReadyState(false, MotionReadyReason::kUartDisabled);
    Serial.println(F("TMC UART disabled; motion locked."));
    return;
  }

  tmcOk = tmc.begin(runCurrentMa, currentMicrosteps);

  Serial.print(F("test_connection() = "));
  Serial.println(tmc.testConnection());

  if (!tmcOk) {
    setMotionReadyState(false, MotionReadyReason::kUartSetupFailed);
    if (tmc.isConnected()) {
      Serial.print(F("ERROR: TMC setup failed: "));
      Serial.println(microstepStatusText(tmc.lastMicrostepStatus()));
    } else {
      Serial.println(F("ERROR: TMC UART not detected."));
      Serial.println(F("Check PDN_UART wiring/address."));
    }

    Serial.println(F("Motion locked until UART works."));
    return;
  }

  const uint16_t realMicrosteps = tmc.getRealMicrosteps();
  if (realMicrosteps != 0) {
    currentMicrosteps = realMicrosteps;
  }
  refreshStepDelayUsFromCurrentSettings();
  setMotionReadyState(true, MotionReadyReason::kReady);
  Serial.println(F("OK: TMC UART ready. Motion enabled."));
}

}  // namespace app

void setup() {
  const bool watchdogResetDetected = (app::bootResetFlags & _BV(WDRF)) != 0;
  MCUSR = 0;
  wdt_disable();

  Serial.begin(board::kUsbSerialBaud);
  Serial.setTimeout(50);
  delay(500);

  app::initPins();
  const bool bootZeroedFromEndstop = app::zeroPositionFromBootEndstop();
  app::loadRuntimeConfigAtBoot();

  Serial.println();
  Serial.println(F("Nano SuperMini aperture driver"));
  app::printArduinoName();
  Serial.println();

  if (bootZeroedFromEndstop) {
    Serial.println(F("Endstop active at boot; zeroed."));
    Serial.println();
  }

  if (watchdogResetDetected) {
    Serial.println(F("Reset cause: watchdog."));
    Serial.println();
  }

  app::printRuntimeConfigStartupSummary();
  app::initTmcLayer();
  app::updateRuntimeConfigDirtyFromBaseline();
  //printHelp();
  //printStatus();
  app::printPrompt();
}

void loop() {
  app::handleSerial();
  app::serviceMotion();
}
