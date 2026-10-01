#include "TtProtocol.h"
#include <string.h>

// 铁塔 TT「轻风」BLE 透传协议：BLE 只传输串口字节，真正协议为 Modbus RTU + 0xEF 扩展帧。
// 依据：铁塔 TT 协议与 BLE 连接规格书 V1.0（2026-09-23）。

uint16_t TtProtocol::crc16(const uint8_t* p, size_t n) {
  uint16_t c = 0xFFFF;
  for (size_t i = 0; i < n; ++i) {
    c ^= p[i];
    for (uint8_t b = 0; b < 8; ++b)
      c = (c & 1) ? uint16_t((c >> 1) ^ 0xA001) : uint16_t(c >> 1);
  }
  return c;
}

uint16_t TtProtocol::be16(const uint8_t* p) {
  return uint16_t(uint16_t(p[0]) << 8 | p[1]);
}

uint32_t TtProtocol::be32(const uint8_t* p) {
  return (uint32_t(be16(p)) << 16) | be16(p + 2);
}

int16_t TtProtocol::decodeCurrent(uint16_t raw) {
  if (raw < 50000) return int16_t(raw);
  return int16_t(int32_t(raw) - 65536);
}

int TtProtocol::decodeTemp(uint16_t raw) {
  // TT 官方口径：0..100 为正温；101..200 表示负温。
  if (raw <= 200) {
    return raw > 100 ? 100 - int(raw) : int(raw);
  }

  // 兼容少量补码式温度设备。
  int16_t signedRaw = int16_t(raw);
  if (signedRaw >= -100 && signedRaw <= 100) return signedRaw;
  return -128;
}

uint16_t TtProtocol::efSum(const uint8_t* p, size_t n) {
  if (!p || n < 7) return 0;
  uint32_t sum = 0;
  // SUM = 0x10000 - Σ(bytes[1 .. len-4])
  for (size_t i = 1; i + 3 < n; ++i) sum += p[i];
  return uint16_t(0x10000UL - (sum & 0xFFFFUL));
}

bool TtProtocol::canHandle(const uint8_t* d, size_t n) const {
  if (!d || n < 2) return false;

  if (d[0] == 0xEF) return true;

  return d[0] == 0x01 &&
         (d[1] == 0x03 || d[1] == 0x01 || (d[1] & 0x80));
}

int TtProtocol::findFrameStart(const uint8_t* d, size_t n) const {
  if (!d || n < 2) return -1;

  for (size_t i = 0; i + 1 < n; ++i) {
    // Modbus RTU：地址 01 + 03/01/异常功能码
    if (d[i] == 0x01 &&
        (d[i + 1] == 0x03 || d[i + 1] == 0x01 || (d[i + 1] & 0x80))) {
      if (i + 2 < n) {
        if (d[i + 1] & 0x80) expectedLen_ = 5;
        else if (d[i + 2] <= 64) expectedLen_ = size_t(5 + d[i + 2]);
        else continue;
      }
      return int(i);
    }

    // 0xEF 私有帧：总长 = 7 + L，L 位于 f[3]
    if (d[i] == 0xEF) {
      if (i + 3 >= n) return int(i);
      const uint8_t L = d[i + 3];
      if (L > 216) continue; // 防止异常长度导致缓冲失控
      expectedLen_ = size_t(7 + L);
      return int(i);
    }
  }

  return -1;
}

bool TtProtocol::parseFrame(const uint8_t* d, size_t n, BmsData& o) {
  if (!d || n < 5) return false;

  if (d[0] == 0xEF) return parseEf(d, n, o);

  // Modbus 异常响应固定 5B；不更新 BMS 数据。
  if (d[1] & 0x80) {
    expectedLen_ = 5;
    return false;
  }

  if (d[2] > 64) return false;

  const size_t fl = size_t(5 + d[2]);
  if (n < fl) return false;

  const uint16_t got = uint16_t(d[fl - 2]) | (uint16_t(d[fl - 1]) << 8);
  if (crc16(d, fl - 2) != got) return false;

  expectedLen_ = fl;
  return parseModbus(d, fl, o);
}

