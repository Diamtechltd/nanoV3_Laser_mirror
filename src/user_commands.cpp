#include "UserCommandBindings.h"

#include "DriverConfig.h"
#include "UserCommands.h"

namespace app {

void printPrompt() {
  switch (cliMode) {
    case CliMode::Normal:
      Serial.print(F("> "));
      break;

    case CliMode::Config:
      Serial.print(F("config> "));
      break;
  }
}

void enterConfigMode() { cliMode = CliMode::Config; }

void enterNormalMode() { cliMode = CliMode::Normal; }

void printNormalHelp() {
  Serial.println();
  Serial.println(F("Commands:"));
  Serial.println(F("  help           prints this screen"));
  Serial.println(F("  name           print configured board name"));
  Serial.println(F("  config         enter config mode"));
  Serial.println(F("  s              status"));
  Serial.println(F("  e              enable driver"));
  Serial.println(F("  d              disable driver"));
  Serial.println(F("  f [steps]      forward: default distance or given steps"));
  Serial.println(F("  b [steps]      backward: default distance or given steps"));
  Serial.println(F("  g 12.345       go to absolute position 12.345 mm"));
  Serial.println(F("  A 8.500        go to aperture opening 8.500 mm"));
  Serial.println(F("  H [steps]      home Motor, optionally retract by given steps"));
  Serial.println(F("  E              toggle endstop"));
  Serial.println(F("  D              toggle debug output"));
  Serial.println(F("  u 4            set microsteps: 1,2,4,8,16,32,64,128,256"));
  Serial.println(F("  v 2000         set step delay override in microseconds"));
  Serial.println(F("  a              toggle auto-disable after move"));
  Serial.println(F("  write memory   save current runtime config to EEPROM"));
  Serial.println(F("  reload         discard unsaved changes and reload EEPROM"));
  Serial.println(F("  reset defaults load compile-time defaults into RAM"));
  Serial.println(F("  show memory    print saved EEPROM runtime config"));
  Serial.println(F("  show defaults  print compile-time default runtime config"));
  Serial.println(F("  reboot         reset the MCU via watchdog"));
  Serial.println();
}

void printConfigHelp() {
  Serial.println();
  Serial.println(F("Config Commands:"));
  Serial.println(F("  help              prints this screen"));
  Serial.println(F("  exit              return to normal mode"));
  Serial.println(F("  i     [mA value]  set Stepper current to value mA RMS"));
  Serial.println();
}

void printHelp() {
  if (cliMode == CliMode::Config) {
    printConfigHelp();
    return;
  }

  printNormalHelp();
}

void printArduinoName() {
  Serial.print(F("name: "));
  Serial.println(driver_config::kArduinoName);
}

bool isMotionOrHomingActive() { return motion.active || homingCycle.active; }

bool ensurePersistenceEnabledForCommand() {
  if (isPersistenceEnabled()) {
    return true;
  }

  Serial.println(F("EEPROM persistence is disabled in conf.yaml."));
  return false;
}

bool ensurePersistenceCommandIdle(const __FlashStringHelper* action) {
  if (!isMotionOrHomingActive()) {
    return true;
  }

  Serial.print(F("Refusing "));
  Serial.print(action);
  Serial.println(F(": motion or homing is active."));
  return false;
}

void showSavedRuntimeConfig() {
  if (!ensurePersistenceEnabledForCommand()) {
    return;
  }

  if (!refreshSavedRuntimeConfigFromEeprom()) {
    Serial.print(F("No valid saved EEPROM config ("));
    Serial.print(runtimeConfigLoadStatusText(lastRuntimeConfigLoadStatus));
    Serial.println(F(")."));
    return;
  }

  printRuntimeConfigSnapshot(F("Saved EEPROM Runtime Config"), savedRuntimeConfig);
}

void showDefaultRuntimeConfig() {
  printRuntimeConfigSnapshot(F("Compile-Time Default Runtime Config"),
                             makeDefaultRuntimeConfig());
}

void writeRuntimeConfigToEeprom() {
  if (!ensurePersistenceEnabledForCommand() ||
      !ensurePersistenceCommandIdle(F("write memory"))) {
    return;
  }

  const persistent_config::RuntimeConfig currentConfig = captureRuntimeConfig();
  if (!persistent_config::saveRuntimeConfig(currentConfig)) {
    Serial.println(F("ERROR: Failed to save runtime config to EEPROM."));
    return;
  }

  savedRuntimeConfig = currentConfig;
  savedRuntimeConfigValid = true;
  runtimeConfigSource = RuntimeConfigSource::kEeprom;
  lastRuntimeConfigLoadStatus = persistent_config::LoadStatus::kLoaded;
  runtimeConfigDirty = false;
  Serial.println(F("Runtime config saved to EEPROM."));
}

void reloadRuntimeConfig() {
  if (!ensurePersistenceEnabledForCommand() ||
      !ensurePersistenceCommandIdle(F("reload"))) {
    return;
  }

  if (refreshSavedRuntimeConfigFromEeprom()) {
    applyRuntimeConfigLocally(savedRuntimeConfig);
    runtimeConfigSource = RuntimeConfigSource::kEeprom;
    reapplyRuntimeConfigToDriver(F("Reload"));
    runtimeConfigDirty = false;
    Serial.println(F("Reloaded runtime config from EEPROM."));
    return;
  }

  applyRuntimeConfigLocally(makeDefaultRuntimeConfig());
  runtimeConfigSource = RuntimeConfigSource::kDefaults;
  runtimeConfigDirty = false;
  reapplyRuntimeConfigToDriver(F("Reload"));
  Serial.print(F("No valid EEPROM config ("));
  Serial.print(runtimeConfigLoadStatusText(lastRuntimeConfigLoadStatus));
  Serial.println(F("); reloaded compile-time defaults."));
}

void resetRuntimeConfigToDefaults() {
  if (!ensurePersistenceEnabledForCommand() ||
      !ensurePersistenceCommandIdle(F("reset defaults"))) {
    return;
  }

  applyRuntimeConfigLocally(makeDefaultRuntimeConfig());
  runtimeConfigSource = RuntimeConfigSource::kDefaults;
  reapplyRuntimeConfigToDriver(F("Reset defaults"));
  runtimeConfigDirty = true;
  Serial.println(F("Loaded compile-time defaults into RAM. Run 'write memory' to save."));
}

void handleRunCurrentCommand(const String& arg) {
  const long value = arg.toInt();
  if (value <= 0 || value > 500) {
    Serial.println(F("ERROR: current must be 1..500 mA RMS."));
    Serial.println(F("For a small motor, start around 120..220 mA."));
    return;
  }

  runCurrentMa = static_cast<uint16_t>(value);
  if (tmcOk) {
    tmc.setRunCurrent(runCurrentMa);
  }

  Serial.print(F("Run current set to "));
  Serial.print(runCurrentMa);
  Serial.println(F(" mA RMS."));
  updateRuntimeConfigDirtyFromBaseline();
}

void handleNormalModeCommand(const String& line) {
  if (line == "write memory") {
    writeRuntimeConfigToEeprom();
    return;
  }
  if (line == "reload") {
    reloadRuntimeConfig();
    return;
  }
  if (line == "reset defaults") {
    resetRuntimeConfigToDefaults();
    return;
  }
  if (line == "show memory") {
    showSavedRuntimeConfig();
    return;
  }
  if (line == "show defaults") {
    showDefaultRuntimeConfig();
    return;
  }
  if (line == "name") {
    printArduinoName();
    return;
  }
  if (line == "reboot") {
    rebootBoard();
    return;
  }
  if (line == "config") {
    enterConfigMode();
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
      updateRuntimeConfigDirtyFromBaseline();
      break;

    case 'D':
      debugMode = !debugMode;
      Serial.print(F("Debug mode: "));
      Serial.println(debugMode ? F("ON") : F("OFF"));
      updateRuntimeConfigDirtyFromBaseline();
      break;

    case 'i':
      Serial.println(F("Enter config mode with 'config' to use 'i'."));
      break;

    case 'u':
      if (arg.length() == 0) {
        Serial.println(F("Usage: u 1 / u 2 / u 4 / u 8 / u 16 ..."));
      } else {
        if (setMicrosteps(static_cast<uint16_t>(value))) {
          updateRuntimeConfigDirtyFromBaseline();
        }
      }
      break;

    case 'v':
      if (value < 5 || value > 100000) {
        Serial.println(F("ERROR: step delay must be 5..100000 us."));
      } else {
        stepDelayOverrideUs = static_cast<uint32_t>(value);
        refreshStepDelayUsFromCurrentSettings();
        Serial.print(F("Step delay override set to "));
        Serial.print(stepDelayUs);
        Serial.print(F(" us, approx "));
        Serial.print(1000000.0 / (2.0 * stepDelayUs));
        Serial.println(F(" steps/sec."));
        updateRuntimeConfigDirtyFromBaseline();
      }
      break;

    case 'a':
      autoDisableAfterMove = !autoDisableAfterMove;
      Serial.print(F("Auto-disable after move: "));
      Serial.println(autoDisableAfterMove ? F("ON") : F("OFF"));
      updateRuntimeConfigDirtyFromBaseline();
      break;

    default:
      if (line == "help") {
        printHelp();
      } else {
        Serial.println(F("Unknown command. Type h for help."));
      }
      break;
  }
}

void handleConfigModeCommand(const String& line) {
  if (line == "exit" || line == "q") {
    enterNormalMode();
    return;
  }
  if (line == "help") {
    printHelp();
    return;
  }

  const char cmd = line.charAt(0);
  String arg = "";
  if (line.length() > 1) {
    arg = line.substring(1);
    arg.trim();
  }

  switch (cmd) {
    case 'h':
    case '?':
      printHelp();
      break;

    case 'i':
      handleRunCurrentCommand(arg);
      break;

    default:
      Serial.println(F("Unknown config command. Type h for help."));
      break;
  }
}

void handleCommand(String line) {
  line.trim();
  if (line.length() == 0) {
    return;
  }

  if (cliMode == CliMode::Config) {
    handleConfigModeCommand(line);
    return;
  }

  handleNormalModeCommand(line);
}

void handleSerial() {
  if (!Serial.available()) {
    return;
  }

  const String line = Serial.readStringUntil('\n');
  handleCommand(line);
  printPrompt();
}

}  // namespace app
