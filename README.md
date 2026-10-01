# JK_BMS_ESP32C3_ST7789_V6

ESP32-C3 + ST7789 1.9-inch JK BMS display project.

Modular BMS protocol architecture with JK02_24S / JK02_32S support.


## BMS 蓝牙协议支持

### 1. 极空 JK
- BLE Service: FFE0
- BLE data channel: FFE1（部分设备也暴露 FFE2）
- JK02 帧头：55 AA EB 90
- 实时数据帧：0x02
- 支持 JK02_24S / JK02_32S
- 300 字节帧、低字节序、末字节校验和。
- 公开协议分析与 ESPHome 实现可交叉验证。 

### 2. 蚂蚁 ANT
- BLE Service: FFE0
- Characteristic: FFE1
- 帧头：7E A1
- 状态帧：0x11
- 设备信息：0x12
- 帧尾：AA 55
- CRC16/Modbus，低字节在前。
- 已加入 `src/protocol/ant/AntProtocol.cpp/.h`，并在 BLE 连接后主动发送 ANT 状态探测；收到 ANT 帧后由协议管理器自动切换。

### 3. 铁塔换电
“铁塔换电柜 V1.1/V2.0”是 JK BMS 可选的 RS485/UART 通信协议，公开资料能够确认协议名称和寄存器体系，但不能据此直接推导某一块“铁塔保护板”的 BLE 私有帧格式。当前项目不伪造 BLE 字段。

如果实际铁塔保护板使用 JK 的 BLE 通道，则其 BLE 实时帧仍可由 JK 解析器处理；如果是独立厂家的 BLE 模块，需要该保护板的 BLE 通知原始数据（十六进制抓包）后再建立独立解析器。

### 协议证据
- ANT：公开的 ESPHome ANT-BMS 实现记录了 FFE0/FFE1、7E A1、0x11 状态帧、CRC16 和字段偏移。
- JK：公开的 JK BLE 协议分析记录了 55 AA EB 90、0x02 实时帧、FFE0/FFE1 和 JK02 24S/32S。
- 铁塔：公开的 JK 文档/开源集成确认 UART1 存在 China Tower shared battery cabinet V1.1/V2.0，但这是 RS485/UART 侧协议，不等同于 BLE 私有协议。
