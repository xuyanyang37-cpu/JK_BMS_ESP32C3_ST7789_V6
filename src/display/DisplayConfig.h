/*
 * DisplayConfig.h
 * ================================================================
 * 屏幕“显示什么”与“怎么显示”的独立配置层。
 *
 * 设计原则：
 *   BmsData       = BMS真实数据
 *   DisplayConfig = 用户显示偏好
 *   Display      = 按固定320x170布局渲染
 *
 * WebConfig只修改DisplayConfig，不直接修改Display.cpp中的布局坐标。
 *
 * 四个右侧数据框位置固定：
 *   row[0] -> Y=4
 *   row[1] -> Y=42
 *   row[2] -> Y=80
 *   row[3] -> Y=118
 *
 * 配置保存到ESP32 NVS Preferences：
 *   命名空间：jkdisplay
 *
 * 因此网页修改一次后，即使ESP32重新上电，配置仍然存在。
 * ================================================================
 */

#ifndef DISPLAY_CONFIG_H
#define DISPLAY_CONFIG_H

#include <Arduino.h>

/*
 * 可显示的数据项目。
 *
 * 注意：
 *   枚举值会写入Preferences，所以不要随意改变已有项目的数字顺序。
 *   如果以后删除项目，应该保留旧编号或做版本迁移。
 */
enum DisplayMetric : uint8_t {
  DISPLAY_VOLTAGE = 0,              // 总电压，V
  DISPLAY_CURRENT,                  // 电流，A
  DISPLAY_POWER,                    // 功率，W
  DISPLAY_MIN_VOLTAGE,              // 最低单体电压，V
  DISPLAY_MAX_VOLTAGE,              // 最高单体电压，V
  DISPLAY_AVG_VOLTAGE,              // 有效电芯平均电压，V
  DISPLAY_DELTA_VOLTAGE,            // 最高-最低，mV显示
  DISPLAY_CELL_COUNT,               // 电芯数量，S
  DISPLAY_MOS_TEMP,                 // MOS温度，C
  DISPLAY_TEMP1,                    // 温度1，C
  DISPLAY_TEMP2,                    // 温度2，C
  DISPLAY_REMAINING_CAPACITY,       // 剩余容量，Ah
  DISPLAY_TOTAL_CAPACITY,            // 总容量，Ah
  DISPLAY_REMAINING_RANGE,          // 软件估算剩余里程，km
  DISPLAY_SOC,                      // SOC，%
  DISPLAY_CYCLE_COUNT               // 循环次数
};

/*
 * 单个右侧数据框的配置。
 *
 * metric：
 *   决定这个框显示BmsData中的哪一个数据。
 *
 * color：
 *   RGB565颜色，直接用于ST7789。
 *
 * font：
 *   TFT_eSPI内置字体编号。
 *   当前限制2~4，防止网页输入过大破坏固定布局。
 */
struct DisplayRowConfig {
  uint8_t metric;
  uint16_t color;
  uint8_t font;
};

/*
 * 整套屏幕配置。
 *
 * row[4]：
 *   四个右侧数据框。
 *
 * socColor：
 *   左侧大SOC数字颜色。
 *
 * tempColor / capacityColor：
 *   左下温度、容量区域颜色。
 *
 * socBar：
 *   是否显示底部SOC进度条。
 */
struct DisplayConfig {
  DisplayRowConfig row[4];

  uint16_t socColor;
  uint16_t tempColor;
  uint16_t capacityColor;

  bool socBar;

  // 使用默认值初始化，不读取Flash。
  void setDefaults();

  // 从Preferences读取；没有保存值时自动使用默认值。
  void load();

  // 将当前配置写入Preferences。
  void save() const;
};

/*
 * 全局显示配置。
 *
 * WebConfig：
 *   修改 + save()
 *
 * Display：
 *   load() + 读取
 */
extern DisplayConfig g_displayConfig;

/*
 * 将枚举值转换成网页和屏幕使用的中文名称。
 */
const char* displayMetricName(uint8_t metric);

#endif
