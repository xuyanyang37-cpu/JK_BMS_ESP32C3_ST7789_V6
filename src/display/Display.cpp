#include "Display.h"
#include "../font/FontGB2312.h"
#include <math.h>
#include <stdint.h>

/*
 * JK BMS + ST7789 1.9" / 320x170
 * UI 2.0 - 按设计图重新做像素级布局。
 *
 * 主界面（参考 ESP32E28 风格并适配 320x170）：
 *   顶部：BMS / BMS名称 / 蓝牙图标
 *   左：SOC 仪表卡 + 温度 / 剩余容量
 *   右：4 个可配置数据卡
 *   底：SOC 渐变条
 *
 * 主界面只显示电池运行数据，不显示连接状态文字。
 */

namespace {
  // 设计图的深蓝色圆角卡片。
  static const uint16_t UI_PANEL      = 0x0948;
  static const uint16_t UI_PANEL_DARK = 0x0127;
  static const uint16_t UI_HEADER     = 0x061F;

  static const uint16_t UI_WHITE   = TFT_WHITE;
  static const uint16_t UI_VOLTAGE = 0xFFE0; // 明黄色
  static const uint16_t UI_CURRENT = 0x07E0; // 亮绿色
  static const uint16_t UI_MIN_VOLTAGE = 0xFCA8; // 柔和红色
  static const uint16_t UI_RANGE   = 0x2F3C; // 青色
  static const uint16_t UI_TEMP    = 0x07E0;

  // 设计图：低电量可调颜色阈值。
  static const float SOC_WARN_THRESHOLD = 30.0f;
  static const float SOC_CRITICAL_THRESHOLD = 15.0f;

  // 16bit RGB565 颜色线性插值。
  static uint16_t lerp565(uint16_t a, uint16_t b, uint16_t percent) {
    if (percent > 100) percent = 100;

    uint8_t ar = (a >> 11) & 0x1F;
    uint8_t ag = (a >> 5)  & 0x3F;
    uint8_t ab = a & 0x1F;

    uint8_t br = (b >> 11) & 0x1F;
    uint8_t bg = (b >> 5)  & 0x3F;
    uint8_t bb = b & 0x1F;

    uint8_t rr = ar + (((int16_t)br - (int16_t)ar) * percent) / 100;
    uint8_t rg = ag + (((int16_t)bg - (int16_t)ag) * percent) / 100;
    uint8_t rb = ab + (((int16_t)bb - (int16_t)ab) * percent) / 100;

    return ((uint16_t)rr << 11) | ((uint16_t)rg << 5) | rb;
  }

  static uint16_t socColor(float soc) {
    if (soc <= SOC_CRITICAL_THRESHOLD) return TFT_RED;
    if (soc <= SOC_WARN_THRESHOLD) return TFT_YELLOW;
    return UI_WHITE;
  }

  static void drawPanel(TFT_eSprite& sprite,
                        int16_t x, int16_t y,
                        int16_t w, int16_t h) {
    sprite.fillRoundRect(x, y, w, h, 7, UI_PANEL);
  }

  static void drawMetricRow(TFT_eSprite& sprite,
                            char icon,
                            const String& label,
                            const String& value,
                            uint16_t color,
                            uint8_t font) {
    sprite.fillSprite(TFT_BLACK);
    sprite.fillRoundRect(0, 0, 168, 32, 7, UI_PANEL);

    // 右侧四项统一布局：固定位置、可由网页选择指标。
    sprite.drawCircle(15, 15, 10, color);
    sprite.setTextColor(color, UI_PANEL);
    sprite.drawCentreString(String(icon), 15, 6, 2);
    sprite.drawCentreString(String(icon), 16, 6, 2);

    FontGB2312::drawText(sprite, 31, 6,
                         label, color, UI_PANEL, 1);
    FontGB2312::drawText(sprite, 32, 6,
                         label, color, UI_PANEL, 1);

    sprite.setTextColor(color, UI_PANEL);
    sprite.drawRightString(value, 163, 0, font);
    sprite.drawRightString(value, 164, 0, font);
  }
}