bool TtProtocol::parseModbus(const uint8_t* f, size_t n, BmsData& o) {
  if (!f || n < 5) return false;

  // 0x18：24 字节 ASCII 设备 ID。
  if (f[1] == 0x03 && f[2] == 24 && n == 29) {
    char id[25];
    memcpy(id, f + 3, 24);
    id[24] = 0;
    for (int i = 23; i >= 0 && id[i] == ' '; --i) id[i] = 0;

    o.deviceName = String(id);
    o.online = true;
    o.updateMs = millis();
    return false;
  }

  // 0x07：告警/开关量。
  if (f[1] == 0x01 && f[2] == 7 && n == 12) {
    const uint32_t ov =
      ((uint32_t)(f[4] >> 4) |
       ((uint32_t)f[5] << 4) |
       ((uint32_t)f[6] << 12)) & 0xFFFFFUL;

    const uint32_t uv =
      ((uint32_t)f[7] |
       ((uint32_t)f[8] << 8) |
       ((uint32_t)(f[9] & 0x0F) << 16)) & 0xFFFFFUL;

    // 保留低位基础告警，并用 bit31 标记存在单体过压/欠压。
    o.errors = uint32_t(f[3] & 0xFC);
    o.errors |= uint32_t(f[4] & 0x0F) << 8;
    if (ov) o.errors |= 0x80000000UL;
    if (uv) o.errors |= 0x40000000UL;

    o.online = true;
    o.updateMs = millis();
    return false;
  }

  // 0x3A：29 个保持寄存器，共 63B。
  if (f[1] != 0x03 || f[2] != 58 || n != 63) return false;

  const uint16_t totalVoltageRaw = be16(f + 3);

  // 空板哨兵：总压 FFFD。
  if (totalVoltageRaw == 0xFFFD) {
    o.valid = false;
    o.online = true;
    o.updateMs = millis();
    return false;
  }

  uint8_t cells = uint8_t(be16(f + 5));
  if (cells > 20) cells = 20;

  o.totalVoltage = totalVoltageRaw * 0.01f;
  o.cellCount = cells;
  o.soc = float(be16(f + 7));
  if (o.soc > 100.0f) o.soc = 100.0f;

  o.remainingCapacityAh = be16(f + 9) * 0.01f;
  o.current = decodeCurrent(be16(f + 13)) * 0.01f;
  o.power = o.totalVoltage * o.current;

  const int envT = decodeTemp(be16(f + 15));
  const int cellT = decodeTemp(be16(f + 17));
  const int mosT = decodeTemp(be16(f + 19));

  o.temperature1 = envT == -128 ? 0.0f : float(envT);
  o.temperature2 = cellT == -128 ? 0.0f : float(cellT);
  o.mosTemperature = mosT == -128 ? 0.0f : float(mosT);

  float minV = 100.0f;
  float maxV = 0.0f;
  uint8_t minC = 0;
  uint8_t maxC = 0;

  for (uint8_t i = 0; i < JK_MAX_CELLS; ++i) o.cellVoltage[i] = 0.0f;

  for (uint8_t i = 0; i < cells; ++i) {
    const float v = be16(f + 21 + i * 2) * 0.001f;
    o.cellVoltage[i] = v;

    if (v >= 0.5f && v <= 6.0f) {
      if (v < minV) {
        minV = v;
        minC = i + 1;
      }
      if (v > maxV) {
        maxV = v;
        maxC = i + 1;
      }
    }
  }

  o.minCellVoltage = minV < 100.0f ? minV : 0.0f;
  o.maxCellVoltage = maxV;
  o.deltaCellVoltage =
    (maxV > 0.0f && minV < 100.0f) ? maxV - minV : 0.0f;
  o.minCell = minC;
  o.maxCell = maxC;

  if (o.energyConsumptionWhKm > 1.0f) {
    o.remainingRangeKm =
      (o.remainingCapacityAh * o.totalVoltage) / o.energyConsumptionWhKm;
  } else {
    o.remainingRangeKm = 0.0f;
  }

  o.online = true;
  o.valid = true;
  o.updateMs = millis();
  return true;
}

bool TtProtocol::parseEf(const uint8_t* f, size_t n, BmsData& o) {
  if (!f || n < 7 || f[0] != 0xEF) return false;
  if (f[6 + f[3]] != 0x16) return false;

  const size_t fl = size_t(7 + f[3]);
  if (n < fl) return false;

  const uint16_t got =
    (uint16_t(f[fl - 3]) << 8) | uint16_t(f[fl - 2]);

  if (efSum(f, fl) != got) return false;

  expectedLen_ = fl;

  // 0xCA：累计充/放电量。
  // 当前 BmsData 没有累计电量字段，因此只确认帧有效，不虚构字段。
  if (f[2] == 0xCA && f[3] >= 9) {
    o.online = true;
    o.updateMs = millis();
    return false;
  }

  // 0x83 参数块同样只做完整性校验；参数字段可继续扩展到配置页面。
  if (f[2] == 0x83) {
    o.online = true;
    o.updateMs = millis();
    return false;
  }

  return false;
}

bool TtProtocol::buildCommand(uint8_t cmd, uint8_t, uint8_t out[20]) {
  if (!out) return false;
  memset(out, 0, 20);

  uint8_t q[8];

  if (cmd == 0x03) {
    const uint8_t t[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x1D};
    memcpy(q, t, 6);
  } else if (cmd == 0x18) {
    const uint8_t t[] = {0x01, 0x03, 0x03, 0xE8, 0x00, 0x0C};
    memcpy(q, t, 6);
  } else if (cmd == 0x01) {
    const uint8_t t[] = {0x01, 0x01, 0x00, 0x00, 0x00, 0x34};
    memcpy(q, t, 6);
  } else if (cmd == 0xCA) {
    // 读累计充/放电量：EF 01 CA 00 FF 35 16。
    const uint8_t t[] = {0xEF, 0x01, 0xCA, 0x00, 0xFF, 0x35, 0x16};
    memcpy(out, t, sizeof(t));
    expectedLen_ = 16;
    return true;
  } else {
    return false;
  }

  const uint16_t c = crc16(q, 6);
  memcpy(out, q, 6);
  out[6] = uint8_t(c & 0xFF);
  out[7] = uint8_t(c >> 8);

  if (cmd == 0x03) expectedLen_ = 63;
  else if (cmd == 0x18) expectedLen_ = 29;
  else if (cmd == 0x01) expectedLen_ = 12;

  return true;
}

size_t TtProtocol::commandLength(uint8_t cmd) const {
  if (cmd == 0x03 || cmd == 0x18 || cmd == 0x01) return 8;
  if (cmd == 0xCA) return 7;
  return 0;
}
