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
uint8_t selectedAxis = 0;

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

bool parseAxisIndexedCommand(const char* token, char* baseCommandOut,
                             size_t baseCommandOutSize, uint8_t* axisOut,
                             bool* hasAxisOut) {
  if (token == nullptr || baseCommandOut == nullptr || baseCommandOutSize == 0 ||
      axisOut == nullptr || hasAxisOut == nullptr) {
    return false;
  }

  *hasAxisOut = false;
  *axisOut = 0;

  const char* openBracket = strchr(token, '[');
  if (openBracket == nullptr) {
    const char cmd = token[0];
    if ((cmd == 'f' || cmd == 'b' || cmd == 'g' || cmd == 'H' || cmd == 'A') &&
        token[1] >= '0' && token[1] <= '9') {
      uint16_t axisValue = 0;
      for (const char* cursor = token + 1; *cursor != '\0'; ++cursor) {
        if (*cursor < '0' || *cursor > '9') {
          return false;
        }
        axisValue = static_cast<uint16_t>(axisValue * 10U +
                                          static_cast<uint16_t>(*cursor - '0'));
        if (axisValue > 255U) {
          return false;
        }
      }

      baseCommandOut[0] = cmd;
      baseCommandOut[1] = '\0';
      *axisOut = static_cast<uint8_t>(axisValue);
      *hasAxisOut = true;
      return true;
    }

    strncpy(baseCommandOut, token, baseCommandOutSize - 1);
    baseCommandOut[baseCommandOutSize - 1] = '\0';
    return true;
  }

  const char* closeBracket = strchr(openBracket + 1, ']');
  if (closeBracket == nullptr || closeBracket[1] != '\0') {
    return false;
  }

  const size_t baseLength = static_cast<size_t>(openBracket - token);
  if (baseLength == 0 || baseLength >= baseCommandOutSize) {
    return false;
  }

  memcpy(baseCommandOut, token, baseLength);
  baseCommandOut[baseLength] = '\0';

  if (closeBracket == openBracket + 1) {
    return false;
  }

  uint16_t axisValue = 0;
  for (const char* cursor = openBracket + 1; cursor < closeBracket; ++cursor) {
    if (*cursor < '0' || *cursor > '9') {
      return false;
    }

    axisValue = static_cast<uint16_t>(axisValue * 10U + static_cast<uint16_t>(*cursor - '0'));
    if (axisValue > 255U) {
      return false;
    }
  }

  *axisOut = static_cast<uint8_t>(axisValue);
  *hasAxisOut = true;
  return true;
}

long parseLongValue(const char* text) {
  if (text == nullptr) {
    return 0;
  }

  char* end = nullptr;
  return strtol(text, &end, 10);
}

bool parseAxisSuffix(const char* text, const char* prefix, uint8_t* axisOut) {
  if (text == nullptr || prefix == nullptr || axisOut == nullptr) {
    return false;
  }

  const size_t prefixLength = strlen(prefix);
  if (strncmp(text, prefix, prefixLength) != 0) {
    return false;
  }

  const char* suffix = text + prefixLength;
  if (*suffix == '\0') {
    return false;
  }

  uint16_t axisValue = 0;
  for (const char* cursor = suffix; *cursor != '\0'; ++cursor) {
    if (*cursor < '0' || *cursor > '9') {
      return false;
    }

    axisValue = static_cast<uint16_t>(axisValue * 10U +
                                      static_cast<uint16_t>(*cursor - '0'));
    if (axisValue > 255U) {
      return false;
    }
  }

  *axisOut = static_cast<uint8_t>(axisValue);
  return true;
}

bool parseAxisBracketSuffix(const char* text, const char* prefix,
                            uint8_t* axisOut) {
  if (text == nullptr || prefix == nullptr || axisOut == nullptr) {
    return false;
  }

  const size_t prefixLength = strlen(prefix);
  if (strncmp(text, prefix, prefixLength) != 0) {
    return false;
  }

  const char* suffix = text + prefixLength;
  if (suffix[0] != '[') {
    return false;
  }

  const char* closeBracket = strchr(suffix + 1, ']');
  if (closeBracket == nullptr || closeBracket[1] != '\0' || closeBracket == suffix + 1) {
    return false;
  }

  uint16_t axisValue = 0;
  for (const char* cursor = suffix + 1; cursor < closeBracket; ++cursor) {
    if (*cursor < '0' || *cursor > '9') {
      return false;
    }

    axisValue = static_cast<uint16_t>(axisValue * 10U +
                                      static_cast<uint16_t>(*cursor - '0'));
    if (axisValue > 255U) {
      return false;
    }
  }

  *axisOut = static_cast<uint8_t>(axisValue);
  return true;
}

