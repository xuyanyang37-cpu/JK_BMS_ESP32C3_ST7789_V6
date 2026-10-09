#pragma once
#include <Arduino.h>
#include "../BmsData.h"

// 彦阳保护板 Modbus RTU over BLE transparent bridge decoder.
// Reads holding registers 75..166; request/response CRC uses Modbus CRC-16.
class YanyangProtocolDecoder {
public:
  using StatusCallback = void (*)(const BmsData& data);
  using DeviceInfoCallback = void (*)(const char* hardwareVersion, const char* softwareVersion);
  YanyangProtocolDecoder(StatusCallback statusCallback, DeviceInfoCallback deviceInfoCallback);
  void reset();
  void feed(const uint8_t* data, size_t length);
  static void buildStatusRequest(uint8_t slaveAddress, uint8_t output[8]);
  static uint16_t crc16(const uint8_t* data, size_t length);
private:
  bool processBufferedFrames();
  bool parseStatusResponse(const uint8_t* frame, size_t length);
  void updateCellStatistics(BmsData& data) const;
  const StatusCallback statusCallback_;
  const DeviceInfoCallback deviceInfoCallback_;
  uint8_t frameBuffer_[256] = {};
  size_t frameLength_ = 0;
  uint32_t lastChunkAt_ = 0;
  bool infoPublished_ = false;
};
