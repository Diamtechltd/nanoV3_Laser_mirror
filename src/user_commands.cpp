#include "UserCommandBindings.h"

#include "DriverConfig.h"
#include "UserCommands.h"

#include <stdlib.h>
#include <string.h>

namespace app {

namespace {

constexpr size_t kCommandBufferSize = 48;

char commandBuffer[kCommandBufferSize] = {};
size_t commandLength = 0;
bool lastTerminatorWasCarriageReturn = false;

void trimInPlace(char* text) {
  if (text == nullptr) {
    return;
  }

  char* start = text;
  while (*start == ' ' || *start == '\t') {
    ++start;
  }

  if (start != text) {
    memmove(text, start, strlen(start) + 1);
  }

  size_t length = strlen(text);
  while (length > 0 &&
         (text[length - 1] == ' ' || text[length - 1] == '\t')) {
    text[length - 1] = '\0';
    --length;
  }
}

bool isExactCommand(const char* line, const char* command) {
  return strcmp(line, command) == 0;
}

bool isHelpCommand(const char* line) {
  return isExactCommand(line, "h") || isExactCommand(line, "help") ||
         isExactCommand(line, "?");
}

bool splitCommandArg(const char* line, char* commandOut, size_t commandOutSize,
                     const char** argOut) {
  if (line == nullptr || commandOut == nullptr || commandOutSize == 0 ||
      argOut == nullptr) {
    return false;
  }

  const char* separator = line;
  while (*separator != '\0' && *separator != ' ' && *separator != '\t') {
    ++separator;
  }

  size_t commandLengthLocal = static_cast<size_t>(separator - line);
  if (commandLengthLocal >= commandOutSize) {
    commandLengthLocal = commandOutSize - 1;
  }

  memcpy(commandOut, line, commandLengthLocal);
  commandOut[commandLengthLocal] = '\0';

  while (*separator == ' ' || *separator == '\t') {
    ++separator;
  }

  *argOut = separator;
  return true;
}

long parseLongValue(const char* text) {
  if (text == nullptr) {
    return 0;
  }

  char* end = nullptr;
  return strtol(text, &end, 10);
}

void printStepsPerSecondApprox() {
  Serial.println((1000000UL + stepDelayUs) / (2UL * stepDelayUs));
}

}  // namespace

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
  Serial.println(F("Cmds:"));
  Serial.println(F("  help         show help"));
  Serial.println(F("  name         print name"));
  Serial.println(F("  config       config mode"));
  Serial.println(F("  status       display status"));
  Serial.println(F("  driver [on|off] drv on / off"));
  Serial.println(F("  f [steps]    fwd steps"));
  Serial.println(F("  b [steps]    back steps"));
  Serial.println(F("  g [mm]       goto pos mm"));
  Serial.println(F("  aperture/A [mm] goto ap mm"));
  Serial.println(F("  H [steps]    home"));
  Serial.println(F("  reboot       watchdog rst"));
  Serial.println();
}

