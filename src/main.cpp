#include <Arduino.h>

#include "BoardConfig.h"
#include "DriverConfig.h"
#include "Tmc2209Driver.h"

namespace {

Tmc2209Driver tmc;

bool autoDisableAfterMove = true;
bool motionReady = true;
bool tmcOk = false;

uint32_t stepDelayUs = driver_config::kDefaultStepDelayUs;
uint16_t runCurrentMa = driver_config::kDefaultCurrentMa;
uint16_t currentMicrosteps = driver_config::kDefaultMicrosteps;

void enableDriver() {
  digitalWrite(board::kEnablePin, board::kEnableActiveLow ? LOW : HIGH);
  delay(5);
}

void disableDriver() {
  digitalWrite(board::kEnablePin, board::kEnableActiveLow ? HIGH : LOW);
  delay(5);
}

void printDivider() { Serial.println(F("----------------------------------------")); }

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
  switch (microsteps) {
    case 1:
    case 2:
    case 4:
    case 8:
    case 16:
    case 32:
    case 64:
    case 128:
    case 256:
      break;
    default:
      Serial.println(F("ERROR: Invalid microstep value."));
      Serial.println(F("Allowed: 1, 2, 4, 8, 16, 32, 64, 128, 256"));
      return false;
  }

  currentMicrosteps = microsteps;

  if (tmcOk && !tmc.setMicrosteps(microsteps)) {
    Serial.println(F("ERROR: Failed to push microstep setting over UART."));
    return false;
  }

  Serial.print(F("Microsteps set to: "));
  Serial.println(currentMicrosteps);
  return true;
}

void printStatus() {
  Serial.println();
  Serial.println(F("Nano SuperMini aperture driver status"));
  printDivider();

  Serial.print(F("  motion ready     : "));
  Serial.println(motionReady ? F("YES") : F("NO"));

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

void pulseStep() {
  digitalWrite(board::kStepPin, HIGH);
  delayMicroseconds(stepDelayUs);
  digitalWrite(board::kStepPin, LOW);
  delayMicroseconds(stepDelayUs);
}

void moveSteps(long steps) {
  if (!motionReady) {
    Serial.println(F("Refusing move: motion baseline not ready."));
    return;
  }

  if (steps == 0) {
    Serial.println(F("Move ignored: 0 steps."));
    return;
  }

  const bool logicalForward = steps > 0;
  const unsigned long absSteps = static_cast<unsigned long>(labs(steps));
  const bool physicalDir =
      board::kInvertDirection ? !logicalForward : logicalForward;

  digitalWrite(board::kDirPin, physicalDir ? HIGH : LOW);
  delayMicroseconds(200);

  enableDriver();

  Serial.println();
  Serial.print(F("Move "));
  Serial.print(logicalForward ? F("forward ") : F("backward "));
  Serial.print(absSteps);
  Serial.println(F(" steps"));

  uint16_t mscntBefore = 0;
  if (tmcOk) {
    mscntBefore = tmc.microstepCounter();
    Serial.print(F("MSCNT before: "));
    Serial.println(mscntBefore);
  } else {
    Serial.println(F("UART offline: running STEP/DIR only."));
  }

  pulseStep();

  if (tmcOk) {
    const uint16_t mscntAfterOne = tmc.microstepCounter();
    Serial.print(F("MSCNT after 1 step: "));
    Serial.println(mscntAfterOne);

    if (mscntAfterOne == mscntBefore) {
      Serial.println(F("WARNING: MSCNT did not change after first step."));
    } else {
      Serial.println(F("OK: TMC saw STEP pulse."));
    }
  }

  for (unsigned long i = 1; i < absSteps; ++i) {
    pulseStep();
  }

  Serial.println(F("Move done."));

  if (autoDisableAfterMove) {
    delay(100);
    disableDriver();
    Serial.println(F("Driver disabled after move."));
  }

  Serial.println();
}

void printHelp() {
  Serial.println();
  Serial.println(F("Commands:"));
  Serial.println(F("  h              help"));
  Serial.println(F("  s              status"));
  Serial.println(F("  e              enable driver"));
  Serial.println(F("  d              disable driver"));
  Serial.println(F("  f              move forward default steps"));
  Serial.println(F("  b              move backward default steps"));
  Serial.println(F("  f 2000         move forward 2000 steps"));
  Serial.println(F("  b 2000         move backward 2000 steps"));
  Serial.println(F("  m 1000         move signed steps"));
  Serial.println(F("  m -1000        move signed steps backward"));
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
      enableDriver();
      Serial.println(F("Driver enabled."));
      break;

    case 'd':
      disableDriver();
      Serial.println(F("Driver disabled."));
      break;

    case 'f':
      moveSteps(arg.length() ? value : driver_config::kDefaultMoveSteps);
      break;

    case 'b':
      moveSteps(arg.length() ? -value : -driver_config::kDefaultMoveSteps);
      break;

    case 'm':
      if (arg.length() == 0) {
        Serial.println(F("Usage: m 1000 or m -1000"));
      } else {
        moveSteps(value);
      }
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
    Serial.println(F("ERROR: TMC2209 not detected over UART."));
    Serial.println(F("Check PDN_UART wiring, resistor, ground, and driver address."));
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

void loop() { handleSerial(); }
