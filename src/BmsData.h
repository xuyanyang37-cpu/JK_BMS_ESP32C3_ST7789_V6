#ifndef BMS_DATA_H
#define BMS_DATA_H

#include <Arduino.h>

/*
 * ================================================================
 * BmsData.h - 全项目共享的 BMS 数据中心
 *
 * 【最重要的理解】
 *
 *   蓝牙 BLE 收到原始数据
 *          ↓
 *   protocol/* 解析协议
 *          ↓
 *   写入 g_bmsData
 *          ↓
 *      ┌───┴──────────────┐
 *      ↓                  ↓
 *   Display.cpp       WebConfig.cpp
 *      ↓                  ↓
 *    屏幕显示          网页/API
 *
 * 所以：
 *   1. 本文件只定义“数据长什么样”，不负责解析。
 *   2. protocol/ 主要负责写入电气数据。
 *   3. BmsBle 主要负责写入连接/扫描状态。
 *   4. WebConfig 主要负责写入网页配置参数。
 *   5. Display 主要读取，不应该在这里修改 BMS 原始数据。
 *
 * 每一个变量下面都明确标注：
 *   谁写入 → 谁读取 → 单位 → 数据来源 → 什么时候更新 → 异常时是什么值
 *
 * “异常值”这里特指本项目当前代码中的默认/回退值，
 * 不代表所有异常都能仅靠这个值判断。
 * 判断“数据是否可信”优先看 online / valid。
 * ================================================================
 */

#define JK_MAX_CELLS 32

/*
 * ================================================================
 * 启动状态
 *
 * BOOT_START      = 刚启动
 * BOOT_SCANNING   = 正在扫描
 * BOOT_CONNECTING = 正在连接
 * BOOT_CONNECTED  = 已连接并且收到有效数据
 * BOOT_HOTSPOT    = 三次失败后进入热点配网
 * ================================================================
 */
enum BmsBootState {
  BOOT_START = 0,
  BOOT_SCANNING = 1,
  BOOT_CONNECTING = 2,
  BOOT_CONNECTED = 3,
  BOOT_HOTSPOT = 4
};

struct BmsData {

  // ==============================================================
  // ① BMS在线/数据有效状态
  // ==============================================================

  /*
   * online
   * 谁写入：BmsBle / JkProtocol
   * 谁读取：Display / WebConfig / main
   * 单位：bool（无单位）
   * 数据来源：BLE连接状态 + 协议解析结果
   * 什么时候更新：
   *   - BLE断开时 -> false
   *   - 成功解析有效BMS帧时 -> true
   * 异常值：false
   *
   * 注意：
   *   online=true 只表示当前认为BMS在线；
   *   是否有可信数据还要结合 valid。
   */
  bool online = false;

  /*
   * valid
   * 谁写入：JkProtocol::parseNewTlvFrame / parseOldFrame
   * 谁读取：main / BmsBle / Display / WebConfig
   * 单位：bool（无单位）
   * 数据来源：JK协议帧解析结果
   * 什么时候更新：
   *   - 成功解析并且满足最低数据条件 -> true
   *   - 本次启动初始化、连接失败 -> false
   * 异常值：false
   *
   * 当前JK判断条件：
   *   新版：总电压 > 0.1V 或 有电芯 或 SOC > 0
   *   旧版：总电压 > 0.1V
   */
  bool valid = false;

  // ==============================================================
  // ② 电芯数据
  // ==============================================================  

  /*
   * cellCount
   * 谁写入：JkProtocol
   * 谁读取：Display / 其他协议或业务代码
   * 单位：节
   * 数据来源：JK 0x79 电芯TLV 或 0xA9 实际电芯数量；
   *           旧版由电芯mask统计
   * 什么时候更新：每次成功解析BMS帧
   * 异常值：0
   */
  uint8_t cellCount = 0;

