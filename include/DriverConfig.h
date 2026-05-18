#pragma once

#include <Arduino.h>
#include "GeneratedBuildConfig.h"

namespace driver_config {

constexpr float kRSense = 0.11f;
constexpr uint8_t kDriverAddress = 0b00;

constexpr uint32_t kStepsPerMmX1000 = generated_build_config::kStepsPerMmX1000;
constexpr uint32_t kDerivedStepsPerMmX1000 =
    generated_build_config::kDerivedStepsPerMmX1000;
constexpr int32_t kFullStrokeMilliMm = generated_build_config::kFullStrokeMilliMm;
constexpr uint16_t kFullStrokeSteps1x = generated_build_config::kFullStrokeSteps1x;
constexpr uint16_t kDefaultCurrentMa = generated_build_config::kDefaultCurrentMa;
constexpr uint16_t kDefaultMicrosteps = generated_build_config::kDefaultMicrosteps;
constexpr long kDefaultMoveSteps = generated_build_config::kDefaultMoveSteps;
constexpr uint32_t kDirectionSetupDelayUs = 200;
constexpr unsigned long kMaxMoveDurationMs = 30000UL;
constexpr unsigned long kMotionStatusIntervalMs = 0UL;
constexpr bool kEndstopEnabled = generated_build_config::kEndstopEnabled;
constexpr int32_t kMinimumPositionMilliMm =
    generated_build_config::kMinimumPositionMilliMm;
constexpr int32_t kMaximumPositionMilliMm =
    generated_build_config::kMaximumPositionMilliMm;
constexpr uint16_t kMaximumSpeedMmPerSec =
    generated_build_config::kMaximumSpeedMmPerSec;
constexpr int32_t kApertureIrisMinMilliMm =
    generated_build_config::kApertureIrisMinMilliMm;
constexpr int32_t kApertureIrisMaxMilliMm =
    generated_build_config::kApertureIrisMaxMilliMm;
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
  return kMinimumPositionMilliMm;
}

inline int32_t maximumPositionMilliMm() {
  return kMaximumPositionMilliMm;
}

inline uint32_t strokeMilliMm() {
  return static_cast<uint32_t>(maximumPositionMilliMm() - minimumPositionMilliMm());
}

inline int32_t fullStrokeMilliMm() {
  return kFullStrokeMilliMm;
}

inline int32_t apertureIrisMinimumMilliMm() {
  return kApertureIrisMinMilliMm;
}

inline int32_t apertureIrisMaximumMilliMm() {
  return kApertureIrisMaxMilliMm;
}

inline uint32_t apertureIrisStrokeMilliMm() {
  return static_cast<uint32_t>(apertureIrisMaximumMilliMm() -
                               apertureIrisMinimumMilliMm());
}

inline uint32_t activeStepsPerStroke(uint16_t microsteps) {
  return static_cast<uint32_t>(kFullStrokeSteps1x) * static_cast<uint32_t>(microsteps);
}

inline uint32_t activeStepsPerMmX1000(uint16_t microsteps) {
  return kStepsPerMmX1000 * static_cast<uint32_t>(microsteps);
}

inline uint32_t speedLimitedStepDelayUsFor(uint16_t microsteps) {
  const uint64_t denominator =
      2ULL * activeStepsPerMmX1000(microsteps) *
      static_cast<uint32_t>(kMaximumSpeedMmPerSec);
  const uint64_t numerator = 1000000000ULL;
  return static_cast<uint32_t>((numerator + denominator - 1ULL) / denominator);
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

  const uint64_t numerator = 1000000000000ULL;
  const uint64_t denominator =
      2ULL * static_cast<uint64_t>(delayUs) * activeStepsPerMmX1000(microsteps);
  return static_cast<uint32_t>(numerator / denominator);
}

inline uint32_t homingStepDelayUsFor(uint16_t microsteps) {
  return effectiveNormalStepDelayUsFor(microsteps) * 2UL;
}

inline uint32_t secondSeekHomingStepDelayUsFor(uint16_t microsteps) {
  return homingStepDelayUsFor(microsteps) *
         static_cast<uint32_t>(kHomingSecondSeekDelayMultiplier);
}

}  // namespace driver_config
