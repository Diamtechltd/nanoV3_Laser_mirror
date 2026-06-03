#include "PersistentConfig.h"

#include <EEPROM.h>
#include <string.h>

namespace persistent_config {
namespace {

constexpr uint32_t kRecordMagic = 0x41505452UL;  // "APTR"
constexpr uint16_t kRecordVersion = 3;

struct __attribute__((packed)) StoredRuntimeConfigPayload {
  uint8_t endstopEnabled = 0;
  uint8_t debugMode = 0;
  uint8_t autoDisableAfterMove = 0;
  uint16_t runCurrentMa = 0;
  uint16_t microsteps = 0;
  uint32_t stepDelayOverrideUs = 0;
  int32_t apertureIrisMinMilliMm = 0;
  int32_t apertureIrisMaxMilliMm = 0;
  char arduinoName[kArduinoNameCapacity] = {};
};

struct __attribute__((packed)) StoredRuntimeConfigRecord {
  uint32_t magic = 0;
  uint16_t version = 0;
  StoredRuntimeConfigPayload payload{};
  uint16_t crc16 = 0;
};

uint16_t crc16Ccitt(const uint8_t* data, size_t length) {
  uint16_t crc = 0xFFFF;
  for (size_t index = 0; index < length; ++index) {
    crc ^= static_cast<uint16_t>(data[index]) << 8;
    for (uint8_t bit = 0; bit < 8; ++bit) {
      if ((crc & 0x8000U) != 0U) {
        crc = static_cast<uint16_t>((crc << 1U) ^ 0x1021U);
      } else {
        crc = static_cast<uint16_t>(crc << 1U);
      }
    }
  }
  return crc;
}

uint16_t computeRecordCrc16(const StoredRuntimeConfigRecord& record) {
  return crc16Ccitt(reinterpret_cast<const uint8_t*>(&record),
                    sizeof(record) - sizeof(record.crc16));
}

bool recordStorageFits() {
  return storageSizeBytes() <= static_cast<size_t>(EEPROM.length());
}

bool isBlankRecord(const StoredRuntimeConfigRecord& record) {
  const uint8_t* data = reinterpret_cast<const uint8_t*>(&record);
  bool allZero = true;
  bool allFF = true;
  for (size_t index = 0; index < sizeof(record); ++index) {
    allZero = allZero && data[index] == 0x00U;
    allFF = allFF && data[index] == 0xFFU;
  }
  return allZero || allFF;
}

size_t boundedStringLength(const char* text, size_t capacity) {
  size_t length = 0;
  while (length < capacity && text[length] != '\0') {
    ++length;
  }
  return length;
}

RuntimeConfig runtimeConfigFromPayload(const StoredRuntimeConfigPayload& payload) {
  RuntimeConfig config{};
  config.endstopEnabled = payload.endstopEnabled != 0;
  config.debugMode = payload.debugMode != 0;
  config.autoDisableAfterMove = payload.autoDisableAfterMove != 0;
  config.runCurrentMa = payload.runCurrentMa;
  config.microsteps = payload.microsteps;
  config.stepDelayOverrideUs = payload.stepDelayOverrideUs;
  config.apertureIrisMinMilliMm = payload.apertureIrisMinMilliMm;
  config.apertureIrisMaxMilliMm = payload.apertureIrisMaxMilliMm;
  memcpy(config.arduinoName, payload.arduinoName, sizeof(config.arduinoName));
  return config;
}

StoredRuntimeConfigPayload payloadFromRuntimeConfig(const RuntimeConfig& config) {
  StoredRuntimeConfigPayload payload{};
  payload.endstopEnabled = config.endstopEnabled ? 1U : 0U;
  payload.debugMode = config.debugMode ? 1U : 0U;
  payload.autoDisableAfterMove = config.autoDisableAfterMove ? 1U : 0U;
  payload.runCurrentMa = config.runCurrentMa;
  payload.microsteps = config.microsteps;
  payload.stepDelayOverrideUs = config.stepDelayOverrideUs;
  payload.apertureIrisMinMilliMm = config.apertureIrisMinMilliMm;
  payload.apertureIrisMaxMilliMm = config.apertureIrisMaxMilliMm;
  memcpy(payload.arduinoName, config.arduinoName, sizeof(payload.arduinoName));
  return payload;
}

bool areFlagBytesValid(const StoredRuntimeConfigPayload& payload) {
  return payload.endstopEnabled <= 1U && payload.debugMode <= 1U &&
         payload.autoDisableAfterMove <= 1U;
}

}  // namespace

bool runtimeConfigsEqual(const RuntimeConfig& left, const RuntimeConfig& right) {
  return left.endstopEnabled == right.endstopEnabled &&
         left.debugMode == right.debugMode &&
         left.autoDisableAfterMove == right.autoDisableAfterMove &&
         left.runCurrentMa == right.runCurrentMa &&
         left.microsteps == right.microsteps &&
         left.stepDelayOverrideUs == right.stepDelayOverrideUs &&
         left.apertureIrisMinMilliMm == right.apertureIrisMinMilliMm &&
         left.apertureIrisMaxMilliMm == right.apertureIrisMaxMilliMm &&
         strcmp(left.arduinoName, right.arduinoName) == 0;
}

bool isSupportedMicrosteps(uint16_t microsteps) {
  switch (microsteps) {
    case 1:
    case 2:
    case 4:
    case 8:
    case 16:
    case 32:
    case 64:
    case 128:
    case 256:
      return true;
    default:
      return false;
  }
}

bool isValidRunCurrentMa(uint16_t runCurrentMa) {
  return runCurrentMa >= 1U && runCurrentMa <= 500U;
}

bool isValidStepDelayOverrideUs(uint32_t stepDelayOverrideUs) {
  return stepDelayOverrideUs == kNoStepDelayOverrideUs ||
         (stepDelayOverrideUs >= 5UL && stepDelayOverrideUs <= 100000UL);
}

bool isValidApertureIrisBounds(int32_t apertureIrisMinMilliMm,
                               int32_t apertureIrisMaxMilliMm) {
  return apertureIrisMinMilliMm < apertureIrisMaxMilliMm;
}

bool isValidArduinoName(const char* arduinoName) {
  const size_t length = boundedStringLength(arduinoName, kArduinoNameCapacity);
  return length > 0 && length <= kMaxArduinoNameLength &&
         arduinoName[length] == '\0';
}

bool isValidRuntimeConfig(const RuntimeConfig& config) {
  return isValidRunCurrentMa(config.runCurrentMa) &&
         isSupportedMicrosteps(config.microsteps) &&
         isValidStepDelayOverrideUs(config.stepDelayOverrideUs) &&
         isValidApertureIrisBounds(config.apertureIrisMinMilliMm,
                                   config.apertureIrisMaxMilliMm) &&
         isValidArduinoName(config.arduinoName);
}

LoadResult loadRuntimeConfig() {
  LoadResult result{};
  if (!recordStorageFits()) {
    result.status = LoadStatus::kStorageTooSmall;
    return result;
  }

  StoredRuntimeConfigRecord record{};
  EEPROM.get(0, record);

  if (isBlankRecord(record)) {
    result.status = LoadStatus::kEmpty;
    return result;
  }

  if (record.magic != kRecordMagic) {
    result.status = LoadStatus::kInvalidMagic;
    return result;
  }

  if (record.version != kRecordVersion) {
    result.status = LoadStatus::kVersionMismatch;
    return result;
  }

  if (record.crc16 != computeRecordCrc16(record)) {
    result.status = LoadStatus::kCrcMismatch;
    return result;
  }

  if (!areFlagBytesValid(record.payload)) {
    result.status = LoadStatus::kValueOutOfRange;
    return result;
  }

  result.config = runtimeConfigFromPayload(record.payload);
  if (!isValidRuntimeConfig(result.config)) {
    result.status = LoadStatus::kValueOutOfRange;
    return result;
  }

  result.status = LoadStatus::kLoaded;
  return result;
}

bool saveRuntimeConfig(const RuntimeConfig& config) {
  if (!recordStorageFits() || !isValidRuntimeConfig(config)) {
    return false;
  }

  StoredRuntimeConfigRecord record{};
  record.magic = kRecordMagic;
  record.version = kRecordVersion;
  record.payload = payloadFromRuntimeConfig(config);
  record.crc16 = computeRecordCrc16(record);

  const uint8_t* data = reinterpret_cast<const uint8_t*>(&record);
  for (size_t index = 0; index < sizeof(record); ++index) {
    EEPROM.update(static_cast<int>(index), data[index]);
  }
  return true;
}

size_t storageSizeBytes() { return sizeof(StoredRuntimeConfigRecord); }

}  // namespace persistent_config
