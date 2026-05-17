#include "Tmc2209Driver.h"

Tmc2209Driver::Tmc2209Driver()
    : driver_(board::kTmcUartPin, board::kTmcUartPin, driver_config::kRSense,
              driver_config::kDriverAddress),
      enabled_(driver_config::kDriverUartEnabled),
      connected_(false),
      lastMicrostepStatus_(MicrostepStatus::kUnavailable) {}

bool Tmc2209Driver::begin(uint16_t runCurrentMa, uint16_t microsteps) {
  if (!enabled_) {
    connected_ = false;
    lastMicrostepStatus_ = MicrostepStatus::kUnavailable;
    return false;
  }

  driver_.beginSerial(driver_config::kDriverUartBaud);
  delay(50);

  driver_.pdn_disable(true);
  driver_.mstep_reg_select(true);
  delay(50);

  const uint8_t result = driver_.test_connection();
  connected_ = (result == 0);

  if (!connected_) {
    lastMicrostepStatus_ = MicrostepStatus::kUnavailable;
    return false;
  }

  driver_.pdn_disable(true);
  driver_.mstep_reg_select(true);
  driver_.I_scale_analog(false);
  driver_.toff(4);
  driver_.blank_time(24);
  driver_.en_spreadCycle(true);
  driver_.rms_current(runCurrentMa);
  driver_.ihold(1);
  driver_.iholddelay(1);
  driver_.TPOWERDOWN(2);

  lastMicrostepStatus_ = setMicrosteps(microsteps);
  return lastMicrostepStatus_ == MicrostepStatus::kOk;
}

bool Tmc2209Driver::isEnabled() const { return enabled_; }

bool Tmc2209Driver::isConnected() const { return connected_; }

bool Tmc2209Driver::setRunCurrent(uint16_t runCurrentMa) {
  if (!connected_) {
    return false;
  }

  driver_.rms_current(runCurrentMa);
  return true;
}

Tmc2209Driver::MicrostepStatus Tmc2209Driver::setMicrosteps(
    uint16_t microsteps) {
  const uint8_t mres = microstepsToMres(microsteps);
  if (mres == 255) {
    lastMicrostepStatus_ = MicrostepStatus::kInvalidMicrostepValue;
    return lastMicrostepStatus_;
  }

  if (!connected_) {
    lastMicrostepStatus_ = MicrostepStatus::kUnavailable;
    return lastMicrostepStatus_;
  }

  driver_.mstep_reg_select(true);
  driver_.intpol(false);
  driver_.microsteps(microsteps);
  delay(5);

  uint32_t chop = driver_.CHOPCONF();
  chop &= ~(0x0FUL << 24);
  chop |= (static_cast<uint32_t>(mres) << 24);
  driver_.CHOPCONF(chop);
  delay(10);

  lastMicrostepStatus_ = getRealMicrosteps() == microsteps
                             ? MicrostepStatus::kOk
                             : MicrostepStatus::kWriteFailed;
  return lastMicrostepStatus_;
}

Tmc2209Driver::MicrostepStatus Tmc2209Driver::lastMicrostepStatus() const {
  return lastMicrostepStatus_;
}

uint16_t Tmc2209Driver::getRealMicrosteps() {
  if (!connected_) {
    return 0;
  }

  const uint32_t chop = driver_.CHOPCONF();
  const uint8_t mres = (chop >> 24) & 0x0F;
  return mresToMicrosteps(mres);
}

uint8_t Tmc2209Driver::testConnection() {
  if (!connected_) {
    return 255;
  }

  return driver_.test_connection();
}

uint32_t Tmc2209Driver::drvStatus() {
  return connected_ ? driver_.DRV_STATUS() : 0;
}

uint16_t Tmc2209Driver::rmsCurrent() {
  return connected_ ? driver_.rms_current() : 0;
}

uint8_t Tmc2209Driver::toff() { return connected_ ? driver_.toff() : 0; }

bool Tmc2209Driver::overtemp() {
  return connected_ ? driver_.ot() : false;
}

bool Tmc2209Driver::standstill() {
  return connected_ ? driver_.stst() : false;
}

uint16_t Tmc2209Driver::microstepCounter() {
  return connected_ ? driver_.MSCNT() : 0;
}

uint32_t Tmc2209Driver::chopconf() {
  return connected_ ? driver_.CHOPCONF() : 0;
}

uint16_t Tmc2209Driver::libraryMicrosteps() {
  return connected_ ? driver_.microsteps() : 0;
}

uint8_t Tmc2209Driver::microstepsToMres(uint16_t microsteps) {
  switch (microsteps) {
    case 256:
      return 0;
    case 128:
      return 1;
    case 64:
      return 2;
    case 32:
      return 3;
    case 16:
      return 4;
    case 8:
      return 5;
    case 4:
      return 6;
    case 2:
      return 7;
    case 1:
      return 8;
    default:
      return 255;
  }
}

uint16_t Tmc2209Driver::mresToMicrosteps(uint8_t mres) {
  if (mres <= 8) {
    return 256 >> mres;
  }

  return 0;
}

