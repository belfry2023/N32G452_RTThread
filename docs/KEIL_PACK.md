# N32G452VE RT-Thread Keil Pack

版本：`Belfry.N32G452_RTThread.0.1.1.pack`。日期：2026-10-05。替代未经原生编译验证的 0.1.0。

这是本工程的本地集成包，包含 SDK 原生 RT-Thread **3.1.4 内核、设备框架、FINSH/msh**，不是 RT-Thread Nano。当前启用 PIN、串口、PWM、hwtimer、SPI、GP21、GC9307C 与可选 LVGL 8.3.11；没有启用 DFS、网络等未接入本工程的子系统。本包不是厂商官方发布，不替换 Nations DFP。

## 安装与直接打开示例

1. 在 Keil Pack Installer 中安装 `dist/Nationstech.N32G45x_DFP.1.3.0.pack`。这是对 `zip/Nations.N32G45x_DFP.1.3.0.pack` 的文件名兼容修正副本：外层文件名和内部 PDSC 文件名改为与内部厂商标识 Nationstech 一致，所有条目内容逐字节保留。原 zip 文件不改动。器件选择 **Nationstech → N32G452VEL7**；J-Link 中的短名仍是 N32G452VE。
2. 双击 `dist/Belfry.N32G452_RTThread.0.1.1.pack`，或在 Pack Installer 使用 **File → Import** 导入。已经装过 0.1.0 的工程需选择新版本；旧示例工程不会随 Pack 安装自动重写，建议从新版重新复制示例，再迁移自己的接线配置。
3. 在 Pack Installer 的 Examples 中查找 **N32G452 RT-Thread GP21 LCD**，复制到自己的工作目录后打开 `N32G452_RTThread.uvprojx`。如果列表筛选使示例不可见，将 pack 当 ZIP 展开，复制整个 `Examples/N32G452_RTThread/` 即可；该目录源文件自包含，没有指向原工作区的路径。
4. 选择四个 Target 之一：`AC5_Headless`、`AC5_LVGL`、`AC6_Headless`、`AC6_LVGL`。Headless 保留设备驱动，关闭 LVGL 界面。
5. 在 Options for Target 的 ARM Compiler 下拉框选择本机已安装的对应编译器。示例预填并已实测 AC5 5.06 update 7 和 AC6 6.24，其他版本需实际重编译核查。保持 **Use MicroLIB、软件浮点、C99**；AC6 汇编使用 armclang 的 GNU 语法，AC5 使用 armasm。
6. 修改示例 `Config/board_config.h` 接线宏；`Config/gc9307c_panel.h` 为面板参数，`Config/rtconfig.h` 为内核配置，`Config/lv_conf.h` 为 LVGL 配置。默认 HSI PLL 128 MHz、GP21 外部 5 MHz、GC9307C 240×320。
7. Build/Rebuild 后检查 map，按原工程的 `BRINGUP_GP21.md` 上板验证。示例生成 AXF 和 HEX；如需 BIN，使用对应编译器的 `fromelf --bin` 转换 AXF。

本工作区无需通过 Pack 复制工程，可以直接打开 `keil/N32G452_RTThread.uvprojx`。该入口共用工作区原有源码，修改 `inc/board_config.h`、`board/rtconfig.h`、`board/lv_conf.h` 即可。`keil/generated/` 仅保存经过修正的启动、上下文切换及 hwtimer 副本；不要直接编辑生成文件。`./tools/build_keil.ps1` 全量构建四个 Target，自动输出 AXF/HEX/BIN 到 `build/keil/<Target>/Objects/`。如 Keil 安装路径不同，传入 `-KeilRoot`。

## 从 Manage Run-Time Environment 导入现有工程

在 **RT-Thread Full** 分类下选择：

| 组件 | 内容 / 依赖 |
| --- | --- |
| Kernel | 原生内核、IPC、设备框架、SPI/PWM/PIN/hwtimer/serial、FINSH 与编译器端口 |
| Board | N32G452VEL7 的时钟、CMSIS/SPL 快照、启动/scatter、PIN/串口/PWM/TIM6/SPI3/按键；依赖 Kernel |
| GP21 | `tdc0` 完整设备；依赖 Board |
| GC9307C | `lcd0` 完整设备；依赖 Board |
| LVGL | LVGL 库、显示与按键输入端口、小型 demos；依赖 GC9307C |
| Application | 现有采集、诊断、示例 main；依赖 GP21/GC9307C，选中 LVGL 后同时启用 UI |

依赖可通过 Resolve 自动补齐。`Board` 提供当前使用的器件头文件、system、SPL 和专用启动文件；不要同时勾选官方 **Device:Startup / System_N32G45x / StdPeriph Drivers / CMSIS:CORE**。包中定义了上述组件和 Nano（RealThread 的 RTOS:RT-Thread:kernel）的冲突条件。其他 RTOS 也不可并用，不能把当前冲突条件理解成能检查所有第三方内核。

RTE 会将配置文件复制到工程的 `RTE/RT-Thread_Full/N32G452VEL7/`（这是本机 MDK 5.43a 的实际路径）。请修改这些副本，而不是 Pack 安装缓存。配置原文件独立放在 Config 目录，未作为包 include 路径暴露，避免编译器误用未修改的原件。

