# N32G452VE / GP21 / GC9307C 开发与上板说明

本说明对应当前工程：N32G452VE，内部 HSI 经 PLL 得到 128 MHz，GP21 外部 5 MHz 参考输入，GC9307C 240×320 竖屏。代码中的引脚是可修改的默认分配，不代表已核实的 PCB 接线。

## 构建和调试

在工程根目录执行：

```powershell
./tools/check_env.ps1
./tools/run_tests.ps1
./tools/build.ps1
./tools/build.ps1 -Preset rtthread-lvgl-release
./tools/test_linker.ps1
```

脚本仅临时调整自身进程的 DLL 搜索顺序，退出时恢复 PATH。没有修改系统 PATH、STM/NXP/DSP 工具链或其他工程的设置。直接使用 CMake 时选择 `rtthread-lvgl`；旧裸机/GP22 预设已清理。

VS Code 的 Ctrl+Shift+B 默认构建 GP21 + LVGL 调试版。首次使用 CMake Tools 时选择 `rtthread-lvgl` 配置预设；IntelliSense 从实际编译配置读取宏和头文件。F5 选择 `N32 GP21 + LVGL (HSI / J-Link)`。GDB/J-Link 路径在 `.vscode/settings.json` 中配置。当前调试入口由 Cortex-Debug 管理本次调试服务器，旧任务不会再结束全系统同名服务器进程。

生成目录为 `build/rtthread-lvgl/` 或 `build/rtthread-lvgl-release/`，包含 ELF、BIN、HEX、MAP 和 `compile_commands.json`。Flash 起始地址 `0x08000000`。J-Link 入口已配置且本机器件列表包含 N32G452VE，但本次未连接探针、烧录或调试真实芯片。旧 OpenOCD STM32 兼容脚本已移除。

| 预设 | 用途 | 时钟 |
| --- | --- | --- |
| `rtthread-lvgl` | 当前完整工程，含调试信息 | HSI PLL 128 MHz |
| `rtthread-lvgl-release` | 当前完整工程，体积优化 | HSI PLL 128 MHz |
| `rtthread` | RT-Thread + GP21，无 LVGL 界面 | HSI PLL 128 MHz |

工具版本：ARM GCC 13.3.0、CMake 4.0.0、Ninja 1.13.2。本工程预设格式要求 CMake ≥3.25。沿用本地 Nations SDK 2.6.0、SDK 自带 RT-Thread **3.1.4**、LVGL **8.3.11**，没有重新拼装一套假的 RT 设备 API。

构建按官方 GCC 教程的启动、链接、宏和产物要求落实，Makefile 的调度由 CMake/Ninja 承担。默认构建自动执行 31 项 ELF 检查；`test_linker.ps1` 额外测试错误镜像与链接断言。内存边界及与官方模板的差异详见 [OFFICIAL_GCC_AUDIT.md](OFFICIAL_GCC_AUDIT.md)。

## 引脚和资源分配

修改 `inc/board_config.h` 后重新构建。SPI/PWM 的硬件复用必须符合 N32 引脚表；任意 GPIO 并不能替代固定的外设复用通道。

| 功能 | 默认引脚 / 外设 | 配置说明 |
| --- | --- | --- |
| 控制台 | USART1，PA9 TX、PA10 RX | 115200，8N1 |
| SPI3 | PC3 SCK、PA0 MISO、PA1 MOSI | 默认无重映射，Mode 1，MSB first，8 bit |
| GP21 CSN | PC4 | 硬件 SPI 之外的软件片选 |
| GP21 INTN | PD13 | 低有效，PIN 下降沿中断，EXTI13 |
| GP21 RSTN | PB7 | 可设 `BSP_TDC_RESET_PIN=-1`，由外部电路复位 |
| GP21 EN_START / EN_STOP1 | 默认 `-1` | 表示外部保持高电平；接 GPIO 时填 PIN 宏编号 |
| GP21 参考输入 | 外部 5 MHz 接 GP21 XIN（高速参考输入） | 不由 MCU HSI 生成，也不是 SPI SCK |
| GC9307C D0…D15 | PE0…PE15 | 同一完整 GPIO 端口，一一对应 |
| LCD WR / RS / CS | PD5 / PD11 / PD7 | GPIO 模拟 8080 |
| LCD RESET / RD | PD6 / PD4 | RD 在写操作中保持高 |
| LCD 背光 | PD3 | 与 RD 分离，控制模组背光使能/驱动管 |
| PREV / NEXT / ENTER | PC5 / PC6 / PC7 | 上拉输入，按下接地 |
| PWM | TIM3 CH3 PB0、CH4 PB1；TIM4 CH1 PD12 | TIM4 重映射，PD13 留给 TDC |
| 硬件定时器 | TIM6 → `timer6` | 不与 PWM 或 SysTick 共用 |

MCU HSI 不需要外部主晶振；GP21 仍需要用户指定的 5 MHz 参考。GP21 的 START/STOP1 是外部数字测量信号，直接连接相应 GP21 输入。驱动不会自动产生被测脉冲。供电、电平、参考时钟接法和模组接口选择以器件/模组资料为准。

