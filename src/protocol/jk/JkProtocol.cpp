/*
 * ================================================================
 * JkProtocol.cpp - JK协议解析器
 *
 * 业务入口：
 *   BmsProtocolManager::parseFrame()
 *             ↓
 *   JkProtocol::parseFrame()
 *             ↓
 *      4E57 ? ──是──> parseNewTlvFrame()
 *       │
 *       否
 *       ↓
 *   55 AA EB 90 ──> parseOldFrame()
 *
 * 这里是“协议知识最集中的地方”。
 * 修改JK数据位置、Tag含义、缩放比例，应优先在这里处理。
 * ================================================================
 */

#include "JkProtocol.h"
#include <string.h>

JkProtocol::JkProtocol() : protocol32S_(true) {}

const char* JkProtocol::name() const {
  return protocol32S_ ? Jk02_32S::name() : Jk02_24S::name();
}

// [识别] 判断这是不是JK帧。
bool JkProtocol::canHandle(const uint8_t* p, size_t n) const {
  if (!p || n < 4) return false;

  // JK02 新版 BLE TLV
  if (p[0] == 0x4E && p[1] == 0x57) return true;

  // JK01 / 旧版固定 300-byte frame
  if (p[0] == 0x55 && p[1] == 0xAA && p[2] == 0xEB && p[3] == 0x90) return true;

  // JK Legacy RS485 bridge response: EB 90 ... fixed 74 bytes.
  return p[0] == 0xEB && p[1] == 0x90;
}

// [找帧头] 支持新版4E57和旧版55AAEB90。
int JkProtocol::findFrameStart(const uint8_t* p, size_t n) const {
  if (!p || n < 2) return -1;

  for (size_t i = 0; i + 1 < n; ++i) {
    if (p[i] == 0x4E && p[i + 1] == 0x57) return (int)i;

    if (i + 3 < n &&
        p[i] == 0x55 && p[i + 1] == 0xAA &&
        p[i + 2] == 0xEB && p[i + 3] == 0x90) {
      return (int)i;
    }

    if (p[i] == 0xEB && p[i + 1] == 0x90) return (int)i;
  }
  return -1;
}

// [算帧长] 新版4E57是变长，旧版固定300字节。
size_t JkProtocol::frameLength(const uint8_t* p, size_t n) const {
  if (!p || n < 4) return 0;

  if (p[0] == 0x4E && p[1] == 0x57) {
    // JK 4E57: bytes 2..3 = body length, total = body + 4.
    uint16_t bodyLen = ((uint16_t)p[2] << 8) | p[3];
    if (bodyLen < 8) return 0;
    size_t total = (size_t)bodyLen + 4U;
    if (total > FRAME_MAX) return 0;
    return total;
  }

  if (p[0] == 0x55 && p[1] == 0xAA &&
      p[2] == 0xEB && p[3] == 0x90) {
    return 300;
  }

  if (p[0] == 0xEB && p[1] == 0x90) return 74;

  return 0;
}

