#pragma once

// ESPHome jk_bms_ble JkBmsBle 接口参考
// 来源：用户提供的 JkBmsBle.h。
// 本文件仅保存接口参考，不直接参与 Arduino 编译。
// 用于后续将 JK04 / JK02_24S / JK02_32S 的解析、assemble()、
// build_frame()、write_register() 等能力移植到 Arduino V6。

#include <array>
#include <cstddef>
#include <cstdint>

namespace jk_bms_reference {

enum ProtocolVersion {
  PROTOCOL_VERSION_JK04,
  PROTOCOL_VERSION_JK02_24S,
  PROTOCOL_VERSION_JK02_32S,
};

struct LookupTable {
  const char *const *entries{nullptr};
  size_t count{0};
  const char *get(uint8_t index) const {
    return (entries != nullptr && index < count) ? entries[index] : nullptr;
  }
};

// 用户提供的 ESPHome JkBmsBle 接口中与 Arduino 移植直接相关的核心定义。
class JkBmsBleReference {
 public:
  void assemble(const uint8_t *data, uint16_t length);
  void set_protocol_version(ProtocolVersion protocol_version) {
    protocol_version_ = protocol_version;
  }
  ProtocolVersion get_protocol_version() const { return protocol_version_; }

  static std::array<uint8_t, 20> build_frame(uint8_t address,
                                             uint32_t value,
                                             uint8_t length);
  static uint32_t encode_jk04_payload(float value, uint8_t len, float factor);

 private:
  ProtocolVersion protocol_version_{PROTOCOL_VERSION_JK02_24S};
};

}  // namespace jk_bms_reference
