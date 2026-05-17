#pragma once

#include <Arduino.h>
#include "GeneratedBoardPins.h"

namespace board {

constexpr unsigned long kUsbSerialBaud = 115200UL;
constexpr uint8_t kEnablePin = generated_board_pins::kEnablePin;
constexpr uint8_t kStepPin = generated_board_pins::kStepPin;
constexpr uint8_t kDirPin = generated_board_pins::kDirPin;
constexpr uint8_t kEndstopPin = generated_board_pins::kEndstopPin;
constexpr uint8_t kTmcUartPin = generated_board_pins::kTmcUartPin;

constexpr bool kEnableActiveLow = true;
constexpr bool kInvertDirection = true;
constexpr bool kEndstopActiveHigh = false;

}  // namespace board
