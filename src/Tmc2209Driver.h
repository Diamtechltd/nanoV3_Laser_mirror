#pragma once

#include <Arduino.h>
#include <new>
#include <TMCStepper.h>

#include "BoardConfig.h"
#include "DriverConfig.h"

class Tmc2209Driver {
 public:
  enum class MicrostepStatus : uint8_t {
    kOk = 0,
    kInvalidMicrostepValue,
    kUnavailable,
    kWriteFailed,
  };

  Tmc2209Driver();

  bool configure(uint8_t uartPin, uint8_t driverAddress);

  bool begin(uint16_t runCurrentMa, uint16_t microsteps);
  bool isEnabled() const;
  bool isConnected() const;
  uint8_t refreshConnection();
  bool setEnabledState(bool enable);
  bool enabledState() const;

  bool setRunCurrent(uint16_t runCurrentMa);
  MicrostepStatus setMicrosteps(uint16_t microsteps);
  MicrostepStatus lastMicrostepStatus() const;

  uint16_t getRealMicrosteps();
  uint8_t testConnection();
  uint32_t drvStatus();
  uint16_t rmsCurrent();
  uint8_t toff();
  bool overtemp();
  bool standstill();
  uint16_t microstepCounter();
  uint32_t chopconf();
  uint16_t libraryMicrosteps();

 private:
  static uint8_t microstepsToMres(uint16_t microsteps);
  static uint16_t mresToMicrosteps(uint8_t mres);

  TMC2209Stepper* driver_;
  uint8_t storage_[sizeof(TMC2209Stepper)];
  uint8_t uartPin_;
  uint8_t driverAddress_;
  bool hasDriver_;
  bool enabled_;
  bool connected_;
  bool enabledState_;
  MicrostepStatus lastMicrostepStatus_;
};

