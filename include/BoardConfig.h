#pragma once

#include <Arduino.h>
#include "GeneratedBoardPins.h"

namespace board {

constexpr unsigned long kUsbSerialBaud = 115200UL;
constexpr uint8_t kAxisCount = generated_board_pins::kAxisCount;
constexpr uint8_t axisStepPin(uint8_t axis) {
	return generated_board_pins::kAxisStepPins[axis];
}
constexpr uint8_t axisDirPin(uint8_t axis) {
	return generated_board_pins::kAxisDirPins[axis];
}
constexpr uint8_t axisTmcUartPin(uint8_t axis) {
	return generated_board_pins::kAxisTmcUartPins[axis];
}
constexpr uint8_t axisTmcDriverAddress(uint8_t axis) {
	return generated_board_pins::kAxisTmcDriverAddresses[axis];
}

// Backward-compatible aliases for axis 0.
constexpr uint8_t kStepPin = generated_board_pins::kAxisStepPins[0];
constexpr uint8_t kDirPin = generated_board_pins::kAxisDirPins[0];
constexpr uint8_t kEndstopPin = generated_board_pins::kEndstopPin;
constexpr uint8_t kTmcUartPin = generated_board_pins::kAxisTmcUartPins[0];
constexpr uint8_t kTmcDriverAddress = generated_board_pins::kAxisTmcDriverAddresses[0];

constexpr bool kInvertDirection = true;
constexpr bool kEndstopActiveHigh = false;

}  // namespace board
