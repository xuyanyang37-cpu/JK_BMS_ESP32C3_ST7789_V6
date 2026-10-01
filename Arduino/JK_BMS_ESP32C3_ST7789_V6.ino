// JK_BMS_ESP32C3_ST7789_V6 - Arduino IDE 入口
// 主项目源码保持在 src/，本文件放在 Arduino/ 文件夹中。
// 直接使用 Arduino IDE 打开本 .ino 即可加载 V6 源码。

#include "../src/protocol/jk/Jk02_24S.cpp"
#include "../src/protocol/jk/Jk02_32S.cpp"
#include "../src/protocol/jk/JkProtocol.cpp"
#include "../src/protocol/ant/AntProtocol.cpp"
#include "../src/protocol/jbd/JbdProtocol.cpp"
#include "../src/protocol/daly/DalyProtocol.cpp"
#include "../src/protocol/BmsProtocolManager.cpp"
#include "../src/ble/BmsBle.cpp"
#include "../src/font/FontGB2312.cpp"
#include "../src/display/Display.cpp"
#include "../src/web/WebConfig.cpp"
#include "../src/main.cpp"