// [显示辅助] 只有数值变化达到阈值才刷新对应区域，减少闪烁。
bool Display::changed(float a, float b, float eps) const {
  return fabsf(a - b) >= eps;
}

// [显示流程1] ST7789初始化 -> Sprite创建 -> 开启背光 -> 首次整屏绘制。
void Display::begin() {
  // ST7789 初始化期间保持背光关闭，避免初始化过程中的白屏/闪屏。
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, LOW);

  // 初始化 ST7789 控制器。
  tft_.init();
  tft_.setRotation(1);

  // 控制器初始化完成后先清黑屏，再开启背光。
  tft_.fillScreen(TFT_BLACK);
  delay(30);
  digitalWrite(TFT_BL, HIGH);

  // 关键内存优化：
  // 上电/扫描/BLE连接阶段不创建任何长期 Sprite。
  // 原版本这里一次申请约 40KB 的 RGB565 缓冲区，容易和 NimBLE/WiFi
  // 同时启动时形成连续 RAM 压力。
  // 主界面真正显示时才由 ensureDashboardBuffers() 懒加载。
  initialized_ = true;
  firstDashboard_ = true;
  drawFullPage(g_bmsData);
}

bool Display::ensureDashboardBuffers() {
  if (dashboardBuffersReady_) return true;

  socSprite_.setColorDepth(16);
  leftInfoSprite_.setColorDepth(16);
  rowSprite_.setColorDepth(16);
  barSprite_.setColorDepth(16);

  if (!socSprite_.createSprite(140, 91)) return false;
  if (!leftInfoSprite_.createSprite(140, 36)) {
    socSprite_.deleteSprite();
    return false;
  }
  if (!rowSprite_.createSprite(168, 36)) {
    leftInfoSprite_.deleteSprite();
    socSprite_.deleteSprite();
    return false;
  }
  if (!barSprite_.createSprite(312, 12)) {
    rowSprite_.deleteSprite();
    leftInfoSprite_.deleteSprite();
    socSprite_.deleteSprite();
    return false;
  }

  socSprite_.fillSprite(TFT_BLACK);
  leftInfoSprite_.fillSprite(TFT_BLACK);
  rowSprite_.fillSprite(TFT_BLACK);
  barSprite_.fillSprite(TFT_BLACK);

  dashboardBuffersReady_ = true;
  return true;
}

// [显示流程2] 主循环每500ms调用。这里决定“整屏画”还是“局部刷新”。
void Display::update(const BmsData& d) {
  if (!initialized_) return;

  // 扫描阶段单独处理：
  // 第一次进入扫描页才整页绘制，后续 1/3 -> 2/3 -> 3/3
  // 只刷新进度条和次数，不再 fillScreen()/整页 pushSprite()。
  if (d.bootState == BOOT_SCANNING || d.bootState == BOOT_START) {
    bool force = !scanScreenInitialized_ || lastBootState_ != d.bootState;
    drawScanningScreen(d, force);
    scanScreenInitialized_ = true;
    lastBootState_ = d.bootState;
    lastData_ = d;
    return;
  }

  // 离开扫描页时，允许下一次扫描重新初始化静态区域。
  scanScreenInitialized_ = false;

  if (d.bootState != lastBootState_ ||
      d.hotspot != lastData_.hotspot) {
    drawFullPage(d);
    lastBootState_ = d.bootState;
    lastData_ = d;
    return;
  }

  if (d.bootState != BOOT_CONNECTED && !d.online) {
    if (d.statusMessage != lastData_.statusMessage ||
        d.scanAttempt != lastData_.scanAttempt ||
        d.hotspotIp != lastData_.hotspotIp ||
        d.mac != lastData_.mac) {
      drawFullPage(d);
      lastData_ = d;
    }
    return;
  }

  if (displayConfigChanged()) {
    // 网页保存后无需重启：重新绘制受配置影响的局部区域。
    drawSoc(d);
    drawConfiguredRow(0, d);
    drawConfiguredRow(1, d);
    drawConfiguredRow(2, d);
    drawConfiguredRow(3, d);
    drawTemperature(d);
    if (g_displayConfig.socBar) drawSocBar(d);
    lastDisplayConfig_ = g_displayConfig;
  } else {
    drawDashboard(d, firstDashboard_);
  }
  firstDashboard_ = false;
  lastDisplayConfig_ = g_displayConfig;
  lastData_ = d;
}

