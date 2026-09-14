# Qt 串口 IAP 演示上位机

这是 STM32F407ZGT6 UART IAP 演示工程的电脑端。界面使用代码创建，不依赖 `.ui` 文件；同一套源码兼容 Qt 5.12+ 与 Qt 6。

## 功能

- 刷新并选择串口，按 `115200 8-N-1` 连接。
- 选择最大 256 KiB 的 APP `.bin`，计算 CRC-32/IEEE。
- 发送前检查 `.bin` 的初始 MSP、Thumb 位和按 `0x08040000` 链接的 Reset Handler 范围。
- 输入 32 位版本号并执行 `HOLD → START → DATA → END → BOOT`。
- DATA 数据最多 256 字节，包含 32 位序号和偏移。
- 每帧等待 ACK 1500 ms；超时重发相同帧，最多重发 3 次。
- 校验 ACK/NACK 返回的下一序号、期望偏移和错误码。
- 支持取消、进度显示和带时间戳日志。取消时尽力发送一次 `ABORT`，但不等待其 ACK。

## 协议 v1

所有多字节协议字段均为大端：

```text
[command u16][total_length u16][data N][reserved u32=0][CRC16 u16]
```

- `total_length` 包含整帧。
- CRC16 为 CRC-16/CCITT-FALSE：多项式 `0x1021`、初值 `0xFFFF`、不反转、无最终异或；覆盖 CRC 字段之前的所有字节。
- START data：`target=0xFF`（设备自动选择非活动槽）、`version u32`、`size u32`、`CRC32/IEEE u32`。
- DATA data：`sequence u32`、`offset u32`、最多 256 字节固件数据。
- ACK/NACK data：`next_sequence u32`、`expected_offset u32`、`error u16`。
- CRC32 为软件 CRC-32/IEEE：多项式反射值 `0xEDB88320`、初值/最终异或均为 `0xFFFFFFFF`。它不是 STM32F4 CRC 外设的默认算法。

设备端必须保证重复 DATA 幂等：ACK 丢失后，上位机会原样重发同一 DATA，设备只能重发当前 ACK，不能再次推进写入位置。

## 构建

当前仓库使用的 Qt 6.8.3/MinGW 构建方式：

```powershell
cd E:\IAP\stm32f407_uart_iap_demo\host\qt_updater
.\build_qt.ps1
```

脚本直接调用以下绝对路径，不修改系统 PATH：

- `D:\Qt\Tools\CMake_64\bin\cmake.exe`
- `D:\Qt\Tools\Ninja\ninja.exe`
- `D:\Qt\Tools\mingw1310_64\bin\gcc.exe` / `g++.exe`
- `D:\Qt\6.8.3\mingw_64`

脚本依次配置、构建、运行 `windeployqt`，最后以 `--smoke-test` 启动程序并检查 CRC 固定向量及 Qt 事件循环。通用 CMakeLists 仍会优先查找 Qt 6，找不到时回退到 Qt 5.12+。

也可用 MinGW 对应的 Qt Kit，但编译器必须与 Qt 安装包匹配。运行时需要部署 `Widgets` 和 `SerialPort` 依赖；Windows 发布可使用对应 Kit 的 `windeployqt`。

## 使用

1. 在 Keil 中将 APP 链接到 `0x08040000`，生成连续 `.bin`。
2. 连接开发板 CH340 对应串口，选择固件并填写版本号。
3. 点击“开始升级”，以设备返回的 ACK 和最终板上行为判断结果。

如果开发板已经在运行 APP，请先按复位键，再在 Bootloader 的 3 秒等待窗口内点击“开始升级”。升级完成后，APP 的文本心跳会继续显示在上位机日志区。

## 证据边界与限制

- `[源码证据]`：本目录包含协议编码、CRC、ACK 状态机、超时重发和程序化 UI。
- `[PC模拟]`：2026-09-05 已使用 `D:\Qt\6.8.3\mingw_64`、Qt SerialPort、MinGW 13.1、CMake 和 Ninja 完成 Release 构建；`windeployqt` 部署后，隐藏运行 `--smoke-test` 返回 0。该检查覆盖 QApplication/界面对象创建、事件循环、CRC 固定向量和公共错误码映射。
- `windeployqt` 报告未找到可选的 `dxcompiler.dll`/`dxil.dll`，但本 Widgets 程序的启动烟雾测试通过；若以后加入依赖 DirectX Shader Compiler 的界面功能，需要重新部署核对。
- 未连接 STM32F407ZGT6 开发板，不能标记为 `[开发板实测]` 或宣称升级成功。
- `QSerialPort::write()` 成功只表示数据进入本机发送缓冲；进度只在设备 ACK 的序号和偏移与预期一致后推进。
- 固定协议没有 magic/SOF 字段；接收端只能根据已知命令、长度和 CRC 尝试重新同步，抗噪能力受限。
- ACK/NACK 没有携带“被确认的命令”或事务 ID；极端情况下，同一控制帧超时重发产生的迟到重复 ACK 无法与下一控制帧 ACK 完全区分。正式协议应增加关联字段或在设备端保证每个停等事务只形成一个有效响应。
- 当前一次把整个 `.bin` 读入内存，不支持断点续传、固件签名、防降级或自动发现设备。
- BOOT ACK 仅证明设备接受了 BOOT 命令；APP 是否真正进入 `Reset_Handler/main`，仍需串口日志、CMSIS-DAP 和寄存器观察证明。