  /*
   * cellVoltage[]
   * 谁写入：JkProtocol
   * 谁读取：Display / 电芯统计计算
   * 单位：V
   * 数据来源：
   *   新版：JK Tag 0x79，mV × 0.001
   *   旧版：固定帧电芯字段，mV × 0.001
   * 什么时候更新：每次成功解析包含电芯数据的帧
   * 异常值：0.0V
   *
   * 下标：
   *   cellVoltage[0] = 第1节
   *   cellVoltage[1] = 第2节
   *   ...
   *   最大32节。
   */
  float cellVoltage[JK_MAX_CELLS] = {0};

  /*
   * minCellVoltage
   * 谁写入：JkProtocol
   * 谁读取：Display / WebConfig或后续保护逻辑
   * 单位：V
   * 数据来源：根据 cellVoltage[] 实时计算
   * 什么时候更新：每次解析完电芯数据后重新计算
   * 异常值：0.0V
   */
  float minCellVoltage = 0;

  /*
   * maxCellVoltage
   * 谁写入：JkProtocol
   * 谁读取：Display / WebConfig或后续保护逻辑
   * 单位：V
   * 数据来源：根据 cellVoltage[] 实时计算
   * 什么时候更新：每次解析完电芯数据后重新计算
   * 异常值：0.0V
   */
  float maxCellVoltage = 0;

  /*
   * deltaCellVoltage
   * 谁写入：JkProtocol
   * 谁读取：Display / 后续保护逻辑
   * 单位：V
   * 数据来源：maxCellVoltage - minCellVoltage
   * 什么时候更新：每次解析完电芯数据后重新计算
   * 异常值：0.0V
   */
  float deltaCellVoltage = 0;

  /*
   * minCell
   * 谁写入：JkProtocol
   * 谁读取：Display / 后续保护逻辑
   * 单位：节编号
   * 数据来源：计算最低有效电芯电压时记录
   * 什么时候更新：每次解析完电芯数据后
   * 异常值：0（表示没有找到有效电芯）
   *
   * 注意：这里是“第几节”，所以有效值从1开始，不是数组下标。
   */
  uint8_t minCell = 0;

  /*
   * maxCell
   * 谁写入：JkProtocol
   * 谁读取：Display / 后续保护逻辑
   * 单位：节编号
   * 数据来源：计算最高有效电芯电压时记录
   * 什么时候更新：每次解析完电芯数据后
   * 异常值：0
   */
  uint8_t maxCell = 0;

  // ==============================================================
  // ③ 电池组实时电气参数
  // ==============================================================

  /*
   * totalVoltage
   * 谁写入：JkProtocol
   * 谁读取：Display / WebConfig / remainingRange计算
   * 单位：V
   * 数据来源：
   *   新版：JK Tag 0x83，原始值 ÷ 1000
   *   旧版：固定帧字段，原始值 × 0.001
   *   新版没有总电压时，可由电芯电压求和得到
   * 什么时候更新：每次成功解析BMS帧
   * 异常值：0.0V
   */
  float totalVoltage = 0;

  /*
   * current
   * 谁写入：JkProtocol
   * 谁读取：Display / WebConfig / power计算
   * 单位：A
   * 数据来源：
   *   新版：JK Tag 0x84，有符号32位 ÷ 1000
   *   旧版：固定帧字段，有符号值 × 0.001
   * 什么时候更新：每次成功解析BMS帧
   * 异常值：0.0A
   *
   * 正负方向的具体含义由JK协议定义；
   * 本文件不擅自把正数解释成充电或放电。
   */
  float current = 0;

  /*
   * power
   * 谁写入：JkProtocol
   * 谁读取：Display / WebConfig
   * 单位：W
   * 数据来源：totalVoltage × current
   * 什么时候更新：每次解析得到电压、电流后计算
   * 异常值：0.0W
   */
  float power = 0;

