# Keil 原生编译及 Pack 验证

日期：2026-10-05。交付版本：Belfry.N32G452_RTThread 0.1.1。

## 本机工具与范围

- Keil MDK 5.43a：`C:/Users/belfry/AppData/Local/Keil_v5`。
- AC5：5.06 update 7（build 960）；AC6：6.24。
- 调用真实 `UV4.exe -r` 全量构建，实际调用 armcc/armasm 或 armclang，再由 armlink 链接；不把 GCC 编译当作 Keil 验证。
- 输出目录均在当前工作区。仅向现有 Pack 根目录增加 N32G45x 正确厂商标识的器件包及新版 Belfry 包；不修改系统 PATH、编译器安装文件和其他 MCU 工程。

## 路径一：完整 RT-Thread 工程

工作区入口：`keil/N32G452_RTThread.uvprojx`。
分发包入口：`Examples/N32G452_RTThread/N32G452_RTThread.uvprojx`。
两套工程各全量构建四个 Target，结果相同：

| Target | 编译/链接错误 | 编译警告 | 链接警告 |
| --- | ---: | ---: | ---: |
| AC5_Headless | 0 | 0 | 0 |
| AC5_LVGL | 0 | 22 | 0 |
| AC6_Headless | 0 | 77 | 0 |
| AC6_LVGL | 0 | 83 | 0 |

未通过全局屏蔽警告来制造“零警告”。剩余主要是旧版 RT-Thread FINSH 的函数指针注册/无原型调用、未使用回调参数、符号比较，以及 LVGL 的枚举/控制流/typedef 提示；应用中的 FINSH 注册也会触发同类兼容性提示。这些警告需要在后续升级中间件时重新评估，不能据此宣称无潜在运行问题。

日志：`build/keil-check/<Target>.log`；Pack 示例日志在 `build/keil-check/pack-example/`。`results.json` 保存原生工具结果和 AXF 的 SHA256。

实际修复：

1. µVision 会合并多个 Target 的文件树。原工程不同 Target 的组序和文件列表不同，导致 AC5 误编译 GNU 汇编。改为一致的文件树，并逐文件写入 IncludeInBuild。
2. AC5 在本机按系统代码页解释 UTF-8，产生错误的字符串结束位置。使用 `--no_multibyte_chars` 保留 UTF-8 字节，避免更改系统区域设置。
3. AC6 MicroLIB 的 stdio.h 已定义 `__FILE`，按编译器区分 retarget 的结构体定义。
4. armlink 删除未引用的 HEAP 段，触发 scatter 堆大小断言。显式保留 HEAP/STACK 及 RT/FINSH 表。
5. GNU 汇编缺少 EABI 对齐声明，产生 L6306W。补充声明，并修正上下文端口 HardFault 调用 C 函数前的实际 MSP 对齐。
6. 原始 DFP 文件名为 Nations，内部 vendor 为 Nationstech，直接安装会放入错误厂商目录。交付兼容副本仅统一文件名，所有源条目内容逐字节不变；工程明确使用 Nationstech.N32G45x_DFP.1.3.0。
7. RTE 导入时 DFP 自动定义芯片宏，组件原先再次定义空值，导致每个编译单元产生重复宏警告。组件现在通过受保护的默认值提供宏，避免与 DFP 冲突。

另使用 `tools/prepare_keil_rte_check.py` 生成 AC5/AC6 两个仅选择六个 RTE 组件的工程。工程没有手工加入 C/汇编源文件、宏或 include 路径；µVision 从已安装 Pack 中解析并添加源码、生成全局预包含头、复制配置头与 scatter，然后实际编译链接。配置副本位于 `RTE/RT-Thread_Full/N32G452VEL7/`，链接使用这个副本。验证记录在 `build/keil-check/RTE_AC5_LVGL.log` 和 `RTE_AC6_LVGL.log`。

## 路径二：官方 SDK 原例程对照

依据本地 Nations SDK 2.6.0 的原始 Keil 工程和各自 readme，对照厂商常规流程：安装 DFP → 打开 MDK-ARM 工程 → 选择已安装 AC5 → 全量编译 → 检查 AXF/HEX 和 map。

| 官方示例 | 编译错误 | 警告 | 用途 |
| --- | ---: | ---: | --- |
| GPIO/LedBlink | 0 | 0 | 启动文件、标准库、编译器和链接器基线 |
| RT_Thread8_PIN_DEVICE_REGISTER | 0 | 0 | 厂商 RT-Thread 内核、ARMASM 上下文和 PIN 设备框架基线 |

生成工具：`tools/prepare_keil_official.py`。工程副本在 `build/keil-official/`，日志在 `build/keil-check/Official_*.log`。只重定向源文件/输出路径、选用本机 AC5 和已安装的 DFP；厂商源码、时钟、引脚及自动内存布局均保留。

**官方示例原目标是 N32G457QEL7、外部晶振/144 MHz，只用于编译环境对照，不能直接下载到本项目 N32G452/HSI 板。** 正式四目标工程已使用 N32G452VEL7、HSI PLL 128 MHz。

当前 docs/zip 没有单独的 Keil 教程 PDF；现有 PDF 是 GCC 环境和 RT 设备说明。官网旧快速开发指南链接已失效，新版下载页要求登录，故此处准确记录为“官方 SDK 原工程对照”，不声称逐页复现了未取得的教程。N32G452 资源及版本可核对[国民技术官网](https://www.nationstech.com/product/general/n32g/n32g45x/n32g452)。

## 链接产物检查

`tools/verify_keil_image.py` 直接读取最终 Arm ELF（AXF）：

- 102 项向量，Flash 起点与 Reset/Thumb 位，以及 HardFault、PendSV、SysTick、USART1、EXTI15_10、TIM6 实际入口。
- MicroLIB 堆 512 B，MSP 2 KiB，RT 堆 72 KiB；RT 堆结束于 0x20024000，静态段不侵入堆栈。
- Arm C 库 __main、RT 主线程包装、原始 $Super$$main 均存在。
- 8 字节初始化描述符连续，初始化级别及板级/设备边界正确，FINSH 两种符号表非空。
- 处理 armlink 将同名 RW_IRAM1 拆成压缩 RW 和 ZI 两个 ELF section 的情况，按实际执行地址核对 RAM。

这与 GCC 的 `.ld` 检查是两条独立验证链。Keil 使用 `pack/keil/n32g452_rtthread.sct`，不使用 GNU `.ld`。

## Pack 与回归

- 官方 CMSIS-Toolbox PackChk 1.4.5：PDSC XSD、静态和 RTE 模型依赖检查，严格模式 0 错误、0 警告。
- cpackget 2.2.2：工作区隔离安装，全部条目与最终包逐字节校验。
- `tools/validate_pack.py`：ZIP CRC/SHA256、路径安全、完整清单、真实外部 Pack 标识、组件依赖/互斥、四目标文件树和 RTE 源文件选择一致性、配置头文件不被包内副本遮蔽。
- 同一源文件输入连续打包两次，SHA256 一致；最终值见 dist 中相应 `.sha256`。
- 原 GCC 三个预设继续构建并通过各 31 项链接检查；GP21 协议/设备、LCD 和 17 项按键手势主机测试通过。

RTE 组件解析和编译已通过 µVision 命令行验证，未进行图形界面手工勾选的交互验证，也未连接硬件。主机编译和产物审查不代替屏幕时序、TDC 通信与测量精度的实板检查。
