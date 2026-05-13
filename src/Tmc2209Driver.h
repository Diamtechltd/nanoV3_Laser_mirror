#pragma once

#include <Arduino.h>
#include <SoftwareSerial.h>
#include <TMCStepper.h>

#include "BoardConfig.h"
#include "DriverConfig.h"

class Tmc2209Driver {
 public:
  Tmc2209Driver();

  bool begin(uint16_t runCurrentMa, uint16_t microsteps);
  bool isEnabled() const;
  bool isConnected() const;

  bool setRunCurrent(uint16_t runCurrentMa);
  bool setMicrosteps(uint16_t microsteps);

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

  SoftwareSerial serial_;
  TMC2209Stepper driver_;
  bool enabled_;
  bool connected_;
};

