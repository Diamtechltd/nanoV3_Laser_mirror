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
  kHomingSeek,
  kHomingRetract,
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

Tmc2209Driver tmc;
MotionState motion;

bool autoDisableAfterMove = driver_config::kAutoDisableAfterMove;
bool homingEnabled = driver_config::kHomingEnabled;
bool motionReady = true;
bool tmcOk = false;

uint32_t stepDelayUs = driver_config::kDefaultStepDelayUs;
uint16_t runCurrentMa = driver_config::kDefaultCurrentMa;
uint16_t currentMicrosteps = driver_config::kDefaultMicrosteps;
MotionAbortReason lastMotionAbort = MotionAbortReason::kNone;

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
    case MotionMode::kHomingSeek:
      return !logicalForward;
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

  if (status == Tmc2209Driver::MicrostepStatus::kOk) {
    Serial.print(F("Microsteps set to: "));
    Serial.println(currentMicrosteps);
    tmcOk = tmc.isConnected();
    return true;
  }

  Serial.print(F("WARNING: "));
  Serial.println(microstepStatusText(status));
  Serial.println(F("Microstep setting saved locally but not applied to the driver."));
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
  digitalWrite(board::kStepPin, LOW);

  if (aborted) {
    lastMotionAbort = reason;
    Serial.print(F("ERROR: Move aborted ("));
    Serial.print(motionAbortReasonText(reason));
    Serial.println(F(")."));
  } else {
    lastMotionAbort = MotionAbortReason::kNone;
    Serial.println(finishedMode == MotionMode::kHomingRetract ? F("Homing complete.")
                                                              : F("Move done."));
  }

  resetMotionState();

  const bool shouldDisable =
      finishedMode == MotionMode::kNormal ? (autoDisableAfterMove || aborted)
                                          : true;
  if (shouldDisable) {
    if (!aborted && finishedMode == MotionMode::kNormal) {
      delay(100);
    }

    disableDriver();
    Serial.println(finishedMode == MotionMode::kNormal
                       ? F("Driver disabled after move.")
                       : F("Driver disabled after homing."));
  }

  if (!aborted && finishedMode == MotionMode::kHomingRetract &&
      isEndstopTriggered()) {
    Serial.println(F("WARNING: Endstop still active after retract."));
  }

  Serial.println();
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

  if (!allowTriggeredStart && isEndstopTriggered() &&
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

  Serial.println();
  Serial.print(F("Move "));
  Serial.print(motion.logicalForward ? F("forward ") : F("backward "));
  Serial.print(motion.totalSteps);
  Serial.println(F(" steps"));

  if (tmcOk) {
    motion.mscntBefore = tmc.microstepCounter();
    Serial.print(F("MSCNT before: "));
    Serial.println(motion.mscntBefore);
  } else {
    Serial.println(F("UART offline: running STEP/DIR only."));
  }
}

void finishHomingWithoutRetract() {
  Serial.println(F("Endstop triggered."));
  Serial.println(F("No homing retract requested."));
  disableDriver();
  Serial.println(F("Homing complete."));
  Serial.println(F("Driver disabled after homing."));
  lastMotionAbort = MotionAbortReason::kNone;
  Serial.println();
}

void startHomingRetract(unsigned long retractSteps) {
  if (retractSteps == 0) {
    finishHomingWithoutRetract();
    return;
  }

  const bool retractForward = driver_config::kHomingDirectionNegative;
  if (!beginMotion(retractForward, retractSteps, true,
                   driver_config::kHomingStepDelayUs,
                   MotionMode::kHomingRetract, true)) {
    return;
  }

  Serial.print(F("Endstop triggered. Retracting "));
  Serial.print(retractSteps);
  Serial.println(F(" steps away from endstop."));
}

void handleHomingTrigger() {
  const unsigned long retractSteps = motion.plannedRetractSteps;

  digitalWrite(board::kStepPin, LOW);
  resetMotionState();
  startHomingRetract(retractSteps);
}