void Display::drawScanningScreen(const BmsData& d, bool force) {
  // 参考 ESP32E28 的启动页原则：
  // 只用 LCD 原生绘图，不创建任何 Sprite / framebuffer。
  // 这样 BLE 扫描刚启动时不会因为显示缓存抢占连续 RAM。
  if (force) {
    tft_.fillScreen(TFT_BLACK);

    // 大号启动标题，模拟参考项目的“启动仪表页”层次。
    tft_.setTextColor(TFT_CYAN, TFT_BLACK);
    tft_.drawCentreString("JK BMS", 160, 18, 4);

    tft_.setTextColor(TFT_WHITE, TFT_BLACK);
    tft_.drawCentreString("ESP32-C3", 160, 52, 2);

    // 中央状态卡：纯几何图形，无 Sprite。
    tft_.drawRoundRect(35, 78, 250, 38, 8, TFT_DARKGREY);
    tft_.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft_.drawCentreString("SCANNING BLUETOOTH", 160, 88, 2);

    tft_.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft_.drawCentreString("JK BMS", 160, 132, 2);
  }

  // 只覆盖进度条内部，不重绘整个启动页。
  tft_.fillRect(43, 111, 234, 5, TFT_BLACK);

  int progress = (d.scanAttempt * 100) / 3;
  if (progress > 100) progress = 100;
  if (progress < 0) progress = 0;

  int filled = 230 * progress / 100;
  if (filled > 0) {
    tft_.fillRoundRect(45, 111, filled, 5, 2, TFT_BLUE);
  }

  tft_.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft_.drawCentreString(String(d.scanAttempt) + "/3", 160, 150, 2);
}

