# STM32F407 UART IAP

面向现场固件升级与启动失败恢复的 STM32F407ZGT6 Bootloader 演示工程。

## 设计主线

```text
UART HOLD → START → DATA(序号/偏移/CRC16) → END(CRC32)
          → 非活动 Slot A/B → READY → Run 区二次校验
          → PENDING → APP 启动 → CONFIRMED / 回滚
```

- 五区 Flash：Boot、Param、Run、Slot A、Slot B
- APP 固定链接地址：`0x08040000`
- 跳转前校验 MSP、Reset_Handler、Thumb 位和地址范围
- 参数记录使用代次号、CRC32 和独立提交标记
- 主机端提供 Qt SerialPort 升级工具，测试端提供 PC 故障模拟

## 目录

- `firmware/bootloader`：启动、协议服务、Flash、元数据、A/B 流程和 APP 跳转
- `firmware/app`：FreeRTOS V9.0.0 应用与启动确认
- `common`：协议、CRC 和存储接口
- `host/qt_updater`：Qt Widgets + Qt SerialPort 主机工具
- `tests/pc_sim`：乱序、CRC、ACK 丢失、下载/搬运中断和未确认回滚测试
- `docs`：Flash 布局、接线与证据记录

## 验证边界

PC/编译验证覆盖 Keil 目标、Qt 工具和 6 组主要故障模拟。开发板记录覆盖一次约 14.6 KB 镜像升级，传输耗时约 3.86 s；真实 Flash、复位和 FreeRTOS 启动证据应以 `docs/evidence_and_test.md` 为准。这里不宣称安全启动、数字签名、加密或量产级掉电安全。

## 构建

```powershell
.\scripts\verify_all.ps1
.\tests\pc_sim\run_tests.ps1
```

Keil MDK、ARMCC 和 Qt 是外部工具链，仓库不提交本机构建产物、用户配置或商业软件安装包。