LCD 模组须选择 **8080-I、16 位接口，IM[3:0]=0001**。驱动不读取 LCD ID，因此初始化函数成功只说明配置合法且已发送序列，不能证明面板实际接通。端口整写用于提速，数据端口不能与控制引脚或其他使用中的外设共享。驱动检查自身控制脚重叠，但不会自动证明整个 PCB 的所有复用关系。

`BSP_LCD_MADCTL` 可调整镜像/色序；当前仅支持 240×320 竖屏，MV 位不能置位。`BSP_LCD_INVERT` 控制反显。`board/gc9307c_panel.h` 独立保存电源和 gamma 基线参数，取自本地 GC9307C 手册的复位值；获得模组厂商参数后应在这里调整并实测。

## 设备结构和接口

```text
应用 proc ── rt_device_read/control(tdc0)
               └─ GP21 worker / FIFO / 配置和诊断
                    ├─ RT SPI device spi30 → spi3 → SPI3
                    └─ RT PIN IRQ → EXTI13
应用 LVGL ── flush → rt_device_control(lcd0) → GPIO 8080 → GC9307C
按键扫描 ── RT PIN → 手势识别 → LVGL encoder / focus group
呼吸灯   ── RT PWM → pwm3 / pwm4
计时用户 ── RT hwtimer → timer6
```

| 设备名 | 框架 / 接口 | 实现 |
| --- | --- | --- |
| `pin` | 原生 `rt_pin_ops`，读写、模式、上下沿 IRQ | `board/drv_gpio.c` |
| `usart1` | 原生串口设备，控制台与 shell | `board/drv_usart.c` |
| `pwm3` / `pwm4` | 原生 `rt_device_pwm` / `rt_pwm_*` | `board/drv_pwm.c` |
| `timer6` | 原生 `rt_hwtimer_t` | `board/drv_hwtimer.c` |
| `spi3` / `spi30` | 原生 SPI bus/device | `board/drv_spi.c` |
| `tdc0` | `RT_Device_Class_Sensor`，独占打开 | `board/drv_gp21.c` |
| `lcd0` | `RT_Device_Class_Graphic`，RGB565 | `board/drv_lcd_device.c` |

PIN 编号由 `BSP_PIN('D', 13)` 表示。相同位号的 EXTI 线不能同时绑定两个端口；驱动拒绝占用冲突，禁用单线时不会关掉仍被其他线使用的共享 NVIC 中断。

PWM 参数单位为 ns，驱动量化为 1 µs，周期范围 1…65535 µs；支持 0% 和 100%。TIM3 的两个通道共用周期，另一通道正在输出时不能通过本通道改变周期，否则返回 `-RT_EBUSY`。通过 RCC 获取实际定时器时钟，避免 HSI 128 MHz 配置下使用旧的 72 MHz 常数。改变硬件通道需要同时调整驱动支持的通道和板级复用配置。

TIM6 支持单次/周期、频率设置、计数读取、停止和到期回调。设置频率须整除当前 TIM6 时钟且处于可分频范围。超过单个 16 位周期的计时由 RT hwtimer 框架分段。SDK hwtimer `INFO_GET` 分支缺少 `break` 的问题在构建目录生成的副本中修复，原厂库未改写。

### GP21

默认是**数字输入、校准后的测量范围 2、一次 START 到一次 STOP1**，CLKHS 常开，自动校准，结果为 `RES0`。不是 GP22 配置，也不自动执行超声发射、模拟前端温度补偿或范围 1 多击测量。若以后切换这些模式，需要相应扩展配置验证与结果解释，不能只改寄存器而沿用本模式的数据处理。

默认寄存器（含可读回的低 8 位 ID 字节）：

```text
REG0 020668A0  REG1 210200A1  REG2 A00000A2  REG3 380000A3
REG4 200000A4  REG5 000000A5  REG6 000000A6
```

SPI 为 Mode 1（CPOL=0、CPHA=1），总线时钟默认 2 MHz。写命令和四个数据字节在同一片选内发送；读操作的命令和接收也保持同一次片选。片选释放后保留至少 50 ns 间隔，实际电气时序仍需仪器验证。

`open()` 执行复位、七个寄存器写入、ID 字节和寄存器高字节读回测试。设备不在线时打开失败，应用每 2 秒重试。初始化/配置均由设备封装，应用不直接操作 SPI。

`GP21_CTRL_START` 发送 INIT，等待外部 START/STOP；INTN 到来后，驱动线程读状态和 RES0，验证状态后放入 16 条记录 FIFO，再重新 INIT。100 ms 内无完成信号时计数超时并重新等待，**不把上一次 RES0 当作新样本**。FIFO 满时丢弃最旧记录并统计 dropped。异常状态、超范围值或不符合预期的击数保留原始值但标为无效，`time_ps=0`。SPI 重置/重新启动失败会停止采集，统计可查询，恢复后可用 `tdc start` 再启动。