void Display::drawFullPage(const BmsData& d) {
  tft_.fillScreen(TFT_BLACK);
  firstDashboard_ = (d.online || d.bootState == BOOT_CONNECTED);
  g_displayConfig.load();
  lastDisplayConfig_ = g_displayConfig;

  if (d.bootState == BOOT_SCANNING ||
      d.bootState == BOOT_START) {
    // 上电扫描页禁止 320x170 全屏 Sprite。
    // 参考 ESP32E28：启动阶段只显示轻量状态页，等真正拿到数据后
    // 才创建仪表盘缓存，避免 BLE 初始化时瞬间吃掉大块连续 RAM。
    drawScanningScreen(d, true);
    return;
  }

  if (d.bootState == BOOT_CONNECTING) {
    // 连接页也禁止 320x170 全屏 Sprite。
    // BLE 连接阶段堆内存最紧张，此时申请约 109KB 连续 RAM 容易失败，
    // 随后进入热点时又要启动 WiFi，可能表现为重启。
    // 这里先直接清黑屏，再使用几个小 Sprite。
    tft_.fillScreen(TFT_BLACK);

    TFT_eSprite title(&tft_);
    title.setColorDepth(16);
    if(title.createSprite(320, 32)){
      title.fillSprite(TFT_BLACK);
      FontGB2312::drawCenterString(title, 160, 4,
                                   "正在连接",
                                   TFT_YELLOW, TFT_BLACK, 2);
      title.pushSprite(0, 5);
      title.deleteSprite();
    }

    TFT_eSprite mac(&tft_);
    mac.setColorDepth(16);
    if(mac.createSprite(320, 28)){
      mac.fillSprite(TFT_BLACK);
      FontGB2312::drawCenterString(mac, 160, 4,
                                   d.mac.length() ? d.mac : "JK-BMS",
                                   TFT_WHITE, TFT_BLACK, 1);
      mac.pushSprite(0, 47);
      mac.deleteSprite();
    }

    TFT_eSprite hint(&tft_);
    hint.setColorDepth(16);
    if(hint.createSprite(320, 28)){
      hint.fillSprite(TFT_BLACK);
      FontGB2312::drawCenterString(hint, 160, 4,
                                   "正在建立蓝牙连接...",
                                   TFT_WHITE, TFT_BLACK, 1);
      hint.pushSprite(0, 82);
      hint.deleteSprite();
    }
    return;
  }

  if ((d.bootState == BOOT_HOTSPOT || d.hotspot) &&
      !d.online) {
    // 热点页禁止申请 320x170 的全屏 Sprite。
    // 320x170x16bit 约需要 109KB 连续 RAM；连续 BLE 扫描三次后，
    // 堆内存可能已经碎片化，导致 createSprite() 失败，表现为
    // “背光亮但屏幕没有界面”。
    // 改为：背景直接清屏，中文/英文分别使用小尺寸局部 Sprite。
    tft_.fillScreen(TFT_BLACK);

    TFT_eSprite title(&tft_);
    title.setColorDepth(16);
    if(title.createSprite(320, 32)){
      title.fillSprite(TFT_BLACK);
      FontGB2312::drawCenterString(title, 160, 3,
                                   "热点设置",
                                   TFT_YELLOW, TFT_BLACK, 2);
      title.pushSprite(0, 3);
      title.deleteSprite();
    }

    TFT_eSprite wifi(&tft_);
    wifi.setColorDepth(16);
    if(wifi.createSprite(320, 28)){
      wifi.fillSprite(TFT_BLACK);
      FontGB2312::drawCenterString(wifi, 160, 3,
                                   "WiFi: JK-BMS-SETUP",
                                   TFT_WHITE, TFT_BLACK, 1);
      wifi.pushSprite(0, 40);
      wifi.deleteSprite();
    }

    TFT_eSprite hint(&tft_);
    hint.setColorDepth(16);
    if(hint.createSprite(320, 28)){
      hint.fillSprite(TFT_BLACK);
      FontGB2312::drawCenterString(hint, 160, 3,
                                   "手机连接后打开网页",
                                   TFT_CYAN, TFT_BLACK, 1);
      hint.pushSprite(0, 69);
      hint.deleteSprite();
    }

    TFT_eSprite ip(&tft_);
    ip.setColorDepth(16);
    if(ip.createSprite(320, 30)){
      ip.fillSprite(TFT_BLACK);
      FontGB2312::drawCenterString(
          ip, 160, 3,
          d.hotspotIp.length() ? d.hotspotIp : "192.168.4.1",
          TFT_CYAN, TFT_BLACK, 2);
      ip.pushSprite(0, 98);
      ip.deleteSprite();
    }

    TFT_eSprite bottom(&tft_);
    bottom.setColorDepth(16);
    if(bottom.createSprite(320, 28)){
      bottom.fillSprite(TFT_BLACK);
      FontGB2312::drawCenterString(bottom, 160, 3,
                                   "扫描 / 选择 / 连接电池",
                                   TFT_LIGHTGREY, TFT_BLACK, 1);
      bottom.pushSprite(0, 133);
      bottom.deleteSprite();
    }
    return;
  }

  drawDashboard(d, true);
}

