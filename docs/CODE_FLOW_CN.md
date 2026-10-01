# JK_BMS_ESP32C3_ST7789_V6 业务流程说明

## 一、项目分层

主程序 main.cpp
  -> BmsBle：扫描、连接、BLE收包、发送查询
  -> BmsProtocolManager：识别协议、分发解析
  -> Display：只负责显示 g_bmsData
  -> WebConfig：只负责网页配网和参数设置

核心原则：
- BLE 不直接画屏幕。
- Display 不负责连接蓝牙。
- WebConfig 不负责解析 BMS 协议。
- Protocol 不负责 BLE。
- 模块之间通过 g_bmsData 交换业务数据。

## 二、开机顺序

1. setup()
2. Serial 和复位原因
3. 背光
4. 初始化 g_bmsData
5. display.begin()
6. display.update()
7. bmsBle.begin()
8. 最多执行 3 次：扫描 -> 连接 -> FFE0 -> 可写特征 -> 通知特征 -> 订阅 -> 请求数据 -> 等待有效帧
9. BLE连接成功且 g_bmsData.valid=true：正常运行
10. 三次失败：释放BLE -> WIFI_OFF -> WIFI_AP -> WebConfig

## 三、三次连接业务分支

开始
 ↓
扫描第 N/3 次
 ↓
找到候选？
 ├─ 否 -> 本轮失败
 └─ 是 -> 连接
            ↓
         找 FFE0？
          ├─ 否 -> 失败
          └─ 是 -> 找可写特征
                       ↓
                    找通知特征
                       ↓
                    订阅通知
                       ↓
                    发送查询
                       ↓
                  收到有效BMS帧？
                   ├─ 否 -> 4秒超时 -> 失败
                   └─ 是 -> 成功

注意：BLE GATT 连接成功不等于 BMS 数据连接成功。真正成功条件是 connected() 且 g_bmsData.valid。

## 四、三次失败后热点

第1次失败 -> 第2次
第2次失败 -> 第3次
第3次失败 -> releaseConnectionForHotspot()
                ↓
             关闭 WiFi
                ↓
             开启 SoftAP
                ↓
SSID：JK-BMS-SETUP
密码：12345678
IP：192.168.4.1
                ↓
             WebConfig

## 五、运行期 loop 顺序

1. bmsBle.loop()
2. webConfig.loop()
3. 每500ms执行 display.update()
4. delay(5)

BLE运行期：
- 已连接：按当前协议定时发送查询。
- TT：250ms请求一次。
- 其他协议：有效数据后约5秒请求一次；尚未有效时约1.5秒请求一次。
- 断线：非热点模式下约15秒尝试恢复。

## 六、BLE 收包

BMS Notify
 -> notifyCallback()
 -> handleNotification()
 -> RX缓冲区
 -> findFrameStart()
 -> frameLength()
 -> 判断完整帧
 -> parseFrame()
 -> 自动识别协议
 -> 更新 g_bmsData

为什么需要缓冲区？
因为一次 BLE Notify 可能只有半帧，也可能包含多个完整帧。

## 七、协议识别顺序

1. JK
2. ANT
3. JBD
4. Daly
5. TT

找到协议后：
activeProtocol_ = 对应协议
然后调用 parseFrame()。

## 八、JK 新版 4E 57

帧头：4E 57
byte[2..3]：body length
总长度：body length + 4

主要 Tag：

| Tag | 数据 |
|---|---|
| 79 | 单体电压 |
| 80 | MOS温度 |
| 81 | 温度1 |
| 82 | 温度2 |
| 83 | 总电压 |
| 84 | 电流 |
| 85 | SOC |
| 87 | 循环次数 |
| 88 | 总容量 |
| 89 | 报警 |
| 8A | 充电/放电/均衡 |
| A9 | 实际串数 |
| AA | 剩余容量 |
| BA | BMS名称 |

## 九、JK 旧版 55 AA EB 90

固定长度：300 bytes

流程：
55 AA EB 90
 -> 检查300字节
 -> 根据24S/32S选择offset
 -> 单体电压
 -> 总电压
 -> 电流
 -> 温度
 -> SOC
 -> 剩余容量
 -> 功率
 -> 剩余里程

## 十、剩余里程

公式：

Range = 剩余容量(Ah) × 总电压(V) ÷ 每公里耗电(Wh/km)

例如：
100Ah × 52V ÷ 100Wh/km = 52km

网页修改 Wh/km 后立即重新计算。

## 十一、Display 流程

Display 不读取 BLE。
Display 只读取 g_bmsData。

Display.update()
 -> 第一次？是：整屏绘制
 -> 第一次？否：检查数据是否变化
      -> 没变化：不刷新
      -> 有变化：只刷新对应 Sprite 区域

目的：减少 ST7789 整屏刷新的闪烁。

## 十二、主界面最终布局

┌──────────────────────────────┬──────────────────┐
│                              │      电压        │
│                              ├──────────────────┤
│             SOC              │      电流        │
│                              ├──────────────────┤
│                              │      功率        │
│                              ├──────────────────┤
├──────────────┬───────────────┼──────────────────┤
│     温度     │      容量      │    剩余里程      │
└──────────────┴───────────────┴──────────────────┘
████████████████████████████████████████████████████
                       SOC渐变条

右侧四个数据框：
- 宽168px
- 高36px
- Y：4 / 42 / 80 / 118

温度、容量、剩余里程：
- 同一行
- Y：118

## 十三、网页模式

手机
 -> JK-BMS-SETUP
 -> 192.168.4.1
 -> WebConfig

网页主要功能：
1. 查看状态
2. 扫描BLE
3. 选择设备
4. 连接
5. 保存MAC
6. 选择24S/32S
7. 设置Wh/km
8. 保存Preferences

## 十四、Preferences

命名空间：jkcfg

| Key | 含义 |
|---|---|
| mac | BMS MAC |
| mactype | MAC地址类型 |
| 32s | 24S/32S |
| whkm | 每公里耗电量 |

开机 BmsBle.begin() 会重新读取。

## 十五、故障排查入口

屏幕不显示：
Display.begin()
重点检查 TFT_BL、tft_.init()、setRotation()、Sprite。

扫描不到：
BmsBle::scanDevices()
重点检查候选设备判断、RSSI、FFE0、设备名称。

能连接但没有数据：
BmsBle::connectByAddress()
重点检查 FFE1/FFE2、write characteristic、notify characteristic、subscribe。
然后检查 handleNotification()。

收到数据但数值错误：
BmsProtocolManager -> JkProtocol -> parseNewTlvFrame() / parseOldFrame()。

三次失败后重启：
先看 Serial 的 ESP32 reset reason。
再看 HOTSPOT 前后的 heap。
重点检查 BLE资源释放、WiFi资源、String堆碎片、内存不足。

## 十六、以后修改原则

改屏幕 -> src/display/
改BLE -> src/ble/
改JK协议 -> src/protocol/jk/
增加新BMS -> 新增 src/protocol/xxx/，然后接入 BmsProtocolManager
改网页 -> src/web/

不要把协议解析塞进 main.cpp。
不要让 Display 直接操作 BLE。
不要让 WebConfig 直接解析 BMS 原始帧。

## 十七、最重要的数据流

BMS保护板
  ↓ BLE
BmsBle
  ↓ 原始字节
BmsProtocolManager
  ↓
具体协议解析器
  ↓
g_bmsData
  ├──> Display
  └──> WebConfig

以后遇到问题，先判断属于哪一段，再修改对应模块。