uint16_t JkProtocol::u16le(const uint8_t* p) {
  return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

uint32_t JkProtocol::u32le(const uint8_t* p) {
  return (uint32_t)p[0] |
         ((uint32_t)p[1] << 8) |
         ((uint32_t)p[2] << 16) |
         ((uint32_t)p[3] << 24);
}

int16_t JkProtocol::s16le(const uint8_t* p) {
  return (int16_t)u16le(p);
}

uint32_t JkProtocol::u32be(const uint8_t* p) {
  return ((uint32_t)p[0] << 24) |
         ((uint32_t)p[1] << 16) |
         ((uint32_t)p[2] << 8) |
         p[3];
}

int32_t JkProtocol::s32be(const uint8_t* p) {
  return (int32_t)u32be(p);
}

int JkProtocol::tagSize(uint8_t tag) {
  switch (tag) {
    case 0x79: return -1; // variable: cell index + mV
    case 0x80:
    case 0x81:
    case 0x82: return 2;
    case 0x83: return 4; // total voltage
    case 0x84: return 4; // current
    case 0x85: return 1; // SOC
    case 0x86: return 1;
    case 0x87: return 4; // cycle count
    case 0x88: return 4; // total capacity
    case 0x89: return 4; // alarm
    case 0x8A: return 2; // charge/discharge/balance
    case 0x8B:
    case 0x8C:
    case 0x8D: return 2;
    case 0x8E:
    case 0x8F:
    case 0x90:
    case 0x91:
    case 0x92:
    case 0x93: return 4;
    case 0x94:
    case 0x95: return 2;
    case 0x96:
    case 0x97:
    case 0x98: return 4;
    case 0x99:
    case 0x9A: return 2;
    case 0x9B: return 4;
    case 0x9C: return 2;
    case 0x9D: return 1;
    case 0x9E:
    case 0x9F: return 2;
    case 0xA0:
    case 0xA1:
    case 0xA2:
    case 0xA3:
    case 0xA4:
    case 0xA5:
    case 0xA6:
    case 0xA7:
    case 0xA8: return 2;
    case 0xA9: return 1; // actual cell count
    case 0xAA: return 4; // remaining capacity
    case 0xAB:
    case 0xAC: return 1;
    case 0xAD: return 2;
    case 0xAE:
    case 0xAF: return 1;
    case 0xB0: return 2;
    case 0xB1: return 1;
    case 0xB2: return 16;
    case 0xB3: return 1;
    case 0xB4: return 8;
    case 0xB5: return 6;
    case 0xB6: return 4;
    case 0xB7: return 16;
    case 0xB8: return 1;
    case 0xB9: return 4;
    case 0xBA: return 16; // BMS name
    case 0xBB:
    case 0xBC: return 4;
    default: return 0;
  }
}

int JkProtocol::detectOffset(const uint8_t*, size_t) const {
  return protocol32S_ ? Jk02_32S::DATA_OFFSET : Jk02_24S::DATA_OFFSET;
}

// [新版解析] 逐个Tag读取TLV字段。
bool JkProtocol::parseNewTlvFrame(const uint8_t* p, size_t n, BmsData& o) {
  if (!p || n < 14 || p[0] != 0x4E || p[1] != 0x57) return false;

  size_t declared = (size_t)(((uint16_t)p[2] << 8) | p[3]) + 4U;
  if (declared != n || declared > FRAME_MAX) return false;

  // 保留 MAC、网页配置、启动状态等本机字段，但每帧遥测字段先清零，
  // 避免旧帧缺少某个 Tag 时把上一次的电压/温度误当成本次数据。
  BmsData d = o;
  d.valid = false;
  d.online = false;
  d.cellCount = 0;
  for (uint8_t i = 0; i < JK_MAX_CELLS; ++i) d.cellVoltage[i] = 0.0f;
  d.minCellVoltage = d.maxCellVoltage = d.deltaCellVoltage = 0.0f;
  d.minCell = d.maxCell = 0;
  d.totalVoltage = d.current = d.power = d.soc = 0.0f;
  d.temperature1 = d.temperature2 = d.mosTemperature = 0.0f;
  d.totalCapacityAh = d.remainingCapacityAh = 0.0f;
  d.remainingRangeKm = 0.0f;
  d.cycleCount = 0;
  d.errors = 0;
  d.charging = d.discharging = d.balancing = d.heating = false;
  d.balancingCurrent = 0.0f;
  const uint8_t* cur = p + 10;       // 4E57 + length + address/type/counter
  const uint8_t* end = p + n - 4;    // last 4 bytes are frame checksum

  while (cur < end) {
    uint8_t tag = *cur++;
    int sz = tagSize(tag);
    if (sz == 0) break;

    if (sz < 0) {
      if (cur >= end) return false;
      uint8_t dataLen = *cur++;
      if (cur + dataLen > end) return false;

      if (tag == 0x79) {
        // [cellIndex][cellVoltage_mV_hi][cellVoltage_mV_lo]
        uint8_t count = dataLen / 3;
        if (count > JK_MAX_CELLS) count = JK_MAX_CELLS;

        for (uint8_t i = 0; i < count; ++i) {
          if (cur + 3 > end) break;
          uint8_t index = cur[0];
          uint16_t mv = ((uint16_t)cur[1] << 8) | cur[2];

          if (index < JK_MAX_CELLS) d.cellVoltage[index] = mv * 0.001f;
          cur += 3;
        }
        d.cellCount = count;
      } else {
        cur += dataLen;
      }
      continue;
    }

    if (cur + sz > end) return false;

    switch (tag) {
      case 0x80: {
        uint16_t raw = ((uint16_t)cur[0] << 8) | cur[1];
        if (raw > 2731) d.mosTemperature = (raw - 2731) / 10.0f;
        break;
      }

      case 0x81: {
        uint16_t raw = ((uint16_t)cur[0] << 8) | cur[1];
        if (raw > 2731) d.temperature1 = (raw - 2731) / 10.0f;
        break;
      }

      case 0x82: {
        uint16_t raw = ((uint16_t)cur[0] << 8) | cur[1];
        if (raw > 2731) d.temperature2 = (raw - 2731) / 10.0f;
        break;
      }

      case 0x83:
        d.totalVoltage = u32be(cur) / 1000.0f;
        break;

      case 0x84:
        d.current = s32be(cur) / 1000.0f;
        break;

      case 0x85:
        d.soc = cur[0];
        if (d.soc > 100.0f) d.soc = 100.0f;
        break;

      case 0x87:
        d.cycleCount = u32be(cur);
        break;

      case 0x88:
        d.totalCapacityAh = u32be(cur) / 1000.0f;
        break;

      case 0x89:
        d.errors = u32be(cur);
        break;

      case 0x8A: {
        uint16_t status = ((uint16_t)cur[0] << 8) | cur[1];
        d.charging = (status & 0x0001) != 0;
        d.discharging = (status & 0x0002) != 0;
        d.balancing = (status & 0x0004) != 0;
        break;
      }

      case 0xA9:
        if (cur[0] > 0 && cur[0] <= JK_MAX_CELLS) d.cellCount = cur[0];
        break;

      case 0xAA:
        d.remainingCapacityAh = u32be(cur) / 1000.0f;
        break;

      case 0xBA: {
        size_t copyLen = sz < 16 ? sz : 16;
        // BmsData.deviceName is String, so copy through a temporary C string.
        char nameBuf[17];
        memcpy(nameBuf, cur, copyLen);
        nameBuf[copyLen] = '\0';
        for (size_t i = 0; i < copyLen; ++i) {
          if ((uint8_t)nameBuf[i] < 0x20 || (uint8_t)nameBuf[i] > 0x7E) nameBuf[i] = '\0';
        }
        d.deviceName = String(nameBuf);
        break;
      }

      default:
        break;
    }

    cur += sz;
  }

  // If the frame contains cells but not total voltage, derive total voltage.
  if (d.totalVoltage < 0.5f && d.cellCount > 0) {
    float sum = 0;
    uint8_t count = d.cellCount;
    if (count > JK_MAX_CELLS) count = JK_MAX_CELLS;
    for (uint8_t i = 0; i < count; ++i) sum += d.cellVoltage[i];
    d.totalVoltage = sum;
  }

  if (d.remainingCapacityAh < 0.01f && d.totalCapacityAh > 0.01f) {
    d.remainingCapacityAh = d.totalCapacityAh * d.soc / 100.0f;
  }

  if (d.totalVoltage > 0.1f) {
    float minV = 100.0f;
    float maxV = 0.0f;
    uint8_t minC = 0;
    uint8_t maxC = 0;

    for (uint8_t i = 0; i < d.cellCount && i < JK_MAX_CELLS; ++i) {
      float v = d.cellVoltage[i];
      if (v > 0.5f && v < 6.0f) {
        if (v < minV) { minV = v; minC = i + 1; }
        if (v > maxV) { maxV = v; maxC = i + 1; }
      }
    }

    d.minCellVoltage = minV < 100.0f ? minV : 0.0f;
    d.maxCellVoltage = maxV;
    d.deltaCellVoltage = (maxV > 0.0f && minV < 100.0f) ? maxV - minV : 0.0f;
    d.minCell = minC;
    d.maxCell = maxC;
  }

  d.power = d.totalVoltage * d.current;

  if (d.energyConsumptionWhKm > 1.0f && d.totalVoltage > 0.1f) {
    d.remainingRangeKm =
      (d.remainingCapacityAh * d.totalVoltage) / d.energyConsumptionWhKm;
  }

  d.valid = d.totalVoltage > 0.1f || d.cellCount > 0 || d.soc > 0;
  d.online = d.valid;
  d.updateMs = millis();

  if (d.valid) o = d;
  return d.valid;
}

// [旧版解析] 按24S/32S offset读取固定300字节数据。
bool JkProtocol::parseOldFrame(const uint8_t* p, size_t n, BmsData& o) {
  int off = detectOffset(p, n);

  if (!p || n != 300 || n < (size_t)(184 + off)) return false;
  if (!(p[0] == 0x55 && p[1] == 0xAA && p[2] == 0xEB && p[3] == 0x90)) return false;

  // 旧JK固定300字节帧：byte[299]为 byte[0..298] 的8位累加校验。
  uint8_t crc=0;
  for(size_t i=0;i<299;i++) crc=(uint8_t)(crc+p[i]);
  if(crc!=p[299]) return false;

  uint32_t mask = u32le(p + 54 + off);
  uint8_t maxCells = protocol32S_ ? Jk02_32S::MAX_CELLS : Jk02_24S::MAX_CELLS;
  uint8_t cells = 0;

  for (uint8_t i = 0; i < maxCells; ++i)
    if (mask & (1UL << i)) cells++;

  if (cells == 0) return false;
  if (cells > JK_MAX_CELLS) cells = JK_MAX_CELLS;

  float minV = 100.0f;
  float maxV = 0.0f;
  uint8_t minC = 0, maxC = 0;

  for (uint8_t i = 0; i < JK_MAX_CELLS; ++i) o.cellVoltage[i] = 0;

  for (uint8_t i = 0; i < cells; ++i) {
    size_t pos = 6 + i * 2;
    if (pos + 1 >= n) break;

    float v = u16le(p + pos) * 0.001f;
    o.cellVoltage[i] = v;

    if (v > 0.5f && v < 6.0f) {
      if (v < minV) { minV = v; minC = i + 1; }
      if (v > maxV) { maxV = v; maxC = i + 1; }
    }
  }

  o.cellCount = cells;
  o.minCellVoltage = minV < 100.0f ? minV : 0.0f;
  o.maxCellVoltage = maxV;
  o.deltaCellVoltage = (maxV > 0.0f && minV < 100.0f) ? maxV - minV : 0.0f;
  o.minCell = minC;
  o.maxCell = maxC;

  o.totalVoltage = u32le(p + 118 + off) * 0.001f;
  o.current = (int32_t)u32le(p + 126 + off) * 0.001f;
  o.power = o.totalVoltage * o.current;
  o.temperature1 = s16le(p + 130 + off) * 0.1f;
  o.temperature2 = s16le(p + 132 + off) * 0.1f;
  o.mosTemperature = s16le(p + (off ? 112 + off : 134)) * 0.1f;

  if (protocol32S_) o.errors = u32le(p + 134 + off);
  else o.errors = u16le(p + 136);

  o.balancingCurrent = u16le(p + 138 + off) * 0.001f;
  o.balancing = p[140 + off] != 0;
  o.soc = p[141 + off];
  if (o.soc > 100.0f) o.soc = 100.0f;
  o.remainingCapacityAh = u32le(p + 142 + off) * 0.001f;
  o.totalCapacityAh = u32le(p + 146 + off) * 0.001f;

  if (o.energyConsumptionWhKm > 1.0f && o.totalVoltage > 0.1f)
    o.remainingRangeKm =
      (o.remainingCapacityAh * o.totalVoltage) / o.energyConsumptionWhKm;

  o.charging = p[166 + off] != 0;
  o.discharging = p[167 + off] != 0;
  o.balancing = o.balancing || p[169 + off] != 0;
  o.heating = p[183 + off] != 0;
  o.valid = o.totalVoltage > 0.1f;
  o.online = o.valid;
  o.updateMs = millis();

  return o.valid;
}

// [Legacy RS485] 兼容 JK BLE-RS485 bridge 的 EB90 74字节响应。
// 该流程来自 dionipe 项目公开实现：先做累加校验，再提取总压/节数/电芯/均衡/报警。
// 仍然由 JkProtocol 负责，因此不会破坏 BmsBle -> Manager -> Protocol 的分层。
bool JkProtocol::parseLegacyRs485Frame(const uint8_t* p, size_t n, BmsData& o) {
  if(!p || n!=74) return false;
  if(p[0]!=0xEB || p[1]!=0x90) return false;

  uint8_t crc=0;
  for(size_t i=0;i<73;i++) crc=(uint8_t)(crc+p[i]);
  if(crc!=p[73]) return false;

  auto be16=[](const uint8_t* q)->uint16_t{
    return (uint16_t(q[0])<<8)|q[1];
  };

  BmsData d=o;
  d.deviceName="JK RS485";
  d.totalVoltage=float(be16(p+4))*0.01f;

  uint8_t cells=p[8];
  if(cells>JK_MAX_CELLS) cells=JK_MAX_CELLS;
  d.cellCount=cells;

  d.balancing=(p[11]&0x03)!=0;
  d.errors=p[12];

  for(uint8_t i=0;i<cells;i++){
    size_t off=23U+size_t(i)*2U;
    if(off+1>=73) break;
    d.cellVoltage[i]=be16(p+off)*0.001f;
  }

  if(d.totalVoltage<0.5f && d.cellCount>0){
    float sum=0.0f;
    for(uint8_t i=0;i<d.cellCount;i++) sum+=d.cellVoltage[i];
    d.totalVoltage=sum;
  }

  d.current=0.0f;
  d.power=0.0f;
  d.charging=false;
  d.discharging=false;
  d.valid=(d.cellCount>0 || d.totalVoltage>0.5f);
  d.online=d.valid;
  d.updateMs=millis();

  if(d.valid) o=d;
  return d.valid;
}

// [总入口] 根据帧头选择新版或旧版解析。
bool JkProtocol::parseFrame(const uint8_t* p, size_t n, BmsData& o) {
  if (!canHandle(p, n)) return false;

  if (p[0] == 0x4E && p[1] == 0x57)
    return parseNewTlvFrame(p, n, o);

  if (p[0] == 0xEB && p[1] == 0x90)
    return parseLegacyRs485Frame(p, n, o);

  return parseOldFrame(p, n, o);
}

bool JkProtocol::buildCommand(uint8_t cmd, uint8_t counter, uint8_t out[20]) {
  (void)counter;
  if (!out) return false;

  memset(out, 0, 20);
  out[0] = 0xAA;
  out[1] = 0x55;
  out[2] = 0x90;
  out[3] = 0xEB;
  out[4] = cmd;
  out[5] = 0x00;

  uint8_t checksum = 0;
  for (int i = 0; i < 19; ++i) checksum = (uint8_t)(checksum + out[i]);
  out[19] = checksum;

  return true;
}
