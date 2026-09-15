# PC 模拟测试

运行：

```powershell
cd E:\IAP\stm32f407_uart_iap_demo\tests\pc_sim
.\run_tests.ps1
```

测试直接编译公共 CRC/帧代码和 Bootloader 的 `boot_service.c`，Flash、元数据与启动搬运由内存模型替代。覆盖正常升级、ACK 丢失后的重复包、CRC16 错帧、乱序与错误偏移、整包 CRC32 失败、下载中断、搬运中断和未确认回滚。

结果仅代表主机端逻辑检查。它不能证明 STM32F407ZGT6 的 Flash 时序、UART 电气连接、复位、VTOR/MSP 跳转或真实断电恢复。