void Display::drawDashboard(const BmsData& d, bool force) {
  // 只有真正进入数据仪表盘才申请长期 Sprite。
  if (!ensureDashboardBuffers()) {
    // 内存不足时保留轻量启动页，不强行申请大块连续 RAM。
    tft_.fillRect(0, 145, 320, 25, TFT_BLACK);
    tft_.drawCentreString("LOW MEMORY", 160, 150, 2);
    return;
  }

  if (force) {
    tft_.fillScreen(TFT_BLACK);

    // 顶部区域不用 Sprite，直接绘制，避免又申请一块 15KB 左右的缓存。
    tft_.fillRoundRect(4, 2, 312, 20, 5, UI_HEADER);
    tft_.setTextColor(TFT_YELLOW, UI_HEADER);
    tft_.drawString("BMS", 10, 4, 1);

    String name = d.deviceName.length() ? d.deviceName : "JK BMS";
    if (name.length() > 12) name = name.substring(0, 12);
    tft_.setTextColor(TFT_WHITE, UI_HEADER);
    tft_.drawCentreString(name, 160, 4, 2);

    const uint16_t bt = d.online ? TFT_CYAN : TFT_DARKGREY;
    tft_.drawLine(300, 5, 300, 19, bt);
    tft_.drawLine(300, 5, 306, 10, bt);
    tft_.drawLine(306, 10, 300, 15, bt);
    tft_.drawLine(300, 15, 306, 20, bt);
    tft_.drawLine(306, 20, 300, 15, bt);

    drawSoc(d);
    drawConfiguredRow(0, d);
    drawConfiguredRow(1, d);
    drawConfiguredRow(2, d);
    drawConfiguredRow(3, d);
    drawTemperature(d);
    if (g_displayConfig.socBar) drawSocBar(d);
    return;
  }

  if (changed(d.soc, lastData_.soc, 0.5f)) {
    drawSoc(d);
    if (g_displayConfig.socBar) drawSocBar(d);
    else tft_.fillRect(4, 156, 312, 14, TFT_BLACK);
  }

  for (uint8_t i = 0; i < 4; ++i) {
    if (metricChanged(g_displayConfig.row[i].metric, d, lastData_)) {
      drawConfiguredRow(i, d);
    }
  }

  if (changed(d.remainingCapacityAh, lastData_.remainingCapacityAh, 0.1f) ||
      changed(d.temperature1, lastData_.temperature1, 0.1f) ||
      changed(d.temperature2, lastData_.temperature2, 0.1f) ||
      changed(d.mosTemperature, lastData_.mosTemperature, 0.1f)) {
    drawTemperature(d);
  }
}

void Display::drawSoc(const BmsData& d) {
  socSprite_.fillSprite(TFT_BLACK);
  // 左侧 SOC 主卡片：参考 ESP32E28 的层次感，但保持 V6 的 320x170 结构。
  drawPanel(socSprite_, 0, 0, 140, 91);

  socSprite_.setTextColor(TFT_LIGHTGREY, UI_PANEL);
  socSprite_.drawCentreString("SOC", 68, 5, 2);

  String value = String(d.soc, 0);
  // SOC颜色由网页配置决定；低电量的数值判断仍保留在业务层，
  // 如果以后需要“低电量自动变红”，可以在这里叠加阈值规则。
  uint16_t color = g_displayConfig.socColor;

  socSprite_.setTextColor(color, UI_PANEL);
  socSprite_.drawCentreString(value, 68, 18, 7);
  socSprite_.drawCentreString(value, 69, 18, 7);

  socSprite_.setTextColor(color, UI_PANEL);
  socSprite_.drawString("%", 106, 67, 4);
  socSprite_.drawString("%", 107, 67, 4);

  // 半圆式强调环，模拟参考界面的仪表感。
  const int16_t cx = 70;
  const int16_t cy = 51;
  const int16_t radius = 43;
  socSprite_.drawArc(cx, cy, radius, radius - 3, 210, 330, UI_PANEL_DARK, UI_PANEL, false);
  const int16_t endAngle = 210 + (static_cast<int16_t>(d.soc) * 120) / 100;
  if (endAngle > 210) {
    socSprite_.drawArc(cx, cy, radius, radius - 3, 210, endAngle, color, UI_PANEL, false);
  }

  socSprite_.pushSprite(4, 26);
}

