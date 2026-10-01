#pragma once
/*
 * JK_BMS_TO_WEB-ESP32 协议参考
 * 来源：https://github.com/zhang1997aaa/JK_BMS_TO_WEB-ESP32
 *
 * 用于 Arduino V6 的 JK 传统 BLE 55 AA EB 90 协议移植参考。
 * 该项目的 files.ino 实际使用：
 *   Service  FFE0
 *   Write    FFE1
 *   Notify   FFE1
 *
 * 关键实时帧字段（参考项目 receivedBytes）：
 *   0x06..0x25  32节单体电压，0.001V，低字节在前
 *   0x4A..0x4B  平均单体电压，0.001V
 *   0x4C..0x4D  单体压差，0.001V
 *   0x51..0x6F  32路线阻，0.001（参考项目）
 *   0x90..0x91  MOS温度，0.1℃
 *   0x96..0x99  总电压，0.001V
 *   0x9E..0xA1  电流，0.001A
 *   0xA2..0xA3  T1，0.1℃
 *   0xA4..0xA5  T2，0.1℃
 *   0xAA..0xAB  均衡电流，0.001A
 *   0xAC        均衡动作
 *   0xAD        SOC，%
 *   0xAE..0xB1  剩余容量，0.001Ah
 *   0xB2..0xB5  标称容量，0.001Ah
 *   0xB6..0xB9  循环次数
 *   0xBA..0xBD  循环容量，0.001Ah
 *   0xC2..0xC4  运行时间
 *   0xC6        充电状态
 *   0xC7        放电状态
 *   0xC9        均衡状态
 *
 * 写寄存器：
 *   AA 55 90 EB address length payload(小端) ... checksum
 *   参考项目使用20字节写帧，最后1字节为前19字节累加和低8位。
 *
 * 注意：
 *   本文件是协议证据/移植参考，不依赖 WebServer/WiFi。
 *   V6 的实际解析仍由 JkProtocol/BmsProtocolManager 负责，避免
 *   将原项目的 Web 控制逻辑直接带入 BMS 显示工程。
 */
namespace jk_bms_to_web_reference {
static constexpr uint8_t SERVICE_UUID16 = 0xE0;
static constexpr uint8_t WRITE_NOTIFY_UUID16 = 0xE1;
static constexpr uint8_t FRAME_HEADER[4] = {0x55, 0xAA, 0xEB, 0x90};

inline uint16_t u16_le(const uint8_t *p) {
  return static_cast<uint16_t>(p[0]) |
         (static_cast<uint16_t>(p[1]) << 8);
}
inline uint32_t u32_le(const uint8_t *p) {
  return static_cast<uint32_t>(p[0]) |
         (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}
inline uint8_t checksum8(const uint8_t *data, size_t len) {
  uint8_t sum = 0;
  for (size_t i = 0; i < len; ++i) sum = static_cast<uint8_t>(sum + data[i]);
  return sum;
}
}
