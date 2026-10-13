# ATS Mini 收音机

Sunnygold SI4732 Mini Radio（PCB 1.2 基准），ESP32-S3 + SI4732。
[硬件、电路图与外观](docs/hardware.md) · [收音规格、操作和驱动说明](docs/radio.md)

默认程序支持 LW → MW → SW → VHF，AM/FM/LSB/USB 模式；AIR 显示为不可用。全部频段与模式同时列出，不合法选项置灰。

旋转连续调频，越过频段边缘进入上一/下一可用频段，整个范围在 153 kHz 和 108 MHz 停止；短按按频段、频率、模式、步进、音量、亮度循环选择，再旋转修改；长按静音。每个频段的频率与模式暂存在 RAM，重启后恢复默认。错误时短按重试。

## 构建、上传

```sh
pio run -e ats-mini
pio run -e ats-mini -t upload --upload-port /dev/cu.usbmodem21101
pio device monitor -p /dev/cu.usbmodem21101 -b 115200
```

端口需按 `pio device list` 的结果调整。电量示例保留在 `src/examples/battery_screen.cpp`；在 `platformio.ini` 的 `build_flags` 中添加 `-D BATTERY_DEMO` 可构建该示例。

## 代码结构

- `src/hal`：屏幕、背光、电池、中断编码器、按键、模拟音频控制、收音供电/复位/I2C。
- `src/drivers/si473x`：SI4732 命令与 SSB 补丁，频率入口统一为 Hz。
- `src/radio`：频段、合法模式、步进和频率记忆。
- `src/app`：收音界面与调谐调度。

USB 连接时电量 ADC 可能测到供电节点约 4.68 V，而非真实电芯电压。电压校准方法见硬件文档。