void homeAperture(unsigned long retractSteps) {
  if (!homingEnabled) {
    Serial.println(F("Homing is disabled. Toggle it with E first."));
    return;
  }

  if (isEndstopTriggered()) {
    lastMotionAbort = MotionAbortReason::kEndstopTriggered;
    Serial.println(F("Refusing homing: endstop input is already active."));
    return;
  }

  const bool homingForward = !driver_config::kHomingDirectionNegative;
  if (!beginMotion(homingForward, 0, false, driver_config::kHomingStepDelayUs,
                   MotionMode::kHomingSeek, false)) {
    return;
  }

  motion.plannedRetractSteps = retractSteps;

  Serial.println();
  Serial.print(F("Homing "));
  Serial.print(homingForward ? F("forward") : F("backward"));
  Serial.print(F(" at "));
  Serial.print(driver_config::kHomingStepDelayUs);
  Serial.println(F(" us per edge."));
}

void serviceMotion() {
  if (!motion.active) {
    return;
  }

  if (isEndstopTriggered() &&
      motionDrivesIntoMinimumEndstop(motion.mode, motion.logicalForward)) {
    if (motion.mode == MotionMode::kHomingSeek) {
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
    Serial.print(F("MSCNT after 1 step: "));
    Serial.println(mscntAfterOne);

    if (mscntAfterOne == motion.mscntBefore) {
      Serial.println(F("WARNING: MSCNT did not change after first step."));
    } else {
      Serial.println(F("OK: TMC saw STEP pulse."));
    }

    motion.checkedFirstStep = true;
  }

  if (motion.bounded && motion.completedSteps >= motion.totalSteps) {
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

  Serial.print(F("  homing enabled   : "));
  Serial.println(homingEnabled ? F("ON") : F("OFF"));

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

  Serial.print(F("  microsteps       : "));
  Serial.println(currentMicrosteps);

  Serial.print(F("  step delay us    : "));
  Serial.println(stepDelayUs);

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
  Serial.println(F("  D7 LOW         blocks backward motion into minimum"));
  Serial.println(F("  f              move forward default steps"));
  Serial.println(F("  b              move backward default steps"));
  Serial.println(F("  f 2000         move forward 2000 steps"));
  Serial.println(F("  b 2000         move backward 2000 steps"));
  Serial.println(F("  m 1000         move signed steps"));
  Serial.println(F("  m -1000        move signed steps backward"));
  Serial.println(F("  H              home toward endstop"));
  Serial.println(F("  H 50           home, then retract 50 steps"));
  Serial.println(F("  E              toggle homing feature"));
  Serial.println(F("  i 180          set run current to 180 mA RMS"));
  Serial.println(F("  u 4            set microsteps: 1,2,4,8,16,32,64,128,256"));
  Serial.println(F("  v 2000         set step delay in microseconds"));
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
      startMove(arg.length() ? value : driver_config::kDefaultMoveSteps);
      break;

    case 'b':
      startMove(arg.length() ? -value : -driver_config::kDefaultMoveSteps);
      break;

    case 'm':
      if (arg.length() == 0) {
        Serial.println(F("Usage: m 1000 or m -1000"));
      } else {
        startMove(value);
      }
      break;

    case 'H':
      if (arg.length() && value < 0) {
        Serial.println(F("ERROR: homing retract must be 0 or greater."));
      } else {
        homeAperture(arg.length() ? static_cast<unsigned long>(value)
                                  : driver_config::kHomingRetractSteps);
      }
      break;

    case 'E':
      homingEnabled = !homingEnabled;
      Serial.print(F("Homing feature: "));
      Serial.println(homingEnabled ? F("ON") : F("OFF"));
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
      if (value < 50 || value > 100000) {
        Serial.println(F("ERROR: step delay must be 50..100000 us."));
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
  Serial.println(F("PlatformIO baseline for STEP/DIR + optional TMC2209 UART"));
  Serial.println();

  initTmcLayer();
  printHelp();
  printStatus();
}

void loop() {
  handleSerial();
  serviceMotion();
}