void Display::drawTemperature(const BmsData& d) {
  leftInfoSprite_.fillSprite(TFT_BLACK);

  // 左下只放“温度 + 容量”，整体高度 36px。
  // 该区域顶部 y=118，与右侧第四行“剩余里程”完全对齐。
  leftInfoSprite_.fillRoundRect(0, 0, 68, 36, 7, UI_PANEL);
  leftInfoSprite_.fillRoundRect(72, 0, 68, 36, 7, UI_PANEL);

  // 温度卡片：小标题 + 数值。
  const uint16_t tempColor = g_displayConfig.tempColor;
  const uint16_t capacityColor = g_displayConfig.capacityColor;

  FontGB2312::drawText(leftInfoSprite_, 5, 3,
                       "温度", tempColor, UI_PANEL, 1);
  FontGB2312::drawText(leftInfoSprite_, 6, 3,
                       "温度", tempColor, UI_PANEL, 1);

  String temp = String(d.temperature1, 0) + "C";
  leftInfoSprite_.setTextColor(tempColor, UI_PANEL);
  leftInfoSprite_.drawCentreString(temp, 34, 15, 2);
  leftInfoSprite_.drawCentreString(temp, 35, 15, 2);

  // 容量卡片：小标题 + 剩余容量。
  FontGB2312::drawText(leftInfoSprite_, 77, 3,
                       "容量", capacityColor, UI_PANEL, 1);
  FontGB2312::drawText(leftInfoSprite_, 78, 3,
                       "容量", capacityColor, UI_PANEL, 1);

  String cap = String(d.remainingCapacityAh, 1) + "Ah";
  leftInfoSprite_.setTextColor(capacityColor, UI_PANEL);
  leftInfoSprite_.drawCentreString(cap, 106, 15, 2);
  leftInfoSprite_.drawCentreString(cap, 107, 15, 2);

  // 局部刷新：与右侧第四行保持同一基线。
  leftInfoSprite_.pushSprite(4, 126);
}

bool Display::displayConfigChanged() const {
  for(uint8_t i=0;i<4;i++){
    if(g_displayConfig.row[i].metric != lastDisplayConfig_.row[i].metric ||
       g_displayConfig.row[i].color != lastDisplayConfig_.row[i].color ||
       g_displayConfig.row[i].font != lastDisplayConfig_.row[i].font) return true;
  }
  return g_displayConfig.socColor != lastDisplayConfig_.socColor ||
         g_displayConfig.tempColor != lastDisplayConfig_.tempColor ||
         g_displayConfig.capacityColor != lastDisplayConfig_.capacityColor ||
         g_displayConfig.socBar != lastDisplayConfig_.socBar;
}

/*
 * 根据“当前网页选择的指标”判断该指标是否发生变化。
 * 这样 Display 不需要每次刷新四个框，而是只刷新真正发生变化的框。
 * 参数：a=本次BmsData，b=上一次BmsData。
 */
/*
 * 根据当前网页选择的指标判断数据是否发生变化。
 * 只有对应指标变化时才刷新该行，避免每500ms重复绘制。
 */