现有工程仍需核对一次 Target 选项：

- Target：N32G452VEL7，Use MicroLIB，软件浮点。
- C/C++：AC5 启用 C99，Misc Controls 添加 `--no_multibyte_chars`，避免按系统代码页误解析 UTF-8 字符串；AC6 使用 `-std=c99 -mfloat-abi=soft`。
- Asm：AC5 使用 armasm / SoftVFP；AC6 使用 armclang 与 `--target=arm-arm-none-eabi -x assembler-with-cpp -mfloat-abi=soft`。
- Linker：关闭 **Use Memory Layout from Target Dialog**，选择 RTE 复制出的 `n32g452_rtthread.sct`。若 µVision 未自动选取，手动浏览选择该文件。
- Linker Misc Controls：`--entry=Reset_Handler --keep=*(.rti_fn*) --keep=*(FSymTab) --keep=*(VSymTab) --keep=*(HEAP) --keep=*(STACK)`。HEAP 即使暂时没有 malloc 调用也必须保留，以满足 MicroLIB 边界和 scatter 断言。
- 不添加 GCC 的 `src/syscalls.c`、`.ld`、GCC 启动文件及已有工程的重复中断实现。
- 若已有自己的 main，取消 Application 组件，保留一个 `int main(void)`；RT 内核会通过 `$Sub$$main/$Super$$main` 在主线程中调用它，不要再次手工启动调度器。

Board/LVGL 组件通过标准 `Pre_Include_Global_h` 设置时钟和功能宏；若使用不支持该机制的旧 MDK，应升级，或以随包独立示例的 Target 宏为准手动配置。示例已写入全部宏，不依赖 RTE 自动生成。

## 两个编译器的启动与内存布局

AC5 使用官方 ARMASM 启动文件及 RT `context_rvds`。AC6 使用 GNU 语法启动文件及 RT `context_gcc`，由 armclang 汇编，不依赖旧 armasm。两者均进入 Arm C 库的 `__main` 进行 scatter 初始化，再由 RT-Thread 自带的 main 包装进入调度器；没有照搬 GCC 的手工 data/BSS 清零路径。

统一 scatter：Flash `0x08000000…0x08080000`；静态 RAM 限制在 `0x20000000…0x20011600`；MicroLIB 堆 `0x20011600…0x20011800`（512 B）；MSP `0x20011800…0x20012000`（2 KiB）；RT 堆 `0x20012000…0x20024000`（72 KiB）。Keil 的 MicroLIB 堆与 GCC newlib 可用区大小不同，应用/LVGL 仍使用相同的 RT 堆。

RT 堆使用 EMPTY 区域，板级从 `Image$$RT_HEAP$$ZI$$Base/Limit` 读取边界。`.rti_fn*` 单独按 Lexical 排序，保留初始化级别顺序。FINSH 变量表加入版本变量，避免空 VSymTab 没有 Arm 边界符号。scatter 断言核对 102 个向量、堆/主栈长度及初始化表最小长度。R-SRAM 按普通运行 RAM 使用，不提供 Standby 数据持久化。

两个上下文切换副本还修正了 HardFault 调用 C 诊断函数前的 MSP 对齐；AC6 汇编声明 EABI 栈对齐属性，消除 armlink 的 L6306W。四个 Target 使用一致的文件分组顺序，并显式排除非当前编译器的汇编文件和 Headless 中的 LVGL 源文件，防止 µVision 合并目标文件树后误编译。

## 验证范围

已在本机 MDK 5.43a、AC5 5.06 update 7（build 960）、AC6 6.24 上，通过 µVision 实际重建工作区工程与 Pack 示例的四个 Target；全部为 **0 编译错误、0 链接错误**。保留了 RT-Thread/LVGL 原有的兼容性警告，数量和分类见 `KEIL_PACK_VALIDATION.md`。最终 AXF 的向量表、Arm 启动链、堆/主栈边界、初始化表和 FINSH 表已检查。仍未连接硬件，未烧录验证。

原 GCC 工程继续使用原来的 CMake/Ninja 配置。本次共享源文件调整包括 board.h 的 Arm 链接符号分支、按键枚举边界检查、显式空串口 DMA 操作项和删除未使用的统计变量；SDK 原目录不改写。第三方修改应用在打包副本中。

重建包：在工作区根目录执行 `python tools/build_pack.py`，再执行 `python tools/validate_pack.py`；需要 Python 3.9+。每个 pack 文件有 SHA256 旁文件，内部 `MANIFEST.json` 记录条目哈希和来源。当前仅本地交付，PDSC 的 GitHub 作者页面不代表在线更新仓库，不会上传或自动发布。

格式依据：[CMSIS-Pack 组件与配置文件](https://open-cmsis-pack.github.io/Open-CMSIS-Pack-Spec/main/html/pdsc_components_pg.html)、[AC5/AC6 条件](https://open-cmsis-pack.github.io/Open-CMSIS-Pack-Spec/main/html/pdsc_conditions_pg.html)、[示例工程规则](https://open-cmsis-pack.github.io/Open-CMSIS-Pack-Spec/main/html/pdsc_examples_pg.html)。
