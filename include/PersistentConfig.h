#pragma once

#include <Arduino.h>

namespace persistent_config {

constexpr uint32_t kNoStepDelayOverrideUs = 0UL;
constexpr size_t kArduinoNameCapacity = 32;
constexpr size_t kMaxArduinoNameLength = kArduinoNameCapacity - 1;

struct RuntimeConfig {
  bool endstopEnabled = false;
  bool debugMode = false;
  bool autoDisableAfterMove = false;
  uint16_t runCurrentMa = 0;
  uint16_t microsteps = 0;
  uint32_t stepDelayOverrideUs = kNoStepDelayOverrideUs;
  int32_t apertureIrisMinMilliMm = 0;
  int32_t apertureIrisMaxMilliMm = 0;
  char arduinoName[kArduinoNameCapacity] = {};
};

enum class LoadStatus : uint8_t {
  kLoaded = 0,
  kEmpty,
  kInvalidMagic,
  kVersionMismatch,
  kCrcMismatch,
  kValueOutOfRange,
  kStorageTooSmall,
};

struct LoadResult {
  LoadStatus status = LoadStatus::kEmpty;
  RuntimeConfig config{};
};

bool runtimeConfigsEqual(const RuntimeConfig& left, const RuntimeConfig& right);
bool isSupportedMicrosteps(uint16_t microsteps);
bool isValidRunCurrentMa(uint16_t runCurrentMa);
bool isValidStepDelayOverrideUs(uint32_t stepDelayOverrideUs);
bool isValidApertureIrisBounds(int32_t apertureIrisMinMilliMm,
                               int32_t apertureIrisMaxMilliMm);
bool isValidArduinoName(const char* arduinoName);
bool isValidRuntimeConfig(const RuntimeConfig& config);
LoadResult loadRuntimeConfig();
bool saveRuntimeConfig(const RuntimeConfig& config);
size_t storageSizeBytes();

}  // namespace persistent_config