bool Display::metricChanged(uint8_t metric, const BmsData& a, const BmsData& b) const {
  switch (metric) {
    case DISPLAY_VOLTAGE: return changed(a.totalVoltage, b.totalVoltage, 0.01f);
    case DISPLAY_CURRENT: return changed(a.current, b.current, 0.01f);
    case DISPLAY_POWER: return changed(a.power, b.power, 1.0f);
    case DISPLAY_MIN_VOLTAGE: return changed(a.minCellVoltage, b.minCellVoltage, 0.001f);
    case DISPLAY_MAX_VOLTAGE: return changed(a.maxCellVoltage, b.maxCellVoltage, 0.001f);
    case DISPLAY_AVG_VOLTAGE: {
      float aa=0, bb=0; uint8_t ac=0, bc=0;
      for(uint8_t i=0;i<JK_MAX_CELLS;i++){ if(a.cellVoltage[i]>0.1f){aa+=a.cellVoltage[i];ac++;} if(b.cellVoltage[i]>0.1f){bb+=b.cellVoltage[i];bc++;} }
      if(ac) aa/=ac; if(bc) bb/=bc;
      return changed(aa,bb,0.001f);
    }
    case DISPLAY_DELTA_VOLTAGE: return changed(a.deltaCellVoltage, b.deltaCellVoltage, 0.001f);
    case DISPLAY_CELL_COUNT: return a.cellCount != b.cellCount;
    case DISPLAY_MOS_TEMP: return changed(a.mosTemperature, b.mosTemperature, 0.1f);
    case DISPLAY_TEMP1: return changed(a.temperature1, b.temperature1, 0.1f);
    case DISPLAY_TEMP2: return changed(a.temperature2, b.temperature2, 0.1f);
    case DISPLAY_REMAINING_CAPACITY: return changed(a.remainingCapacityAh, b.remainingCapacityAh, 0.1f);
    case DISPLAY_TOTAL_CAPACITY: return changed(a.totalCapacityAh, b.totalCapacityAh, 0.1f);
    case DISPLAY_REMAINING_RANGE: return changed(a.remainingRangeKm, b.remainingRangeKm, 0.1f);
    case DISPLAY_SOC: return changed(a.soc, b.soc, 0.5f);
    case DISPLAY_CYCLE_COUNT: return a.cycleCount != b.cycleCount;
    default: return true;
  }
}

/*
 * 将网页配置中的一个指标转换成固定右侧行。
 * index 0~3 对应 y=4/42/80/118，屏幕结构永远不改变。
 * 这里只负责“取值+格式化+局部推屏”，不修改BmsData。
 */
/*
 * 将一个网页配置指标转换为固定位置的右侧数据框。
 * index=0/1/2/3，对应屏幕Y=4/42/80/118。
 * 布局固定，网页只能改变内容、颜色和字体。
 */
void Display::drawConfiguredRow(uint8_t index, const BmsData& d) {
  if(index >= 4) return;
  const DisplayRowConfig& cfg = g_displayConfig.row[index];
  String label = displayMetricName(cfg.metric);
  String value;
  char icon = 'V';

  switch(cfg.metric) {
    case DISPLAY_VOLTAGE: value=String(d.totalVoltage,2)+"V"; icon='V'; break;
    case DISPLAY_CURRENT: value=String(d.current,1)+"A"; icon='A'; break;
    case DISPLAY_POWER: value=String(d.power,0)+"W"; icon='W'; break;
    case DISPLAY_MIN_VOLTAGE: value=String(d.minCellVoltage,3)+"V"; icon='V'; break;
    case DISPLAY_MAX_VOLTAGE: value=String(d.maxCellVoltage,3)+"V"; icon='V'; break;
    case DISPLAY_AVG_VOLTAGE: {
      float sum=0; uint8_t count=0;
      for(uint8_t i=0;i<JK_MAX_CELLS;i++){ if(d.cellVoltage[i]>0.1f){sum+=d.cellVoltage[i];count++;} }
      value=String(count ? sum/count : 0.0f,3)+"V"; icon='V'; break;
    }
    case DISPLAY_DELTA_VOLTAGE: value=String(d.deltaCellVoltage*1000.0f,0)+"mV"; icon='D'; break;
    case DISPLAY_CELL_COUNT: value=String(d.cellCount)+"S"; icon='C'; break;
    case DISPLAY_MOS_TEMP: value=String(d.mosTemperature,0)+"C"; icon='T'; break;
    case DISPLAY_TEMP1: value=String(d.temperature1,0)+"C"; icon='T'; break;
    case DISPLAY_TEMP2: value=String(d.temperature2,0)+"C"; icon='T'; break;
    case DISPLAY_REMAINING_CAPACITY: value=String(d.remainingCapacityAh,1)+"Ah"; icon='A'; break;
    case DISPLAY_TOTAL_CAPACITY: value=String(d.totalCapacityAh,1)+"Ah"; icon='A'; break;
    case DISPLAY_REMAINING_RANGE: value=String(d.remainingRangeKm,0)+" KM"; icon='R'; break;
    case DISPLAY_SOC: value=String(d.soc,0)+"%"; icon='S'; break;
    case DISPLAY_CYCLE_COUNT: value=String(d.cycleCount); icon='C'; break;
    default: value=String(d.totalVoltage,2)+"V"; label="电压"; icon='V'; break;
  }

  drawMetricRow(rowSprite_, icon, label, value, cfg.color, cfg.font);
  static const int16_t y[4]={26,59,92,125};
  rowSprite_.pushSprite(148, y[index]);
}

