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
- 短按依次选择：频段 → 频率 → 模式 → BW → AGC → SM → 步进 → 音量 → 亮度 → 频段。
- 选择频段、模式、步进或音量后旋转修改。切换频段保留该频段频率、模式、步进；停止调节两秒后写入 NVS，重启恢复。
- 频率焦点长按 650 ms 切换手动/SEEK，SEEK 中频率变为亮紫色，旋转方向决定向前/向后搜台；音量焦点长按切换静音并保留音量。长按松开不会再触发短按。
- 搜台中短按取消搜索，以芯片回读的当前位置作为手动频率，焦点继续停在频率，不进入下一个焦点。
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

## SEEK

使用 FM_SEEK_START / AM_SEEK_START，WRAP=0，仅搜索当前频段；AM/FM 支持，LSB/USB 不开放。FM 搜索栅格为 50/100/200 kHz（根据当前步进选择芯片支持的值），AM 按 1–10 kHz 栅格搜索。FM 有效台门限为 RSSI 20 dBμV / SNR 3 dB，AM 为 25 dBμV / 5 dB。

每 100 ms 回读搜索进度，达到电台或频段边缘停止；30 s 超时则取消并保留当前位置，不作为致命错误。搜索中短按立即取消并回到手动调谐；同方向连续旋转不排队跳台，反向旋转先取消再反向搜索。芯片搜索期间界面和按键继续更新。

USB 调试命令 `e` / `h` 分别调用与短按/长按相同的动作处理，状态增加 `focus`（0–8 对应九个焦点）、`seek`、`searching`、`result` 字段。`result` 为 0 未搜索、1 搜索中、2 找到台、3 到边缘、4 用户取消、5 超时。

右侧亮度、音量、电量图标及百分比使用紧凑排列。只有音量焦点的长按执行静音；频率焦点长按切换 SEEK，其余焦点长按不执行额外动作。

信号区使用等高 RSSI 分段条，并在右侧显示芯片 RSSI（dBμV）与 SNR（dB）。BW 默认 FM `AUTO`、AM `3.0k`、SSB `2.2k`，可通过编码器修改；自动滤波不显示固定带宽。VALID 来自芯片频道有效性标志，AGC/ATT 后的数值是增益索引而非 dB；SM 选框显示配置的最大软静音衰减，Soft Mute 介入时 SM 选框的文字变为橙色，边框颜色继续表示焦点；P 表示 FM 导频检测；AFC! 表示 AFC 到达调节边缘，AFC OK 表示没有该异常。

MONO/立体声由音量图标表示：单道声波为 mono，双道声波为存在立体声混合，静音为叉号。状态来源于 FM RSQ 的 stereo blend 回读，AM/SSB 为 mono；尚未取得稳定质量状态时不画声波。图标反映接收芯片的解码状态，不改变内置单声道功放的硬件结构。静噪 SQL 尚未实现，Soft Mute 也不等同于 SQL 门限。

SEEK 版本验证：构建/刷入哈希校验、原生模型测试和 `test/seek_smoke.py` 均通过。实机验证 AM 正反向搜索、短按取消后保持频率焦点并以取消频率继续手动步进、LW/VHF 上限停止、SSB 拒绝 SEEK、仅音量焦点长按静音，以及切换配置后排队的 SEEK 能正常启动。FM 搜索返回过 100.6 MHz、VALID=1 的结果；后续信号变化会重新更新 VALID。按钮时序仍需用户实际长按体验，测试通过 USB 调用同一动作处理。

```sh
# 重启设备后运行（会改变频率及状态，最后回到 VHF/FM 100 MHz）
~/.platformio/penv/bin/python test/seek_smoke.py /dev/cu.usbmodem21101
```


## 可调接收参数与主显示区

模式与顶部频段使用相同的 26×17 直角、无填充选框；BW、AGC、SM 选框与模式在同一行，旋钮当前焦点显示黄色。模式之后依次聚焦 BW、AGC、SM，再到步进、音量、亮度；手动频率焦点也使用黄色，SEEK 模式使用亮紫色。RSSI 等高信号条、数值、SNR 与 VALID 在同一行，STEP 使用直角无填充选框，位于 RSSI 行最右侧，AFC、实际 RF 增益索引、FM 导频 P 一并紧凑放在同一行；数字与单位间不插空格。

