# 官方 GCC 教程对照与链接审查

审查日期：2026-10-04。工程沿用原 DSH 工程的目录和部分 CMake/板级代码；本次按原厂资料重新核对启动、链接与工具配置，没有把“原来能编译”当作正确性依据。

## 采用的原始材料

- `zip/5.zip`：外层目录标为 GCC Development Environment V3.1.0，实际内含中英文 **Universal MCU GCC Development V1.2.0**。已阅读其中中文教程全部 18 页；本地同内容文件为 [官方 GCC 教程](CN_AN_Universal_MCU_GCC_Development_V1.2.0.pdf)。教程以 N32G033 为例，并非可直接用于 N32G452 的成品工程。
- `zip/11.zip`：原厂 SDK 2.6.0 中 `projects/n32g45x_EVAL/applications/GCC_demo/GCC/Makefile`、`firmware/CMSIS/device/startup/startup_n32g45x_gcc.s`、`firmware/CMSIS/device/n32g45x_flash.ld`。
- [N32G452 数据手册](CN_DS_N32G452_Series_Datasheet_V2.5.0.pdf)：PDF 第 11 页（印刷页 8）图 2-1、2.2.1、2.2.2，核对 Flash、SRAM 和 R-SRAM 地址。

## 逐项落实

| 教程位置 | 官方要求或示例 | 当前工程对应 |
| --- | --- | --- |
| 第 3–5 页，第 2、3 章 | VS Code、GNU ARM 工具、构建工具、J-Link | 本机 ARM GCC 13.3.0、CMake 4.0.0、Ninja 1.13.2、J-Link 9.32；工作区配置，不改全局 PATH |
| 第 7 页，4.1 | Makefile 组织源文件、宏、编译链接 | 按用户要求由 CMake + Ninja 实现相同 GCC 编译/链接流程；未额外保留会产生不同配置的 Makefile |
| 第 7 页，4.2 | 器件配套 GCC 启动汇编 | 从 N32G45x 官方启动结构派生 `board/startup_n32g45x_rtthread.s`，保留全部 102 项向量和 data/BSS 初始化；进入 RT `entry()` |
| 第 7 页，4.3；第 15 页，7.1 | 按实际型号修改 Flash、SRAM、栈顶、堆栈大小 | 使用 `linker/n32g452_rtthread.ld.in`，由 CMake 容量配置生成每个构建目录的 `.ld`；C 代码读取链接符号 |
| 第 8 页，4.4 | 打印重映射 | RT 控制台/串口设备输出；`src/syscalls.c` 提供 newlib USART1 输出和有界 `_sbrk` |
| 第 9–10 页，第 5 章 | 输出 ELF/BIN/HEX、下载和清理 | 三种预设均生成 ELF/BIN/HEX/MAP；默认构建自动验证 ELF；VS Code F5 使用 J-Link；构建输出集中于 `build/` |
| 第 11–14 页，第 6 章 | GDB 路径、带调试信息 ELF、J-Link GDB Server | `.vscode/launch.json` 使用完整调试版 ELF，Cortex-Debug 管理服务器；GDB ELF/符号/启动反汇编已验证，实板调试未执行 |
| 第 15 页，7.1、7.2 | 平台宏、完整器件名、匹配下载算法 | `N32G45X/N32G452/USE_STDPERIPH_DRIVER`；J-Link 选择 `N32G452VE`，不沿用 SDK 示例的 `N32G455CE` 或 `N32G457_ETH` |
| 第 16 页，7.3–7.5 | 算法库可选、Debug/Release、优化等级 | 算法库未启用；Debug `-Og -g3 -gdwarf-2`，Release `-Os -g0`；区别于官方裸机示例默认 `-O0`，用于当前 RT/LVGL 的调试和体积需求 |

原厂 GCC_demo 的核心选项 `-mcpu=cortex-m4 -mthumb`、函数/数据独立段、`--gc-sections`、`--specs=nosys.specs`、MAP 和 objcopy 产物均保留。使用 soft ABI，与原厂 Makefile 未启用硬浮点 ABI 的默认一致；增加 newlib-nano 减小体积。原厂模板末尾丢弃 libc/libm/libgcc 的规则不适用于本工程的库依赖，因此不照搬。

SDK 源文件未改写。RT-Thread 3.1.4 hwtimer 的 `INFO_GET` 缺少 break 在构建副本中修复。SDK `system_n32g45x.c` 的实际编译命令已检查，收到 `SYSCLK_SRC=2`、`SYSCLK_FREQ=128000000`；内部 HSI 配置没有只停留在应用宏上。

## 内存布局与启动契约

