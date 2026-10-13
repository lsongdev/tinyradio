# 收音功能与 SI4732 技术范围

## 可用频段与模式

[Skyworks SI4732-A10 规格简表](https://www.skyworksinc.com/-/media/Skyworks/SL/documents/public/data-shorts/Si4732-A10-short.pdf)列出的接收范围，作为当前产品频段边界：

| 顺序 | 频段 | 频率范围 | 可选模式 |
| --- | --- | --- | --- |
| 1 | LW | 153–279 kHz | AM / LSB / USB |
| 2 | MW | 520–1710 kHz | AM / LSB / USB |
| 3 | SW | 2.3–26.1 MHz | AM / LSB / USB |
| 4 | VHF | 64–108 MHz | FM |
| 5 | AIR | 118–137 MHz（航空通信参考） | 本硬件不可用，置灰 |

AIR 航空通信频率参考 [FAA AC 90-50D](https://www.faa.gov/regulations_policies/advisory_circulars/index.cfm/go/document.information/documentID/23091)。SI4732 的 SSB 补丁提供单边带解调，不能让 FM 射频前端接收 118–137 MHz，也不能让普通 AM 接收路径覆盖 VHF。短波航空通信属于 HF/SW，和 VHF AIR 是不同范围。

驱动 AM/SSB 编码入口允许 150 kHz–30 MHz（参见 [PU2CLR SI4735 项目](https://github.com/pu2clr/SI4735)的调谐范围及接口），而应用频段目前采用上表的规格范围；命令能表达的范围不等同于全部具有保证的射频性能。没有添加超出原厂广播范围的 CB/10m 预设。

## 操作

全部频段和模式同时列出，全部使用无填充轮廓；当前值为青色，不支持的项置灰并跳过。黄色直角边框或图标表示当前旋钮控制的项目；开机默认聚焦频段。亮度在音量之后加入焦点循环，选中时太阳图标与百分比变黄，旋转调节背光。

- 选择频率焦点后，旋转编码器连续调频；越过当前频段上限进入下一可用频段下限，越过下限进入上一可用频段上限；顺序为 LW ↔ MW ↔ SW ↔ VHF，整个范围在 LW 下限 153 kHz 与 VHF 上限 108 MHz 停止，不首尾相连，并跳过 AIR。跨段后恢复目标频段的模式与步进，累积多步输入按目标频段的步进继续。
- 短按依次选择：频段 → 频率 → 模式 → 步进 → 音量 → 亮度 → 频段。
- 选择频段、模式、步进或音量后旋转修改。切换频段保留该频段本次运行的频率、模式、步进；暂不写入 Flash，重启恢复默认值。
- 长按 650 ms 切换静音；长按松开不会再触发短按。
- FM 步进：10/50/100/200 kHz；AM：1/5/9/10 kHz；SSB：10/50/100/500/1000 Hz。
- 错误时保持静音并关闭功放，短按重置收音芯片并重试。

MW 默认 9 kHz 步进，从 999 kHz 起步，连续增减保持该栅格；切换步进不会强制吸附到新的频道栅格。跨越端点时进入相邻可用频段继续调谐。SSB 切回 AM 时去除不足 1 kHz 的细调量。

## 实现边界

- `src/hal` 负责 GPIO、供电、复位、I2C、屏幕、编码器、按键和模拟音频控制。屏幕维持实机确认的 rotation=3 与色序。编码器用中断采样并在主循环读取累积完整周期，不在 ISR 执行 I2C。
- `src/drivers/si473x` 自行实现芯片命令与错误检查，接受 `TwoWire&`，不依赖板级 GPIO。探测 0x11/0x63，所有频率 API 用 Hz，频率结果在 STC 完成并回读一致后才确认。CTS 命令等待限时 500 ms，调谐完成限时 3 s；HAL 将单次 I2C 事务限制为 20 ms。
- `src/radio/model.*` 维护频段、合法模式、步进与运行内记忆。
- `src/app/receiver.cpp` 实现菜单与调谐。切换频段/模式先合并 180 ms 内的操作，再配置芯片；调谐期间继续累积输入，并在当前调谐完成后发送最新频率。屏幕在内存中绘制后一次推送，降低闪烁。

FM 使用芯片 10 kHz 单位、50 μs 去加重；AM/SSB 载频使用 1 kHz 单位。AM 设置 3 kHz 滤波器，SSB 设置 2.2 kHz 滤波器并关闭 AFC；SW 使用天线调谐电容值 1，LW/MW 使用自动值 0。音量范围 0–63，默认 18。

进入 SSB 时先采用 `carrier_kHz = floor(hz/1000)`、`BFO = -(hz % 1000)`。连续调谐时在当前载频 ±8 kHz 范围内只修改 `BFO = -(hz - carrier_kHz*1000)`，避免每过一个 kHz 都重新等待载频调谐；超出窗口才重新设定载频。这里只保证编码可表达该细调量，不宣称未经校准的晶振具有 10 Hz 绝对频率精度。模式切换时先静音，收到确认频率后再开启音频。

协议参考：[Skyworks AN332](https://www.skyworksinc.com/-/media/Skyworks/SL/documents/public/application-notes/AN332.pdf)、[PU2CLR SSB 接口与补丁加载](https://pu2clr.github.io/SI4735/extras/apidoc/html/group__group17.html)、[社区 BFO 换算](https://github.com/esp32-si4732/ats-mini/blob/ab4f5dc8120767cbb134b70096929f74c49df3e7/ats-mini/ats-mini.ino)。

## SSB 补丁来源

`src/drivers/si473x/patches/ssb.h` 原样复制自 [PU2CLR 的压缩补丁文件](https://github.com/pu2clr/SI4735/blob/123eccc730dc03c0ccc28526cbea6e305da7d2fc/src/patch_ssb_compressed.h)，内容名为 `amrx_6_0_1_ssbrx_patch_init_0xA902`。保留了 Vadim Afonkin 发布及 Ricardo Caratti 转换的署名和说明。补丁不属于 PU2CLR 库代码授权，不能把库许可证推断为补丁商业授权。

驱动查询并记录内部 library ID，加载后检查 GET_REV 的 patch ID=0xA902；不匹配则报错并保持静音。文件名中的 `amrx_6_0_1` 是接收组件版本，不能当作 library ID=6 的要求；本机返回 library ID=0，AM 组件版本为 6.0。由 FM/AM 切入 SSB 时重新加载补丁，同为 SSB 的 LSB/USB 切换保留已加载补丁。掉电、复位或离开 SSB 后必须重新加载。

## 验证

原生模型测试：

```sh
clang++ -std=c++11 -Wall -Wextra -Werror -Isrc \
  test/native/model_test.cpp src/radio/model.cpp -o /tmp/ats-mini-model-test
/tmp/ats-mini-model-test
```

覆盖不可用 AIR、跨频段模式限制、边界、大幅调节、步进、频率记忆与 BFO 符号/单位。

USB 单字符诊断（不依赖菜单焦点）：`0/1/2/3` 选择 LW/MW/SW/FM；`a/l/u/f` 选择 AM/LSB/USB/FM（不合法则拒绝）；`+/-` 按步进调频；`[/]` 跳到当前频段下限/上限；`m` 静音；`?` 打印状态；`r` 重置并重试。这些接口用于调试，不替代编码器。

串口的 `requested` 是目标频率，`confirmed` 是已确认的调谐结果；`error=OK` 不证明该频率上有电台，也不证明音频、天线灵敏度或 SSB 听感已验证。

实机命令测试脚本（会改变频段和模式，最后返回 VHF/FM 65.6 MHz）：

```sh
~/.platformio/penv/bin/python test/hardware_smoke.py /dev/cu.usbmodem21101
```

需要 pyserial；该测试验证控制链路，不验证实际接收电台或声音。

2026-10-13 验证结果：构建和上传哈希校验通过；`model_test` 和 `hardware_smoke.py` 均通过。实机完成 LW 198 kHz、MW 999 kHz、SW 7100 kHz、FM 100 MHz 的调谐回读，SSB 补丁 ID 回读为 0xA902；验证 USB/LSB 切换、100 Hz 调节、跨越 ±8 kHz 窗口重设载频、反向 BFO、离开 SSB 后重新加载补丁、拒绝 FM 频段上的 USB，以及静音状态切换。测试结束后设备停在 FM 100 MHz、音量 18、未静音。

界面更新：顶部左侧频段为 LW/MW/SW/VHF/AIR，右侧显示亮度、音量与电量百分比。VHF 是频段标签，FM 是解调模式。接 USB 时不能可靠估计真实电芯电量，因此电量百分比显示 `--%`。RSSI 用十级信号条显示，底部刻度以当前步进为间隔、每五格一个主刻度，围绕当前频率连续移动，并在频段边缘显示相邻频段的频率（青色刻度），使用 M/k 区分单位。移除了旋钮操作文字提示。

状态栏/刻度尺与跨频段版本已编译通过。原生测试覆盖各频段上下限、模式与步进恢复、大量输入、批量输入与逐格调谐的一致性。

最终状态栏/刻度尺与跨频段版本已刷入，写入哈希校验和实机测试通过。实机验证 LW 279 kHz → MW 520 kHz、MW 1710 kHz → SW 2300 kHz、SW 26100 kHz → VHF 64 MHz，以及反向切换；VHF 上限继续向上停在 108 MHz，LW 下限继续向下停在 153 kHz。跨段同步模式与目标频段步进。

刻度尺超过全局端点的区域留空，避免重复绘制端点刻度；频段和模式选框使用直角，保持无填充样式。

刻度尺保持等间距、连续的刻度与基线，跨频段两侧使用普通刻度标签显示 `LW`、`MW` 等名称，与数字标签处于同一行；不增加间隙或分隔线。最小端与最大端仍不首尾相连。