  /*
   * soc
   * 谁写入：JkProtocol
   * 谁读取：Display / WebConfig / remainingRange计算
   * 单位：%
   * 数据来源：JK Tag 0x85 或旧版固定帧
   * 什么时候更新：每次成功解析BMS帧
   * 异常值：0%
   * 限制：解析后超过100会被限制为100
   */
  float soc = 0;

  // ==============================================================
  // ④ 温度
  // ==============================================================

  /*
   * temperature1
   * 谁写入：JkProtocol
   * 谁读取：Display / WebConfig或后续保护逻辑
   * 单位：°C
   * 数据来源：JK Tag 0x81 或旧版固定帧
   * 什么时候更新：每次成功解析BMS帧
   * 异常值：0°C
   */
  float temperature1 = 0;

  /*
   * temperature2
   * 谁写入：JkProtocol
   * 谁读取：Display / WebConfig或后续保护逻辑
   * 单位：°C
   * 数据来源：JK Tag 0x82 或旧版固定帧
   * 什么时候更新：每次成功解析BMS帧
   * 异常值：0°C
   */
  float temperature2 = 0;

  /*
   * mosTemperature
   * 谁写入：JkProtocol
   * 谁读取：Display / WebConfig或后续保护逻辑
   * 单位：°C
   * 数据来源：JK Tag 0x80 或旧版固定帧MOS温度字段
   * 什么时候更新：每次成功解析BMS帧
   * 异常值：0°C
   */
  float mosTemperature = 0;

  // ==============================================================
  // ⑤ 容量 / 循环 / 均衡
  // ==============================================================

  /*
   * remainingCapacityAh
   * 谁写入：JkProtocol；网页保存参数时只参与重新计算，不改变其BMS来源
   * 谁读取：Display / WebConfig / remainingRange计算
   * 单位：Ah
   * 数据来源：
   *   新版：JK Tag 0xAA，原始值 ÷ 1000
   *   旧版：固定帧字段，原始值 × 0.001
   *   新版若没有该字段，则可由 totalCapacityAh × SOC / 100 估算
   * 什么时候更新：每次成功解析BMS帧
   * 异常值：0.0Ah
   */
  float remainingCapacityAh = 0;

  /*
   * totalCapacityAh
   * 谁写入：JkProtocol
   * 谁读取：WebConfig / remainingCapacity估算
   * 单位：Ah
   * 数据来源：JK Tag 0x88 或旧版固定帧
   * 什么时候更新：每次成功解析BMS帧
   * 异常值：0.0Ah
   */
  float totalCapacityAh = 0;

  /*
   * balancingCurrent
   * 谁写入：JkProtocol
   * 谁读取：Display / 后续均衡状态页面
   * 单位：A
   * 数据来源：JK旧版固定帧；新版当前解析器未从已支持TLV中赋值
   * 什么时候更新：旧版帧解析时更新
   * 异常值：0.0A
   */
  float balancingCurrent = 0;

  /*
   * cycleCount
   * 谁写入：JkProtocol
   * 谁读取：后续统计/网页/显示模块
   * 单位：次
   * 数据来源：JK新版 Tag 0x87
   * 什么时候更新：收到包含0x87的有效帧
   * 异常值：0
   */
  uint32_t cycleCount = 0;

  // ==============================================================
  // ⑥ 剩余里程估算
  // ==============================================================

  /*
   * remainingRangeKm
   * 谁写入：JkProtocol；WebConfig保存新Wh/km参数后立即重算
   * 谁读取：Display / WebConfig
   * 单位：km
   * 数据来源：软件计算，不是BMS原始字段
   *
   * 公式：
   *   剩余公里 = 剩余容量(Ah) × 电压(V) ÷ 每公里耗电(Wh/km)
   *
   * 什么时候更新：
   *   1. 每次BMS解析成功
   *   2. 网页修改每公里耗电量并保存时立即重算
   *
   * 异常值：0.0km
   */
  float remainingRangeKm = 0;