| 区域 | 地址范围（左闭右开） | 用途 |
| --- | --- | --- |
| Flash | `0x08000000…0x08080000` | 512 KiB；向量、代码、常量、RT 注册表、data 初值 |
| 低半 RAM | `0x20000000…0x20012000` | 72 KiB；静态数据、newlib 可用区、MSP |
| MSP 保留区 | `0x20011800…0x20012000` | 默认 2 KiB；8 字节对齐 |
| RT 堆 | `0x20012000…0x20024000` | 72 KiB；动态线程、RT 对象及 LVGL |
| 其中 R-SRAM | `0x20020000…0x20024000` | 数据手册明确的连续 16 KiB，按普通运行内存使用 |

原厂通用 `.ld` 写的是 512 KiB Flash / 128 KiB RAM、`_estack=0x20020000`。当前根据 N32G452VE 的 144 KiB 总容量和 RT 分配需求调整；R-SRAM 不在本工程中承担跨 Standby 保留功能。若以后增加低功耗保持需求，必须重新设计该区域，不能保留当前 RT 堆又把它当持久区。

启动代码把 `_sidata` 指向的 Flash 初值复制到 `_sdata…_edata`，清零 `_sbss…_ebss`，依次调用 `SystemInit`、`__libc_init_array`、RT `entry`。`entry` 意外返回时停在局部循环。`board.c` 使用链接符号设置 VTOR，并添加 DSB/ISB。RT 上下文切换使用线程 PSP；中断使用 MSP，运行时栈水位仍需实板检查。

`.bss`、`.noinit`、`.msp_stack`、`.rt_heap` 为 NOLOAD。`.data` 使用 `>RAM AT>FLASH`。RT 自动初始化及 FINSH 表保持 KEEP/SORT 收集；相比原厂嵌在 `.text` 中，当前单列 `.rti_fn/.fsymtab/.vsymtab`，便于核查，注册语义不变。构造函数数组保留。

`__newlib_heap_start/end` 与 `__rt_heap_start/end` 分别界定两个独立区间；`_sbrk` 禁止越过 MSP 下界或退到堆起点之前。当前应用和 LVGL 使用 RT 分配器，newlib 可用区是容量，不代表已经分配。链接脚本加入向量尺寸、物理容量、word 对齐、最小栈、堆栈重叠、初始化表缺失等 ASSERT。

## 修复的检查盲区及证据

原检查器把 `Reset_Handler` 收集到 `_symReset_Handler`，却读取 `_sym_Reset_Handler`，入口检查被跳过；向量解析又固定匹配有前导零的地址，并错误处理字节序，导致初始 SP 检查未执行。原先“22 项通过”不足以证明这些条件成立。

新检查器从 ELF 文件偏移读取小端向量字节，强制存在全部必需符号和段，检查工具返回值，并验证入口、所有非零向量、关键 IRQ、data VMA/LMA、BSS、Flash 镜像尾、全部分配段、MSP/newlib/RT 堆边界以及 RT 初始化顺序。三个实际固件均通过 **31 项检查**。验证纳入 POST_BUILD，普通 CMake Tools 构建也会执行。

`tools/test_linker.ps1` 使用实际 ELF 的损坏副本验证错误 SP、缺少 Thumb 位、错误复位向量、错误 ELF 入口、丢失 RT 初始化表、工具缺失、物理 RAM 缩小；另用最小汇编镜像验证链接器能拒绝小于 2 KiB 的 MSP 与堆栈重叠。**2 个正常对照和 9 个错误场景全部得到预期结果**，详细日志位于 `build/linker-tests/`。

## 调试工具核查与限制

本机工具链未附带 `arm-none-eabi-gdb`，使用现有 GDB multiarch 17.1。以 `set osabi none` 显式指定无操作系统目标，避免 Windows 版 GDB 的宿主 ABI 默认值；已确认 ARMv7E-M 架构、源码行号、链接符号以及 Reset_Handler 反汇编可读。该设置已放入工作区调试参数。官方教程强调 GDB/编译器兼容，本次完成的是文件层面的兼容检查，远程调试、断点与单步仍需探针验证。

按 [SEGGER Commander 官方说明](https://kb.segger.com/J-Link_Commander#ExpDevList) 导出本机 DLL 器件列表，已找到 `N32G452VE` 和 `0x08000000/0x80000` Flash 区间，因此没有覆盖全局 J-Link DLL 或安装旧补丁。只导出器件清单，没有连接目标或烧录。日志为 `build/jlink-devices.log`、`build/jlink-devices.txt`、`build/gdb-symbols.log`。

实板尚未连接，因此这里的结论是：**官方构建要求已落实，当前链接产物和静态内存布局通过软件检查；上电启动、栈水位、GP21/屏幕电气行为尚待实测。**