`rt_device_read()` 非阻塞，长度和返回值均为**字节数**，每条 `gp21_sample_t` 含 sequence、tick、raw、time_ps、status、valid。不足一条大小返回 0。回调 `rx_indicate` 在 GP21 驱动线程中调用，应只唤醒消费者，避免阻塞采集。应用已有独占消费者，不应从其他线程再次打开并争抢样本。

| control 命令 | 参数 | 条件 |
| --- | --- | --- |
| `GP21_CTRL_GET_CONFIG` | `gp21_config_t *` | 查询参考频率和寄存器 |
| `GP21_CTRL_SET_CONFIG` | `gp21_config_t *` | 停止采集；仅允许当前数字范围 2 语义支持的配置 |
| `GP21_CTRL_SELFTEST` | NULL | 打开且停止状态，读回测试 |
| `GP21_CTRL_START` / `STOP` | NULL | 开始/取消等待；STOP 后 FIFO 已有记录仍可读 |
| `GP21_CTRL_GET_STATS` | `gp21_stats_t *` | IRQ、样本、无效、超时、SPI 错误、丢弃及运行状态 |

校准后的 16.16 结果换算：`time_ps = raw / 65536 × divider / reference_hz × 10^12`。5 MHz、分频 1 时，raw=0x000A0000 表示 **2 µs**。实现使用 64 位整数拆分运算避免溢出；UI 再把 ps 换成 µs。内部 HSI 不参与这个 TDC 结果比例，但参考输入频率设置必须与实际信号一致。

### LCD 与 LVGL

GC9307C 地址参数按高、低两个 8 位参数发送，像素以一个 16 位 RGB565 总线周期发送，整个矩形传输保持 CS 有效。`lcd0` 用互斥锁串行化访问，`RTGRAPHIC_CTRL_GET_INFO` 返回 240×320、16 bpp、无整屏 framebuffer；矩形刷新使用 `LCD_CTRL_BLIT`，彩条使用 `LCD_CTRL_COLORBARS`。

LVGL 使用 240×40 单缓冲（19200 字节），同步写完后立即 `lv_disp_flush_ready()`，无虚假的 DMA 完成回调。所有界面创建/刷新由同一 LVGL 线程完成，shell 的 demo和彩条命令仅提交请求。显示设备初始化失败时保留采集和串口，不再创建依赖显示的控件。

UI 参考 elec-cdemo 的菜单、父页面返回、焦点和控件导航思路，重写为 LVGL：主页、测量/控件页、设备状态页。状态页是进入时的快照。PREV/NEXT 切焦点；ENTER 点击或进入编辑；长按 ENTER 离开编辑；双击 ENTER 返回主页。长按再短按切显示倍率，短按再长按暂停数值刷新。组合判断有 300 ms 等待窗口。开关控制呼吸灯，CLEAR 清应用显示统计，倍率仅影响显示，不改变驱动原始数据。

## 上板验证顺序

1. 核对宏与接线，确认 SWD、串口、屏幕总线和 TDC 引脚没有复用冲突，再使用调试入口下载当前固件。
2. 串口 115200 查看启动日志，执行 `list_device`、`list_thread`、`selftest`、`free`。应看到上表设备，线程栈无溢出。`selftest` 为板上内核/IPC 自检，本次未实机执行。
3. 执行 `timer_test`：应看到周期回调至少三次，单次回调一次，结果为 0。PWM 可用 `led breath off`、`led set 0 50` 检查 PB0 为约 1 kHz、50% 占空比；再检查其他两路。
4. `lcd_test` 显示从上至下红、绿、蓝、白、黑各 64 行彩条。核对最底行 319、色序、方向和全幅覆盖。`lcd_test off` 回到 UI，再检查三个按键和返回导航。需实际模组确认 gamma、反显和背光。
5. TDC 先确保 EN_START/EN_STOP1 有效、5 MHz 参考存在。串口执行 `tdc stop`、`tdc selftest`、`tdc start`。ID 测试应为 0。无外部脉冲时允许 timeouts 增长，但 samples 不应伪增长。
6. 给 GP21 输入已知时间差的 START/STOP1，例如 2 µs；UI 应接近 2.000 µs（倍率 x1），samples 随转换增长。撤去 STOP 测试超时；检查恢复、重复停止/启动、长时间运行与 FIFO dropped。量程边界和精度需以 GP21 手册、参考源和实测为准。
7. 示波器/逻辑分析仪检查 SPI 模式、片选间隔及 LCD WR。GC9307C 手册要求写周期 ≥66 ns、WR 高/低 ≥15 ns、数据建立/保持 ≥10 ns。代码估算值不代表实测帧率或电气验证结论。

## 资料与历史文件

资料索引见 `SOURCES.md`。原始压缩包保留，参考工程按需展开到短路径；完整 SHA256 和路径映射在 `references/manifest.json`。旧 README、旧移植文档与配置已归档到 `zip/history/workspace-before-cleanup-20261004.zip`，整理范围与恢复方法见 `CLEANUP.md`。当前型号、时钟、引脚、接口以本说明和源代码为准。