void printConfigHelp() {
  Serial.println();
  Serial.println(F("Config:"));
  Serial.println(F("  help          show help"));
  Serial.println(F("  exit          normal mode"));
  Serial.println(F("  iris [m/x] [v]iris min/max mm"));
  Serial.println(F("  name [string] rename device"));
  Serial.println(F("  i [mA]        set mA RMS"));
  Serial.println(F("  debug         toggle debug"));
  Serial.println(F("  endstop       toggle endstop"));
  Serial.println(F("  a             toggle driver auto-off"));
  Serial.println(F("  u [step]      microsteps 1/2/4/...256"));
  Serial.println(F("  v [μs]        step delay in μs"));
  Serial.println(F("  write         save cfg"));
  Serial.println(F("  reload        load saved cfg"));
  Serial.println(F("  reset         load default cfg"));
  Serial.println(F("  read          show saved cfg"));
  Serial.println(F("  defaults      show def cfg"));
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
  Serial.print(F("name:"));
  Serial.println(arduinoName);
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

void printConfigOnlyHint() {
  Serial.println(F("This command is available only in 'Config' terminal."));
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
      !ensurePersistenceCommandIdle(F("write"))) {
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
  Serial.println(F("Loaded compile-time defaults into RAM. Run 'write' to save."));
}

void handleNameCommand(const char* arg) {
  if (arg == nullptr || arg[0] == '\0') {
    printArduinoName();
    return;
  }

  if (!persistent_config::isValidArduinoName(arg)) {
    Serial.print(F("ERROR: name must be 1.."));
    Serial.print(persistent_config::kMaxArduinoNameLength);
    Serial.println(F(" chars."));
    return;
  }

  strncpy(arduinoName, arg, persistent_config::kArduinoNameCapacity - 1);
  arduinoName[persistent_config::kArduinoNameCapacity - 1] = '\0';
  Serial.print(F("Staged name:"));
  Serial.println(arduinoName);
  updateRuntimeConfigDirtyFromBaseline();
}

void printCurrentIrisBounds() {
  Serial.print(F("iris: "));
  printMilliMm(apertureIrisMinMilliMm);
  Serial.print(F(".."));
  printMilliMm(apertureIrisMaxMilliMm);
  Serial.println(F(" mm"));
}

void handleIrisCommand(const char* arg) {
  if (arg == nullptr || arg[0] == '\0') {
    printCurrentIrisBounds();
    return;
  }

  char selector[8] = {};
  const char* valueText = "";
  splitCommandArg(arg, selector, sizeof(selector), &valueText);
  if (valueText[0] == '\0') {
    Serial.println(F("Usage: iris min|max <mm>"));
    return;
  }

  int32_t valueMilliMm = 0;
  if (!parseMilliMm(valueText, &valueMilliMm)) {
    Serial.println(F("ERROR: iris mm required."));
    return;
  }

  int32_t nextMinMilliMm = apertureIrisMinMilliMm;
  int32_t nextMaxMilliMm = apertureIrisMaxMilliMm;
  if (strcmp(selector, "min") == 0) {
    nextMinMilliMm = valueMilliMm;
  } else if (strcmp(selector, "max") == 0) {
    nextMaxMilliMm = valueMilliMm;
  } else {
    Serial.println(F("Usage: iris min|max <mm>"));
    return;
  }

  if (!persistent_config::isValidApertureIrisBounds(nextMinMilliMm,
                                                    nextMaxMilliMm)) {
    Serial.println(F("ERROR: iris min < max."));
    return;
  }

  apertureIrisMinMilliMm = nextMinMilliMm;
  apertureIrisMaxMilliMm = nextMaxMilliMm;
  printCurrentIrisBounds();
  updateRuntimeConfigDirtyFromBaseline();
}

void handleRunCurrentCommand(const char* arg) {
  const long value = parseLongValue(arg);
  if (value <= 0 || value > 500) {
    Serial.println(F("ERROR: current must be 1..500 mA RMS."));
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

void handleDriverStateChange(bool enable) {
  if (enable) {
    if (motion.active) {
      Serial.println(F("Driver already enabled for active motion."));
      return;
    }

    if (driverEnabled) {
      Serial.println(F("Driver already enabled."));
      return;
    }

    enableDriver();
    Serial.println(F("Driver enabled."));
    return;
  }

  if (motion.active) {
    finishMove(true, MotionAbortReason::kManualStop);
    return;
  }

  if (!driverEnabled) {
    Serial.println(F("Driver already disabled."));
    return;
  }

  disableDriver();
  Serial.println(F("Driver disabled."));
}

void handleDriverCommand(const char* arg) {
  if (arg == nullptr || arg[0] == '\0') {
    handleDriverStateChange(!driverEnabled);
    return;
  }

  if (strcmp(arg, "on") == 0) {
    handleDriverStateChange(true);
    return;
  }

  if (strcmp(arg, "off") == 0) {
    handleDriverStateChange(false);
    return;
  }

  Serial.println(F("Usage: driver [on|off]"));
}

void handleNormalModeCommand(const char* line) {
  if (isHelpCommand(line)) {
    printHelp();
    return;
  }

  if (isExactCommand(line, "write")) {
    printConfigOnlyHint();
    return;
  }
  if (isExactCommand(line, "reload")) {
    printConfigOnlyHint();
    return;
  }
  if (isExactCommand(line, "reset defaults")) {
    printConfigOnlyHint();
    return;
  }
  if (isExactCommand(line, "read")) {
    printConfigOnlyHint();
    return;
  }
  if (isExactCommand(line, "defaults")) {
    printConfigOnlyHint();
    return;
  }
  if (isExactCommand(line, "iris")) {
    printConfigOnlyHint();
    return;
  }
  if (isExactCommand(line, "debug")) {
    printConfigOnlyHint();
    return;
  }
  if (isExactCommand(line, "endstop")) {
    printConfigOnlyHint();
    return;
  }
  if (isExactCommand(line, "con") || isExactCommand(line, "config")) {
    enterConfigMode();
    return;
  }
  if (isExactCommand(line, "name")) {
    printArduinoName();
    return;
  }
  if (isExactCommand(line, "reboot")) {
    rebootBoard();
    return;
  }

  char command[16] = {};
  const char* arg = "";
  splitCommandArg(line, command, sizeof(command), &arg);
  if (strcmp(command, "iris") == 0) {
    printConfigOnlyHint();
    return;
  }
  if (strcmp(command, "debug") == 0) {
    printConfigOnlyHint();
    return;
  }
  if (strcmp(command, "aperture") == 0 || strcmp(command, "A") == 0) {
    int32_t targetApertureMilliMm = 0;
    if (!parseMilliMm(arg, &targetApertureMilliMm)) {
      Serial.println(F("Usage: aperture 8.500 / A 8.500"));
    } else {
      startApertureOpeningMove(targetApertureMilliMm);
    }
    return;
  }
  if (strcmp(command, "status") == 0) {
    printStatus();
    return;
  }
  if (strcmp(command, "driver") == 0) {
    handleDriverCommand(arg);
    return;
  }
  if (strcmp(command, "name") == 0) {
    if (arg[0] == '\0') {
      printArduinoName();
    } else {
      printConfigOnlyHint();
    }
    return;
  }
  const char cmd = command[0];
  const long value = parseLongValue(arg);

  switch (cmd) {
    case 'h':
    case '?':
      printHelp();
      break;

    case 'f':
      resetMoveContext();
      startMove(arg[0] != '\0' ? value : effectiveDefaultMoveSteps());
      break;

    case 'b':
      resetMoveContext();
      startMove(arg[0] != '\0' ? -value : -effectiveDefaultMoveSteps());
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

    case 'H':
      if (arg[0] != '\0' && value < 0) {
        Serial.println(F("ERROR: homing retract must be 0 or greater."));
      } else {
        homeAperture(arg[0] != '\0' ? static_cast<unsigned long>(value)
                                    : driver_config::kHomingRetractSteps);
      }
      break;

    case 'i':
      printConfigOnlyHint();
      break;

    case 'u':
      printConfigOnlyHint();
      break;

    case 'v':
      printConfigOnlyHint();
      break;

    case 'a':
      printConfigOnlyHint();
      break;

    default:
      Serial.println(F("Unknown command. Type h for help."));
      break;
  }
}

void handleConfigModeCommand(const char* line) {
  if (isHelpCommand(line)) {
    printHelp();
    return;
  }

  if (isExactCommand(line, "write")) {
    writeRuntimeConfigToEeprom();
    return;
  }
  if (isExactCommand(line, "reload")) {
    reloadRuntimeConfig();
    return;
  }
  if (isExactCommand(line, "reset")) {
    resetRuntimeConfigToDefaults();
    return;
  }
  if (isExactCommand(line, "read")) {
    showSavedRuntimeConfig();
    return;
  }
  if (isExactCommand(line, "defaults")) {
    showDefaultRuntimeConfig();
    return;
  }
  if (isExactCommand(line, "end") ||isExactCommand(line, "exit") || isExactCommand(line, "q")) {
    enterNormalMode();
    return;
  }

  char command[16] = {};
  const char* arg = "";
  splitCommandArg(line, command, sizeof(command), &arg);
  if (strcmp(command, "iris") == 0) {
    handleIrisCommand(arg);
    return;
  }
  if (strcmp(command, "endstop") == 0) {
    endstopEnabled = !endstopEnabled;
    Serial.print(F("Endstop protection: "));
    Serial.println(endstopEnabled ? F("ON") : F("OFF"));
    updateRuntimeConfigDirtyFromBaseline();
    return;
  }
  if (strcmp(command, "debug") == 0) {
    debugMode = !debugMode;
    Serial.print(F("Debug mode: "));
    Serial.println(debugMode ? F("ON") : F("OFF"));
    updateRuntimeConfigDirtyFromBaseline();
    return;
  }
  const char cmd = command[0];
  const long value = parseLongValue(arg);

  switch (cmd) {
    case 'h':
    case '?':
      printHelp();
      break;

    case 'i':
      handleRunCurrentCommand(arg);
      break;

    case 'a':
      autoDisableAfterMove = !autoDisableAfterMove;
      Serial.print(F("Auto-disable after move: "));
      Serial.println(autoDisableAfterMove ? F("ON") : F("OFF"));
      updateRuntimeConfigDirtyFromBaseline();
      break;

    case 'n':
      if (strcmp(command, "name") == 0) {
        handleNameCommand(arg);
        break;
      }
      Serial.println(F("Unknown config command. Type h for help."));
      break;

    case 'u':
      if (arg[0] == '\0') {
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
        Serial.print(F("Delay set "));
        Serial.print(stepDelayUs);
        Serial.print(F(" us, step/s "));
        printStepsPerSecondApprox();
        updateRuntimeConfigDirtyFromBaseline();
      }
      break;

    default:
      Serial.println(F("Unknown config command. Type h for help."));
      break;
  }
}

void handleCommand(const char* line) {
  if (line == nullptr || line[0] == '\0') {
    return;
  }

  if (cliMode == CliMode::Config) {
    handleConfigModeCommand(line);
    return;
  }

  handleNormalModeCommand(line);
}

void handleSerial() {
  while (Serial.available()) {
    const char incoming = static_cast<char>(Serial.read());

    if (incoming == '\r' || incoming == '\n') {
      if (incoming == '\n' && lastTerminatorWasCarriageReturn) {
        lastTerminatorWasCarriageReturn = false;
        continue;
      }

      lastTerminatorWasCarriageReturn = incoming == '\r';
      commandBuffer[commandLength] = '\0';
      trimInPlace(commandBuffer);
      handleCommand(commandBuffer);
      printPrompt();
      commandLength = 0;
      commandBuffer[0] = '\0';
      continue;
    }

    lastTerminatorWasCarriageReturn = false;

    if (commandLength + 1 < kCommandBufferSize) {
      commandBuffer[commandLength++] = incoming;
      commandBuffer[commandLength] = '\0';
    }
  }
}

}  // namespace app
