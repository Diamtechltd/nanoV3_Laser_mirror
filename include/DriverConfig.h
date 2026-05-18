#pragma once

#include <Arduino.h>
#include "GeneratedBuildConfig.h"

namespace driver_config {

constexpr float kRSense = 0.11f;
constexpr uint8_t kDriverAddress = 0b00;

constexpr uint16_t kStepsPerRevolution =
    generated_build_config::kStepsPerRevolution;
constexpr uint16_t kFullStrokeSteps1x = generated_build_config::kFullStrokeSteps1x;
constexpr uint16_t kDefaultCurrentMa = generated_build_config::kDefaultCurrentMa;
constexpr uint16_t kDefaultMicrosteps = generated_build_config::kDefaultMicrosteps;
constexpr long kDefaultMoveSteps = generated_build_config::kDefaultMoveSteps;
constexpr uint32_t kDirectionSetupDelayUs = 200;
constexpr unsigned long kMaxMoveDurationMs = 30000UL;
constexpr unsigned long kMotionStatusIntervalMs = 0UL;
constexpr bool kEndstopEnabled = generated_build_config::kEndstopEnabled;
constexpr uint16_t kMinimumPositionMm = generated_build_config::kMinimumPositionMm;
constexpr uint16_t kMaximumPositionMm = generated_build_config::kMaximumPositionMm;
constexpr uint16_t kMaximumSpeedMmPerSec =
    generated_build_config::kMaximumSpeedMmPerSec;
constexpr bool kAutoDisableAfterMove = generated_build_config::kAutoDisableAfterMove;
constexpr uint16_t kHomingRetractSteps = generated_build_config::kHomingRetractSteps;
constexpr bool kHomingDirectionNegative = generated_build_config::kHomingDirectionNegative;
constexpr uint16_t kHomingDoubleTapDistanceMm =
    generated_build_config::kHomingDoubleTapDistanceMm;
constexpr uint16_t kHomingSecondSeekDelayMultiplier =
    generated_build_config::kHomingSecondSeekDelayMultiplier;

// Keep UART optional so STEP/DIR motion can be brought up before UART wiring is finalized.
constexpr bool kDriverUartEnabled = generated_build_config::kDriverUartEnabled;
constexpr unsigned long kDriverUartBaud = generated_build_config::kDriverUartBaud;

inline uint32_t normalStepDelayUsFor(uint16_t microsteps) {
  switch (microsteps) {
    case 1:
      return generated_build_config::kStepDelayUsForMicrosteps1;
    case 2:
      return generated_build_config::kStepDelayUsForMicrosteps2;
    case 4:
      return generated_build_config::kStepDelayUsForMicrosteps4;
    case 8:
      return generated_build_config::kStepDelayUsForMicrosteps8;
    case 16:
      return generated_build_config::kStepDelayUsForMicrosteps16;
    case 32:
      return generated_build_config::kStepDelayUsForMicrosteps32;
    case 64:
      return generated_build_config::kStepDelayUsForMicrosteps64;
    case 128:
      return generated_build_config::kStepDelayUsForMicrosteps128;
    case 256:
      return generated_build_config::kStepDelayUsForMicrosteps256;
    default:
      return generated_build_config::kStepDelayUsForMicrosteps1;
  }
}

inline int32_t minimumPositionMilliMm() {
  return static_cast<int32_t>(kMinimumPositionMm) * 1000L;
}

inline int32_t maximumPositionMilliMm() {
  return static_cast<int32_t>(kMaximumPositionMm) * 1000L;
}

inline uint32_t strokeMilliMm() {
  return static_cast<uint32_t>(maximumPositionMilliMm() - minimumPositionMilliMm());
}

inline uint32_t activeStepsPerStroke(uint16_t microsteps) {
  return static_cast<uint32_t>(kFullStrokeSteps1x) * static_cast<uint32_t>(microsteps);
}

inline uint32_t speedLimitedStepDelayUsFor(uint16_t microsteps) {
  const uint32_t denominator =
      2UL * activeStepsPerStroke(microsteps) * static_cast<uint32_t>(kMaximumSpeedMmPerSec);
  const uint32_t numerator = 1000UL * strokeMilliMm();
  return (numerator + denominator - 1UL) / denominator;
}

inline uint32_t effectiveNormalStepDelayUsFor(uint16_t microsteps) {
  const uint32_t safeDelayUs = normalStepDelayUsFor(microsteps);
  const uint32_t speedDelayUs = speedLimitedStepDelayUsFor(microsteps);
  return safeDelayUs > speedDelayUs ? safeDelayUs : speedDelayUs;
}

inline uint32_t estimatedMaxSpeedMilliMmPerSecForDelay(uint16_t microsteps,
                                                       uint32_t delayUs) {
  if (delayUs == 0) {
    return 0;
  }

  const uint32_t numerator = 1000000UL * strokeMilliMm();
  const uint32_t denominator = 2UL * activeStepsPerStroke(microsteps) * delayUs;
  return numerator / denominator;
}

inline uint32_t homingStepDelayUsFor(uint16_t microsteps) {
  return effectiveNormalStepDelayUsFor(microsteps) * 2UL;
}

inline uint32_t secondSeekHomingStepDelayUsFor(uint16_t microsteps) {
  return homingStepDelayUsFor(microsteps) *
         static_cast<uint32_t>(kHomingSecondSeekDelayMultiplier);
}

}  // namespace driver_config
