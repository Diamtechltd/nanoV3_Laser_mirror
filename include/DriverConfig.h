#pragma once

#include <Arduino.h>
#include "GeneratedBuildConfig.h"

namespace driver_config {

constexpr float kRSense = 0.11f;
constexpr uint8_t kDriverAddress = 0b00;

constexpr uint16_t kDefaultCurrentMa = generated_build_config::kDefaultCurrentMa;
constexpr uint16_t kDefaultMicrosteps = generated_build_config::kDefaultMicrosteps;
constexpr uint32_t kDefaultStepDelayUs = generated_build_config::kDefaultStepDelayUs;
constexpr long kDefaultMoveSteps = generated_build_config::kDefaultMoveSteps;
constexpr uint32_t kDirectionSetupDelayUs = 200;
constexpr unsigned long kMaxMoveDurationMs = 30000UL;
constexpr unsigned long kMotionStatusIntervalMs = 0UL;
constexpr bool kAutoDisableAfterMove = generated_build_config::kAutoDisableAfterMove;
constexpr bool kHomingEnabled = generated_build_config::kHomingEnabled;
constexpr uint32_t kHomingStepDelayUs = generated_build_config::kHomingStepDelayUs;
constexpr uint16_t kHomingRetractSteps = generated_build_config::kHomingRetractSteps;
constexpr bool kHomingDirectionNegative = generated_build_config::kHomingDirectionNegative;

// Keep UART optional so STEP/DIR motion can be brought up before UART wiring is finalized.
constexpr bool kDriverUartEnabled = generated_build_config::kDriverUartEnabled;
constexpr unsigned long kDriverUartBaud = generated_build_config::kDriverUartBaud;

}  // namespace driver_config
