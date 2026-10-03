#include "DisplayConfig.h"
#include <Preferences.h>

DisplayConfig g_displayConfig;

void DisplayConfig::setDefaults() {
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
  setDefaults();

  Preferences p;
  if (!p.begin("jkdisplay", true)) return;

  for (uint8_t i = 0; i < 4; ++i) {
    char key[12];
    snprintf(key, sizeof(key), "m%u", i);
    row[i].metric = p.getUChar(key, row[i].metric);
    snprintf(key, sizeof(key), "c%u", i);
    row[i].color = p.getUShort(key, row[i].color);
    snprintf(key, sizeof(key), "f%u", i);
    row[i].font = p.getUChar(key, row[i].font);
    if (row[i].metric > DISPLAY_CYCLE_COUNT) row[i].metric = DISPLAY_VOLTAGE;
    if (row[i].font < 2 || row[i].font > 4) row[i].font = 4;
  }

  socColor = p.getUShort("soc", socColor);
  tempColor = p.getUShort("temp", tempColor);
  capacityColor = p.getUShort("cap", capacityColor);
  socBar = p.getBool("bar", true);
  p.end();
}

void DisplayConfig::save() const {
  Preferences p;
  if (!p.begin("jkdisplay", false)) return;

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
