#pragma once

#include <Arduino.h>

namespace driver_config {

constexpr float kRSense = 0.11f;
constexpr uint8_t kDriverAddress = 0b00;

constexpr uint16_t kDefaultCurrentMa = 160;
constexpr uint16_t kDefaultMicrosteps = 1;
constexpr uint32_t kDefaultStepDelayUs = 1000;
constexpr long kDefaultMoveSteps = 1000;

// Keep UART optional so STEP/DIR motion can be brought up before UART wiring is finalized.
constexpr bool kDriverUartEnabled = true;
constexpr unsigned long kDriverUartBaud = 115200UL;

}  // namespace driver_config