bool resolveAxisSelection(uint8_t axisIndex, bool printResult) {
  if (axisIndex >= driver_config::kAxisCount) {
    Serial.print(F("ERROR: axis out of range. Valid axis: 0.."));
    Serial.println(driver_config::kAxisCount - 1);
    return false;
  }

  selectedAxis = axisIndex;
  if (printResult) {
    Serial.print(F("Selected axis: "));
    Serial.println(selectedAxis);
  }
  return true;
}

bool tryHandleAxisSelectionCommand(const char* command, const char* arg,
                                   bool printResult) {
  if (command == nullptr) {
    return false;
  }

  uint8_t axisIndex = 0;
  if (parseAxisSuffix(command, "config", &axisIndex)) {
    return resolveAxisSelection(axisIndex, printResult);
  }

  if (parseAxisBracketSuffix(command, "config", &axisIndex)) {
    return resolveAxisSelection(axisIndex, printResult);
  }

  if (strcmp(command, "config") == 0 && arg != nullptr && arg[0] != '\0') {
    const long axisValue = parseLongValue(arg);
    if (axisValue < 0 || axisValue > 255) {
      Serial.println(F("ERROR: config axis must be 0..255."));
      return true;
    }

    return resolveAxisSelection(static_cast<uint8_t>(axisValue), printResult);
  }

  if (strcmp(command, "motor") == 0) {
    if (arg == nullptr || arg[0] == '\0') {
      Serial.print(F("Selected axis: "));
      Serial.println(selectedAxis);
      return true;
    }

    const long axisValue = parseLongValue(arg);
    if (axisValue < 0 || axisValue > 255) {
      Serial.println(F("ERROR: motor axis must be 0..255."));
      return true;
    }

    return resolveAxisSelection(static_cast<uint8_t>(axisValue), printResult);
  }

  return false;
}

void printStepsPerSecondApprox() {
  Serial.println((1000000UL + stepDelayUs) / (2UL * stepDelayUs));
}

}  // namespace

void printPrompt() {
  Serial.print(arduinoName);
  Serial.print(F(" "));

  switch (cliMode) {
    case CliMode::Normal:
      Serial.print(F("axis"));
      Serial.print(selectedAxis);
      Serial.print(F(" > "));
      break;

    case CliMode::Config:
      Serial.print(F("config"));
      Serial.print(selectedAxis);
      Serial.print(F(" > "));
      break;
  }
}

void enterConfigMode() { cliMode = CliMode::Config; }

void enterNormalMode() { cliMode = CliMode::Normal; }

void printNormalHelp() {
  Serial.println();
  Serial.println(F("Cmds: help name config status"));
  Serial.println(F("Move: f[n] b[n] g[0] aperture[0]/A[0] H[0]"));
  Serial.println(F("Axis: config0/config1..., motor <n>"));
  Serial.println(F("Other: driver [on|off], enable, disable, reboot"));
  Serial.println();
}