  /*
   * energyConsumptionWhKm
   * 谁写入：
   *   1. BmsBle::begin() 从 Preferences 读取
   *   2. WebConfig::begin() 从 Preferences读取
   *   3. WebConfig::handleSave() 写入用户设置
   *   4. JkProtocol解析时保留旧值，避免被默认值覆盖
   * 谁读取：JkProtocol / WebConfig / Display
   * 单位：Wh/km
   * 数据来源：用户网页设置 + Preferences持久化
   * 什么时候更新：开机读取一次；网页保存时更新
   * 异常值：100.0 Wh/km
   *
   * 有效范围：
   *   <1 或 >1000 时回退到100。
   */
  float energyConsumptionWhKm = 100.0f;

  // ==============================================================
  // ⑦ 充电 / 放电 / 均衡 / 加热状态
  // ==============================================================

  /*
   * charging
   * 谁写入：JkProtocol
   * 谁读取：Display / WebConfig / 后续业务逻辑
   * 单位：bool
   * 数据来源：
   *   新版：JK Tag 0x8A bit0
   *   旧版：固定帧充电状态字段
   * 什么时候更新：每次成功解析BMS帧
   * 异常值：false
   */
  bool charging = false;

  /*
   * discharging
   * 谁写入：JkProtocol
   * 谁读取：Display / WebConfig / 后续业务逻辑
   * 单位：bool
   * 数据来源：
   *   新版：JK Tag 0x8A bit1
   *   旧版：固定帧放电状态字段
   * 什么时候更新：每次成功解析BMS帧
   * 异常值：false
   */
  bool discharging = false;

  /*
   * balancing
   * 谁写入：JkProtocol
   * 谁读取：Display / WebConfig / 后续业务逻辑
   * 单位：bool
   * 数据来源：
   *   新版：JK Tag 0x8A bit2
   *   旧版：固定帧均衡字段
   * 什么时候更新：每次成功解析BMS帧
   * 异常值：false
   */
  bool balancing = false;

  /*
   * heating
   * 谁写入：JkProtocol
   * 谁读取：Display / 后续业务逻辑
   * 单位：bool
   * 数据来源：JK旧版固定帧加热状态字段
   * 什么时候更新：解析旧版固定帧时
   * 异常值：false
   *
   * 注意：当前新版4E57 TLV解析器没有看到对应赋值，
   * 因此新版帧下保持默认false。
   */
  bool heating = false;

  // ==============================================================
  // ⑧ 故障与更新时间
  // ==============================================================

  /*
   * errors
   * 谁写入：JkProtocol
   * 谁读取：Display / WebConfig / 后续故障页面
   * 单位：bit mask / 无单位
   * 数据来源：
   *   新版：JK Tag 0x89
   *   旧版：固定帧故障字段
   * 什么时候更新：每次成功解析包含故障字段的帧
   * 异常值：0（没有记录故障位）
   *
   * 注意：0不等于“整个BMS绝对没有任何故障”，
   * 它只表示当前这个字段为0。
   */
  uint32_t errors = 0;

  /*
   * updateMs
   * 谁写入：JkProtocol
   * 谁读取：Display / 后续数据超时判断
   * 单位：ms（millis()系统运行时间）
   * 数据来源：ESP32 Arduino millis()
   * 什么时候更新：每次成功解析有效BMS帧
   * 异常值：0
   *
   * 作用：
   *   可以用 millis() - updateMs 判断数据多久没有刷新。
   */
  uint32_t updateMs = 0;

  // ==============================================================
  // ⑨ 蓝牙设备信息
  // ==============================================================

  /*
   * mac
   * 谁写入：BmsBle连接流程；网页连接/配置流程可能参与保存
   * 谁读取：WebConfig / Display / 后续连接管理
   * 单位：字符串
   * 数据来源：BLE设备MAC地址
   * 什么时候更新：连接/识别到目标设备时
   * 异常值：空字符串
   */
  String mac;

