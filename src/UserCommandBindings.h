#pragma once

#include <Arduino.h>

#include "PersistentConfig.h"
#include "Tmc2209Driver.h"

namespace app {

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

enum class RuntimeConfigSource : uint8_t {
  kDefaults = 0,
  kEeprom,
};

enum class CliMode : uint8_t {
  Normal = 0,
  Config,
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

extern Tmc2209Driver tmc;
extern MotionState motion;
extern HomingCycleState homingCycle;
extern bool autoDisableAfterMove;
extern bool debugMode;
extern bool driverEnabled;
extern bool endstopEnabled;
extern bool tmcOk;
extern bool runtimeConfigDirty;
extern bool savedRuntimeConfigValid;
extern uint16_t runCurrentMa;
extern uint16_t currentMicrosteps;
extern uint32_t stepDelayOverrideUs;
extern uint32_t stepDelayUs;
extern int32_t apertureIrisMinMilliMm;
extern int32_t apertureIrisMaxMilliMm;
extern char arduinoName[persistent_config::kArduinoNameCapacity];
extern RuntimeConfigSource runtimeConfigSource;
extern CliMode cliMode;
extern persistent_config::LoadStatus lastRuntimeConfigLoadStatus;
extern persistent_config::RuntimeConfig savedRuntimeConfig;

bool isPersistenceEnabled();
bool refreshSavedRuntimeConfigFromEeprom();
const __FlashStringHelper* runtimeConfigLoadStatusText(
    persistent_config::LoadStatus status);
void printRuntimeConfigSnapshot(const __FlashStringHelper* title,
                                const persistent_config::RuntimeConfig& config);
void printArduinoName();
void reapplyRuntimeConfigToDriver(const __FlashStringHelper* action);
void updateRuntimeConfigDirtyFromBaseline();
void refreshStepDelayUsFromCurrentSettings();
bool parseMilliMm(const char* text, int32_t* milliMmOut);
void printMilliMm(int32_t milliMm);
void printStatus();
void enableDriver();
void disableDriver();
void finishMove(bool aborted, MotionAbortReason reason);
void resetMoveContext();
long effectiveDefaultMoveSteps();
void startMove(long signedSteps);
void startAbsolutePositionMove(int32_t targetPositionMilliMm);
void startApertureOpeningMove(int32_t targetApertureMilliMm);
void homeAperture(unsigned long retractSteps);
bool setMicrosteps(uint16_t microsteps);
void rebootBoard();
bool isMotionOrHomingActive();
persistent_config::RuntimeConfig makeDefaultRuntimeConfig();
persistent_config::RuntimeConfig captureRuntimeConfig();
void applyRuntimeConfigLocally(const persistent_config::RuntimeConfig& config);

}  // namespace app