void printConfigHelp() {
  Serial.println();
  Serial.println(F("Config: help exit write reload reset read defaults"));
  Serial.println(F("Axis: config0/config1..., motor <n>"));
  Serial.println(F("Run: driver [on|off], enable, disable"));
  Serial.println(F("Set: i <mA>, u <uStep>, v <us>, iris min|max <mm>"));
  Serial.println(F("Set: name <text>"));
  Serial.println(F("Toggle: debug endstop a"));
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
  Serial.println(F("Config mode only."));
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

void handleDriverStateChange(bool enable, uint8_t axisIndex) {
  if (enable) {
    if (motion.active) {
      Serial.println(F("Driver already enabled for active motion."));
      return;
    }

    if (driverEnabledByAxis[axisIndex]) {
      Serial.println(F("Driver already enabled."));
      return;
    }

    enableDriver(axisIndex);
    Serial.println(F("Driver enabled."));
    return;
  }

  if (motion.active) {
    finishMove(true, MotionAbortReason::kManualStop);
    return;
  }

  if (!driverEnabledByAxis[axisIndex]) {
    Serial.println(F("Driver already disabled."));
    return;
  }

  disableDriver(axisIndex);
  Serial.println(F("Driver disabled."));
}

void handleDriverCommand(const char* arg, uint8_t axisIndex) {
  if (arg == nullptr || arg[0] == '\0') {
    handleDriverStateChange(!driverEnabledByAxis[axisIndex], axisIndex);
    return;
  }

  if (strcmp(arg, "on") == 0) {
    handleDriverStateChange(true, axisIndex);
    return;
  }

  if (strcmp(arg, "off") == 0) {
    handleDriverStateChange(false, axisIndex);
    return;
  }

  Serial.println(F("Usage: driver [on|off]"));
}

void handleNormalModeCommand(const char* line) {
  if (isHelpCommand(line)) {
    printHelp();
    return;
  }

  char command[16] = {};
  const char* arg = "";
  splitCommandArg(line, command, sizeof(command), &arg);

  if (tryHandleAxisSelectionCommand(command, arg, true)) {
    if (strncmp(command, "config", 6) == 0) {
      enterConfigMode();
    }
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

  char baseCommand[16] = {};
  uint8_t axisIndex = 0;
  bool hasAxisIndex = false;
  if (!parseAxisIndexedCommand(command, baseCommand, sizeof(baseCommand),
                               &axisIndex, &hasAxisIndex)) {
    Serial.println(F("ERROR: invalid axis format. Use command[index], ex: f[0] 4000"));
    return;
  }

  if (hasAxisIndex && axisIndex >= driver_config::kAxisCount) {
    Serial.print(F("ERROR: axis out of range. Valid axis: 0.."));
    Serial.println(driver_config::kAxisCount - 1);
    return;
  }

  const uint8_t targetAxis = hasAxisIndex ? axisIndex : selectedAxis;

  if (strcmp(baseCommand, "iris") == 0) {
    printConfigOnlyHint();
    return;
  }
  if (strcmp(baseCommand, "debug") == 0) {
    printConfigOnlyHint();
    return;
  }
  if (strcmp(baseCommand, "aperture") == 0 || strcmp(baseCommand, "A") == 0) {
    int32_t targetApertureMilliMm = 0;
    if (!parseMilliMm(arg, &targetApertureMilliMm)) {
      Serial.println(F("Usage: aperture[0] 8.500 / A[0] 8.500"));
    } else {
      startApertureOpeningMove(targetAxis, targetApertureMilliMm);
    }
    return;
  }
  if (strcmp(baseCommand, "status") == 0) {
    printStatus();
    return;
  }
  if (strcmp(baseCommand, "driver") == 0) {
    handleDriverCommand(arg, targetAxis);
    return;
  }
  if (strcmp(baseCommand, "enable") == 0) {
    handleDriverStateChange(true, targetAxis);
    return;
  }
  if (strcmp(baseCommand, "disable") == 0) {
    handleDriverStateChange(false, targetAxis);
    return;
  }
  if (strcmp(baseCommand, "name") == 0) {
    if (arg[0] == '\0') {
      printArduinoName();
    } else {
      printConfigOnlyHint();
    }
    return;
  }
  const char cmd = baseCommand[0];
  const long value = parseLongValue(arg);

  switch (cmd) {
    case 'h':
    case '?':
      printHelp();
      break;

    case 'f':
      resetMoveContext();
      startMove(targetAxis, arg[0] != '\0' ? value : effectiveDefaultMoveSteps());
      break;

    case 'b':
      resetMoveContext();
      startMove(targetAxis,
                arg[0] != '\0' ? -value : -effectiveDefaultMoveSteps());
      break;

    case 'g': {
      resetMoveContext();
      int32_t targetPositionMilliMm = 0;
      if (!parseMilliMm(arg, &targetPositionMilliMm)) {
        Serial.println(F("Usage: g[0] 12.345"));
      } else {
        startAbsolutePositionMove(targetAxis, targetPositionMilliMm);
      }
      break;
    }

    case 'H':
      if (arg[0] != '\0' && value < 0) {
        Serial.println(F("ERROR: homing retract must be 0 or greater."));
      } else {
        homeAperture(targetAxis, arg[0] != '\0' ? static_cast<unsigned long>(value)
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

  if (tryHandleAxisSelectionCommand(command, arg, true)) {
    return;
  }

  if (strcmp(command, "iris") == 0) {
    handleIrisCommand(arg);
    return;
  }
  if (strcmp(command, "driver") == 0) {
    handleDriverCommand(arg, selectedAxis);
    return;
  }
  if (strcmp(command, "enable") == 0) {
    handleDriverStateChange(true, selectedAxis);
    return;
  }
  if (strcmp(command, "disable") == 0) {
    handleDriverStateChange(false, selectedAxis);
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
