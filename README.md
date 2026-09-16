# STM32F407ZGT6 UART IAP

基于 STM32F407ZGT6 的 UART 二级 Bootloader 演示工程，实现 A/B 镜像接收、校验、搬运、APP 启动确认与未确认版本回滚。APP 固定链接于 `0x08040000`，运行 FreeRTOS V9.0.0；上位机使用 Qt SerialPort。

## 项目流程

```text
UART HOLD → START → DATA(seq/offset/CRC16) → END(CRC32)
          → 非活动 A/B 槽 → READY → Run 区二次校验
          → PENDING → APP 启动 → CONFIRMED / 回滚
```

- 五区 Flash：Boot、Param、Run、Slot A、Slot B
- 参数记录采用代次号、CRC32 和独立提交标记
- 跳转前校验 MSP、Reset_Handler、Thumb 位和地址范围
- APP 健康运行 2 秒后确认新镜像；未确认版本复位后回退到已确认镜像

## 当前结果

| 检查 | 状态与范围 |
| --- | --- |
| Keil Bootloader / APP 构建 | 通过；当前记录分别为 12,804 bytes / 14,636 bytes |
| PC 协议与状态机模拟 | 6 组通过，覆盖 CRC、重复 ACK、错误帧/序号/偏移、整包 CRC 错误、下载中断模型和搬运/回滚模型 |
| 正常开发板升级 | 已完成 14,636-byte APP UART 升级、Run 激活、FreeRTOS 启动、健康确认及确认后复位恢复；单次传输约 3.855 s（约 3.80 KB/s） |
| 跳转寄存器检查 | 开发板实测 `VTOR=0x08040000`、`MSP=0x20005868`，APP 入口 `PC=0x080402E0` |
| 未确认版本回滚 | 开发板实测版本 4 未确认复位后回到已确认版本 1，并恢复 APP heartbeat |
| 会话/供电恢复 | 关闭 Qt 会话并重启目标端后重新连接，完成升级、健康确认和 heartbeat 恢复；不等同于拔开 UART RX/TX |

开发板截图、日志摘要和适用范围见 [实测记录](docs/evidence_and_test.md) 与 [`docs/evidence/`](docs/evidence/)。

## 目录

- `firmware/bootloader`：启动、协议服务、Flash、元数据、A/B 流程和 APP 跳转
- `firmware/app`：FreeRTOS 应用与启动确认
- `common`：协议、CRC 和存储接口
- `host/qt_updater`：Qt Widgets + Qt SerialPort 上位机
- `tests/pc_sim`：主机协议与状态机故障模拟
- `docs`：Flash 布局、接线和验证记录

## 构建与复验

```powershell
.\scripts\verify_all.ps1
```

也可分别运行 `scripts/build_keil.ps1 -Target All`、`tests/pc_sim/run_tests.ps1` 和 `host/qt_updater/build_qt.ps1`。需要安装 Keil MDK/ARMCC、Qt、CMake、Ninja 与 MinGW；仓库不包含这些工具或其本机构建输出。

## 尚未验证 / 设计限制

- DATA 写入期间、Run 搬运期间以及参数提交期间的真实掉电恢复尚未完成实板验证。
- R3 检查覆盖的是 Qt 会话关闭与目标端供电重启，不代表 UART RX/TX 物理断线测试。
- CRC 用于检测损坏，不提供身份认证；当前没有数字签名、安全启动、加密或防降级。
- 参数区写满后的自动压缩/掉电保护未实现；不能据此宣称量产级掉电安全。
