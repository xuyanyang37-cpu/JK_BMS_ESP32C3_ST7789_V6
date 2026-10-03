/*
 * DisplayConfig.cpp
 * ================================================================
 * 屏幕配置的NVS持久化实现。
 *
 * Preferences命名空间：
 *   jkdisplay
 *
 * 键值约定：
 *   m0~m3  = 四行显示指标
 *   c0~c3  = 四行RGB565颜色
 *   f0~f3  = 四行字体
 *   soc    = SOC颜色
 *   temp   = 温度颜色
 *   cap    = 容量颜色
 *   bar    = SOC底部进度条开关
 *
 * 所有网页参数都在这里做最终合法性保护，避免Flash中保存非法值。
 * ================================================================
 */

#include "DisplayConfig.h"
#include <TFT_eSPI.h>
#include <Preferences.h>

DisplayConfig g_displayConfig;

void DisplayConfig::setDefaults() {
  // 默认布局保持当前已经确认的V6界面：
  // 电压 / 电流 / 最低电压 / 剩余里程。
  row[0] = {DISPLAY_VOLTAGE, 0xFFE0, 4};
  row[1] = {DISPLAY_CURRENT, 0x07E0, 4};
  row[2] = {DISPLAY_MIN_VOLTAGE, 0xFCA8, 4};
  row[3] = {DISPLAY_REMAINING_RANGE, 0x2F3C, 4};

  socColor = TFT_WHITE;
  tempColor = 0x07E0;
  capacityColor = TFT_WHITE;
  socBar = true;
}

void DisplayConfig::load() {
  // 每次读取前先建立一套可靠默认值。
  // 这样即使NVS没有某个键，也不会得到未初始化数据。
  setDefaults();

  Preferences p;

  // true = readOnly，不修改Flash。
  if (!p.begin("jkdisplay", true)) {
    return;
  }

  for (uint8_t i = 0; i < 4; ++i) {
    char key[12];

    // m0~m3：显示指标。
    snprintf(key, sizeof(key), "m%u", i);
    row[i].metric = p.getUChar(key, row[i].metric);

    // c0~c3：RGB565颜色。
    snprintf(key, sizeof(key), "c%u", i);
    row[i].color = p.getUShort(key, row[i].color);

    // f0~f3：字体编号。
    snprintf(key, sizeof(key), "f%u", i);
    row[i].font = p.getUChar(key, row[i].font);

    // 防止网页或Flash损坏导致非法枚举值。
    if (row[i].metric > DISPLAY_CYCLE_COUNT) {
      row[i].metric = DISPLAY_VOLTAGE;
    }

    // 固定布局只允许2~4号字体。
    if (row[i].font < 2 || row[i].font > 4) {
      row[i].font = 4;
    }
  }

  socColor = p.getUShort("soc", socColor);
  tempColor = p.getUShort("temp", tempColor);
  capacityColor = p.getUShort("cap", capacityColor);
  socBar = p.getBool("bar", true);

  p.end();
}

void DisplayConfig::save() const {
  Preferences p;

  // false = 可写模式。
  if (!p.begin("jkdisplay", false)) {
    return;
  }

  for (uint8_t i = 0; i < 4; ++i) {
    char key[12];

    snprintf(key, sizeof(key), "m%u", i);
    p.putUChar(key, row[i].metric);

    snprintf(key, sizeof(key), "c%u", i);
    p.putUShort(key, row[i].color);

    snprintf(key, sizeof(key), "f%u", i);
    p.putUChar(key, row[i].font);
  }

  p.putUShort("soc", socColor);
  p.putUShort("temp", tempColor);
  p.putUShort("cap", capacityColor);
  p.putBool("bar", socBar);

  p.end();
}

const char* displayMetricName(uint8_t metric) {
  switch (metric) {
    case DISPLAY_VOLTAGE: return "电压";
    case DISPLAY_CURRENT: return "电流";
    case DISPLAY_POWER: return "功率";
    case DISPLAY_MIN_VOLTAGE: return "最低电压";
    case DISPLAY_MAX_VOLTAGE: return "最高电压";
    case DISPLAY_AVG_VOLTAGE: return "平均电压";
    case DISPLAY_DELTA_VOLTAGE: return "单体压差";
    case DISPLAY_CELL_COUNT: return "电芯数量";
    case DISPLAY_MOS_TEMP: return "MOS温度";
    case DISPLAY_TEMP1: return "温度1";
    case DISPLAY_TEMP2: return "温度2";
    case DISPLAY_REMAINING_CAPACITY: return "剩余容量";
    case DISPLAY_TOTAL_CAPACITY: return "总容量";
    case DISPLAY_REMAINING_RANGE: return "剩余里程";
    case DISPLAY_SOC: return "SOC";
    case DISPLAY_CYCLE_COUNT: return "循环次数";
    default: return "电压";
  }
}