void Display::drawVoltage(const BmsData& d) {
  drawMetricRow(rowSprite_, 'V', "电压",
                String(d.totalVoltage, 2) + "V",
                UI_VOLTAGE, 4);
  rowSprite_.pushSprite(148, 26);
}

void Display::drawCurrent(const BmsData& d) {
  drawMetricRow(rowSprite_, 'A', "电流",
                String(d.current, 1) + "A",
                UI_CURRENT, 4);
  rowSprite_.pushSprite(148, 59);
}

void Display::drawMinVoltage(const BmsData& d) {
  // 第三行按实物仪表布局显示“最低电压”，不再显示功率。
  drawMetricRow(rowSprite_, 'V', "最低",
                String(d.minCellVoltage, 3) + "V",
                UI_MIN_VOLTAGE, 4);
  rowSprite_.pushSprite(148, 92);
}

void Display::drawRange(const BmsData& d) {
  rowSprite_.fillSprite(TFT_BLACK);
  rowSprite_.fillRoundRect(0, 0, 168, 36, 7, UI_PANEL);

  FontGB2312::drawText(rowSprite_, 8, 7,
                       "剩余里程",
                       UI_RANGE, UI_PANEL, 1);
  FontGB2312::drawText(rowSprite_, 9, 7,
                       "剩余里程",
                       UI_RANGE, UI_PANEL, 1);

  rowSprite_.setTextColor(UI_RANGE, UI_PANEL);
  rowSprite_.drawRightString(
      String(d.remainingRangeKm, 0) + " KM",
      163, 1, 4);
  rowSprite_.drawRightString(
      String(d.remainingRangeKm, 0) + " KM",
      164, 1, 4);

  rowSprite_.pushSprite(148, 125);
}

void Display::drawSocBar(const BmsData& d) {
  barSprite_.fillSprite(TFT_BLACK);

  float ratio = d.soc / 100.0f;
  if (ratio < 0.0f) ratio = 0.0f;
  if (ratio > 1.0f) ratio = 1.0f;

  const int16_t w = 312;
  const int16_t h = 12;
  int filled = (int)(w * ratio + 0.5f);

  for (int16_t x = 0; x < w; ++x) {
    if (x >= filled) {
      barSprite_.drawFastVLine(x, 0, h, UI_PANEL_DARK);
      continue;
    }

    uint16_t color;

    if (w <= 1) {
      color = TFT_GREEN;
    } else {
      uint16_t p = (uint16_t)((x * 100L) / (w - 1));

      if (p <= 50) {
        color = lerp565(TFT_GREEN, TFT_YELLOW,
                        (uint16_t)(p * 2));
      } else {
        color = lerp565(TFT_YELLOW, TFT_RED,
                        (uint16_t)((p - 50) * 2));
      }
    }

    barSprite_.drawFastVLine(x, 0, h, color);
  }

  barSprite_.drawRoundRect(0, 0, w, h, 3, TFT_DARKGREY);
  barSprite_.pushSprite(4, 157);
}
