#pragma once

#include <Arduino.h>

namespace driver_config {

constexpr float kRSense = 0.11f;
constexpr uint8_t kDriverAddress = 0b00;

constexpr uint16_t kDefaultCurrentMa = 160;
constexpr uint16_t kDefaultMicrosteps = 1;
constexpr uint32_t kDefaultStepDelayUs = 1000;
constexpr long kDefaultMoveSteps = 1000;
constexpr uint32_t kDirectionSetupDelayUs = 200;
constexpr unsigned long kMaxMoveDurationMs = 30000UL;
constexpr unsigned long kMotionStatusIntervalMs = 0UL;
constexpr bool kHomingEnabled = false;
constexpr uint32_t kHomingStepDelayUs = 5000;
constexpr uint16_t kHomingRetractSteps = 20;
constexpr bool kHomingDirectionNegative = true;

// Keep UART optional so STEP/DIR motion can be brought up before UART wiring is finalized.
constexpr bool kDriverUartEnabled = true;
constexpr unsigned long kDriverUartBaud = 57600UL;

}  // namespace driver_config