| 参数 | FM | AM | LSB / USB |
| --- | --- | --- | --- |
| BW | AUTO、40、60、84、110 kHz | 1、1.8、2、2.5、3、4、6 kHz | 0.5、1、1.2、2.2、3、4 kHz |
| AGC | AUTO 或 ATT 0–26 | AUTO 或 ATT 0–37 | AUTO 或 ATT 0–37 |
| SM | OFF（0）至 31 dB | OFF（0）至 31 dB | OFF（0）至 31 dB |

带宽旋转按数值顺序循环；AGC 与 SM 在端点停止。ATT 数值是芯片增益索引，不是 dB；值越大衰减越大。SM 设置最大软静音衰减，不是 SQL 门限；本界面统一提供 0–31 dB 的范围。各解调模式分别保留参数，切换模式恢复，保存到 NVS 后重启恢复。首次启动默认 FM 的 SM 为 16 dB，AM/SSB 为 OFF。修改只写接收参数，不重载 SSB 补丁或重设频率。

FM 使用 FM_CHANNEL_FILTER / FM_AGC_OVERRIDE / FM_SOFT_MUTE_MAX_ATTENUATION；AM 使用 AM_CHANNEL_FILTER / AM_AGC_OVERRIDE / AM_SOFT_MUTE_MAX_ATTENUATION；SSB 使用补丁的 SSB_MODE / AM_AGC_OVERRIDE / SSB_SOFT_MUTE_MAX_ATTENUATION，保持 AVC 开启与 AFC 禁用，窄于等于 2 kHz 时 SBCUTFLT=0。来源：[Skyworks AN332](https://www.skyworksinc.com/-/media/Skyworks/SL/documents/public/application-notes/AN332.pdf)、[PU2CLR SSB 配置](https://pu2clr.github.io/SI4735/extras/apidoc/html/group__group17.html)。

红色指针下方始终显示当前模型频率及单位，SSB 保留 1 Hz 精度；主显示区与刻度尺统一使用 `src/radio/frequency.h` 的整数格式化，频率 ≥1 MHz 使用 MHz、其余使用 kHz，去除小数末尾零（例如 2300000 Hz → 2.3MHz，2300100 Hz → 2.3001MHz，999100 Hz → 999.1kHz），不舍弃有效 Hz 精度；仅避让附近的文字标签，不改变刻度间隔、连续基线或频段边界。

大字区域靠左显示频率，较小的 MHz/kHz 单位紧随数字并对齐底部；收到确认的 RDS PS 时显示电台名，收到 RT 时显示广播文本，两者都有则 PS 停留 5 秒后显示 RT，并留足一次完整滚动的时间。长文本按 35 ms/像素水平滚动，起止各停留 1.2 秒，裁剪在主显示区内，文字更新后从头开始。没有有效 RDS，或正在调谐/搜索时显示频率。

使用 LovyanGFX 自带 `efontCN_24` 中文字库渲染名称/文本，频率保持原来的大号数字。RDS 传输不是 UTF-8：当前解码器支持可打印 ASCII，其他字符显示 `?`；引入中文字库不等于已支持非标准中文 RDS 编码，后续需针对实际广播字符集补充转换。

RDS 在 FM 模式开启，每 100 ms 使用 STATUSONLY 查看 FIFO 后最多消费 4 组，避免空 FIFO 的旧块被当作新数据。PS 解码 0A/0B、RT 解码 2A/2B；同一 PI 下两次完整且相同的消息才发布。PI 更换、调频、开始/结束 SEEK、同步丢失或 FIFO 溢出时清除旧数据；RT A/B 标志或版本改变时单独清除旧 RT。接受 A/B/D（以及 2A 的 C）至多 1–2 个已纠正位错误；硬件 RDS_CONFIG=0x5501，四块均使用此门限。FIFO 和块定义参见 AN332 的 FM_RDS_STATUS / FM_RDS_CONFIG。

USB 调试补充：`>` / `<` 调用当前焦点的旋转操作；`bwsel`、`gainset`、`smset` 显示配置，`bw` 为驱动成功应用的带宽。`ps=[…]` 输出已确认的电台名。

新增测试：

```sh
clang++ -std=c++11 -Wall -Wextra -Werror -fsanitize=address,undefined -Isrc \
  test/native/rds_test.cpp -o /tmp/ats-mini-rds-test
/tmp/ats-mini-rds-test
clang++ -std=c++11 -Wall -Wextra -Werror -fsanitize=address,undefined -Itest/native/stubs -Isrc \
  test/native/driver_test.cpp src/drivers/si473x/si473x.cpp -o /tmp/ats-mini-driver-test
/tmp/ats-mini-driver-test
# 设备重启后运行，检查三种接收模式的参数设置及恢复
~/.platformio/penv/bin/python test/reception_smoke.py /dev/cu.usbmodem21101
```

原生测试覆盖 PS/RT 完整消息确认、乱序 PS、纠错门限、RT A/B/版本切换、PI 切换，以及真实驱动对模拟总线的命令字节、FIFO 最后一组/空 FIFO/同步丢失和调频/SEEK 清理。模拟总线不能证明实际 RF 收到 RDS；需在发送 RDS 的 FM 台上进一步验证。

2026-10-13 此版本验证：构建与刷入哈希校验通过；模型、RDS PS/RT、模拟总线驱动测试均通过。实机 `reception_smoke.py` 验证 FM/AM/SSB 带宽选择、AGC 自动/手动端点、SM 调节及模式间参数恢复，`seek_smoke.py` 验证新增焦点后原有 SEEK、取消、限频与长按静音交互。当前 RF 环境尚未取得有效 PS/RT，因此未宣称实机电台文本或中文广播解码已经验证。设备最终恢复 FM 100 MHz、音量 18、手动调谐。


## 掉电记忆

用户停止调节两秒，且调谐/SEEK 已结束后，将完整选择保存到 ESP32 NVS：当前频段，各可用频段的频率/模式/步进，音量、亮度、静音，以及各解调模式的 BW/AGC/SM。开机先加载，再设置背光和收音芯片；焦点从频段开始，SEEK 始终以手动状态启动。只切换焦点、RSSI/RDS 更新或重试芯片不会触发写入；选择没有变化也不重复写入。最后两秒内的尚未保存操作在立即断电时不会恢复。

NVS 资源在 `src/hal/settings_storage.*`；版本化的选择编码在 `src/radio/saved_settings.*`；模型通过 `state()` / `restore()` 交换每频段的语义值，不暴露内部步进索引。存储使用固定大小 96 字节、明确的小端字段、版本/频段数量与 CRC32；未知版本、损坏记录、不支持频段/模式、越界频率、非法步进或接收参数回退默认设置。写入失败每十秒重试，继续保留原来的已保存选择，不影响正常收音。

首次启动默认 VHF/FM 100 MHz，音量 18、亮度 128、未静音。正常固件更新不擦除 NVS 分区，因此兼容版本保留选择。原有按默认参数编写的硬件/SEEK/接收参数测试，需要默认测试配置；仅重启不再重置已保存选择。

```sh
clang++ -std=c++11 -Wall -Wextra -Werror -fsanitize=address,undefined -Isrc \
  test/native/persistence_test.cpp src/radio/model.cpp src/radio/saved_settings.cpp -o /tmp/ats-mini-persistence-test
/tmp/ats-mini-persistence-test
~/.platformio/penv/bin/python test/persistence_smoke.py /dev/cu.usbmodem21101
```

持久化实机测试会改变各频段记忆及接收参数并重启两次，最后将当前状态恢复为 VHF/FM 100 MHz、100 kHz 步进、音量 18、亮度 128、未静音、BW AUTO、AGC AUTO、SM 16 dB，其余频段保留测试记忆。原生测试覆盖精确 SSB 频率、多频段/模式参数往返、记录每一字节的损坏拒绝及失败恢复的原子性。实机测试覆盖重启恢复、跨频段恢复、模式接收参数恢复，以及连续调节期间不写入、停止两秒只写一次。USB 诊断 `brightness` / `stored` / `save` / `writes` 分别表示背光值、存在有效已保存记录、有待保存变化、本次启动写入次数。

AFC OK 表示芯片未报告频率校正达到调节边缘，AFC! 表示达到边缘，AFC -- 表示尚未取得稳定质量状态；SSB 的 AFC 由补丁配置禁用。P0/P1 是 FM 导频检测标志，RF 后的数值是芯片当前增益索引，不是 dB。SM 生效时只将选框文字改为橙色，边框继续使用黄色表示聚焦、青色表示普通选中值；不另外显示 SM*。

最终紧凑布局刷入后，已有 NVS 选择继续恢复，未被固件更新重置；主显示单位紧随频率数字，STEP 选框位于 RSSI 行最右侧，SEEK 与箭头使用亮紫色。
