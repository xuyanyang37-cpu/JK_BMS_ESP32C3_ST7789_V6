#ifndef DISPLAY_CONFIG_H
#define DISPLAY_CONFIG_H

#include <Arduino.h>

/*
 * 屏幕显示配置：
 * 位置固定为右侧四行，不改变320x170物理布局。
 * WebConfig负责修改，Display只读取。
 */
enum DisplayMetric : uint8_t {
  DISPLAY_VOLTAGE = 0,
  DISPLAY_CURRENT,
  DISPLAY_POWER,
  DISPLAY_MIN_VOLTAGE,
  DISPLAY_MAX_VOLTAGE,
  DISPLAY_AVG_VOLTAGE,
  DISPLAY_DELTA_VOLTAGE,
  DISPLAY_CELL_COUNT,
  DISPLAY_MOS_TEMP,
  DISPLAY_TEMP1,
  DISPLAY_TEMP2,
  DISPLAY_REMAINING_CAPACITY,
  DISPLAY_TOTAL_CAPACITY,
  DISPLAY_REMAINING_RANGE,
  DISPLAY_SOC,
  DISPLAY_CYCLE_COUNT
};

struct DisplayRowConfig {
  uint8_t metric;
  uint16_t color;
  uint8_t font;
};

struct DisplayConfig {
  DisplayRowConfig row[4];
  uint16_t socColor;
  uint16_t tempColor;
  uint16_t capacityColor;
  bool socBar;

  void setDefaults();
  void load();
  void save() const;
};

extern DisplayConfig g_displayConfig;

const char* displayMetricName(uint8_t metric);
