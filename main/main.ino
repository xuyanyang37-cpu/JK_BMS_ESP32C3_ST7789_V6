// Arduino IDE 兼容入口。
// PlatformIO 直接编译 src/main.cpp；Arduino IDE 使用本文件时，
// 将模块源码按依赖顺序包含进来，保持 V6 单工程结构。
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
