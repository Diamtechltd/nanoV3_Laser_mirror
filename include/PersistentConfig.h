#pragma once

#include <Arduino.h>

namespace persistent_config {

constexpr uint32_t kNoStepDelayOverrideUs = 0UL;

struct RuntimeConfig {
  bool endstopEnabled = false;
  bool debugMode = false;
  bool autoDisableAfterMove = false;
  uint16_t runCurrentMa = 0;
  uint16_t microsteps = 0;
  uint32_t stepDelayOverrideUs = kNoStepDelayOverrideUs;
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
bool isValidRuntimeConfig(const RuntimeConfig& config);
LoadResult loadRuntimeConfig();
bool saveRuntimeConfig(const RuntimeConfig& config);
size_t storageSizeBytes();

}  // namespace persistent_config