  /*
   * deviceName
   * 谁写入：JkProtocol新版Tag 0xBA
   * 谁读取：WebConfig / Display
   * 单位：字符串
   * 数据来源：JK BMS协议中的BMS名称
   * 什么时候更新：收到包含0xBA的新版JK帧
   * 异常值：空字符串
   */
  String deviceName;

  /*
   * softwareVersion
   * 谁写入：当前代码没有看到实际协议赋值
   * 谁读取：后续WebConfig / Display可读取
   * 单位：字符串
   * 数据来源：预留给BMS软件版本
   * 什么时候更新：当前实现不会主动更新
   * 异常值：空字符串
   */
  String softwareVersion;

  /*
   * hardwareVersion
   * 谁写入：当前代码没有看到实际协议赋值
   * 谁读取：后续WebConfig / Display可读取
   * 单位：字符串
   * 数据来源：预留给BMS硬件版本
   * 什么时候更新：当前实现不会主动更新
   * 异常值：空字符串
   */
  String hardwareVersion;

  // ==============================================================
  // ⑩ 启动业务状态
  // ==============================================================

  /*
   * bootState
   * 谁写入：main.cpp / BmsBle
   * 谁读取：Display / WebConfig
   * 单位：BmsBootState枚举
   * 数据来源：本机启动业务流程，不是BMS协议
   * 什么时候更新：
   *   启动 -> SCANNING -> CONNECTING -> CONNECTED
   *   三次失败 -> HOTSPOT
   * 异常/初始值：BOOT_START
   */
  BmsBootState bootState = BOOT_START;

  /*
   * scanAttempt
   * 谁写入：main.cpp / BmsBle
   * 谁读取：Display / WebConfig
   * 单位：次
   * 数据来源：本机启动扫描计数
   * 什么时候更新：每一轮启动扫描前
   * 异常/初始值：0
   * 正常启动范围：1~3
   */
  uint8_t scanAttempt = 0;

  /*
   * scanMax
   * 谁写入：main.cpp
   * 谁读取：Display / 后续UI逻辑
   * 单位：次
   * 数据来源：本项目固定业务规则
   * 什么时候更新：setup()初始化时设置为3
   * 异常/默认值：3
   */
  uint8_t scanMax = 3;

  /*
   * hotspot
   * 谁写入：main.cpp::startHotspot()
   * 谁读取：WebConfig / Display
   * 单位：bool
   * 数据来源：本机Wi-Fi AP状态
   * 什么时候更新：三次BLE连接失败并成功启动SoftAP后 -> true
   * 异常/初始值：false
   */
  bool hotspot = false;

  /*
   * hotspotIp
   * 谁写入：main.cpp::startHotspot()
   * 谁读取：Display / WebConfig
   * 单位：IPv4字符串
   * 数据来源：WiFi.softAPIP()
   * 什么时候更新：热点启动后
   * 异常/初始值：空字符串
   */
  String hotspotIp;

  /*
   * statusMessage
   * 谁写入：main.cpp / BmsBle
   * 谁读取：Display / WebConfig
   * 单位：字符串
   * 数据来源：本机业务流程状态文字
   * 什么时候更新：
   *   扫描、连接、失败、成功、热点等状态切换时
   * 异常/初始值："正在启动"
   */
  String statusMessage = "正在启动";
};

/*
 * ================================================================
 * 全局唯一数据对象
 *
 * 写入链：
 *   BmsBle / Protocol / main / WebConfig
 *                 ↓
 *             g_bmsData
 *
 * 读取链：
 *   g_bmsData
 *      ├── Display
 *      ├── WebConfig
 *      └── main
 *
 * 以后增加 ANT / JBD / Daly / TT 协议时，
 * 推荐继续遵守：
 *
 *   “协议解析器负责写入实际BMS数据，
 *    UI/Web只读取，不自己重新解析原始蓝牙帧。”
 * ================================================================
 */
extern BmsData g_bmsData;

#endif
