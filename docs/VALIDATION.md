# 软件验证记录

验证日期：2026-10-04。目标配置：N32G452VE / HSI PLL 128 MHz / GP21 外部参考 5 MHz / GC9307C 240×320 / RT-Thread 3.1.4 / LVGL 8.3.11。

## 构建结果

| 预设 | Flash 使用 | 静态 RAM（含对齐） | 链接检查 |
| --- | ---: | ---: | ---: |
| `rtthread-lvgl` | 336500 B / 512 KiB | 38840 B | 31 项通过 |
| `rtthread-lvgl-release` | 301356 B / 512 KiB | 38824 B | 31 项通过 |
| `rtthread` | 85036 B / 512 KiB | 18040 B | 31 项通过 |

144 KiB SRAM 分为低 72 KiB 的静态数据/newlib/MSP 和高 72 KiB 的 RT 堆。每种配置另保留 2048 B MSP；newlib 可用容量依次为 32840 / 32856 / 53640 B。表格不包含这些保留区。LVGL 动态对象和动态内核对象使用 RT 堆；运行时消耗须通过板上 `free` / `list_thread` 核查。链接器区域报告中的 RAM/RT_HEAP“100%”是固定地址保留区造成的，不能当作运行时占用率。

清除旧 build 后，三个配置均重新交叉编译。只出现原厂 `system_n32g45x.c` 中 HSI 分支未使用 `HSEStatus`、`StartUpCounter` 两条告警，没有修改或全局屏蔽厂商告警。

原检查器的入口变量名及向量解析存在跳过检查的问题，已经修复。新 31 项检查覆盖实际 ELF 入口/向量字节、关键 IRQ、data VMA/LMA、BSS、全部分配段物理边界、独立 MSP/newlib/RT 堆、RT 初始化顺序与 shell 表。新增 2 个正常对照和 9 个错误场景，全部符合预期，见 `tools/test_linker.ps1` 与 `OFFICIAL_GCC_AUDIT.md`。

已额外检查 `compile_commands.json`：每个固件仅有一份启动文件，不含 GP22 代码；**SDK 的 `system_n32g45x.c` 也收到 `SYSCLK_SRC=2`、`SYSCLK_FREQ=128000000`**。BIN 初始 SP/入口与 ELF 一致；HEX 记录校验和、下载地址、数据与 BIN 一致。

日志：`build/verified-debug.log`、`build/verified-release.log`、`build/verified-rtthread.log`、`build/final-*.log`、`build/linker-tests.log`、`build/artifact-audit.log`。

## 驱动回归

执行 `tools/run_tests.ps1`，使用主机 GCC、`-Wall -Wextra -Werror`，全部通过：

| 测试 | 验证范围 |
| --- | --- |
| `tests/protocol_test.c` | GP21 寄存器位域、STOP1−START 的 ALU 方向、MSB 字节序、状态/击数/异常结果拒绝、5 MHz 单位换算与溢出边界 |
| `tests/gp21_device_test.c` | 编译实际 GP21 驱动，模拟 SPI/PIN/RT；覆盖打开失败与重试、配置验证/修改、IRQ 完成、无完成信号超时不读旧结果、无效样本、FIFO 满、按字节读取、停止/启动代次取消、I/O 故障停止、关闭重开 |
| `tests/lcd_test.c` | 编译实际 LCD 驱动并在总线边界记录输出；验证 CS、列/行参数按字节发送、第 319 行、RGB565、越界拒绝 |
| `tools/key_gesture_test.c` | 17 项：五种手势、阈值与组合窗口边界、多次连击、持续按住和无操作 |

GP21 最终 REG1 为 **0x210200A1**：手册范围 2 运算为 HIT2−HIT1，HIT1=START、HIT2=第一 STOP。测试独立检查这两个字段，SPI ID 读回一致本身不能证明运算配置正确。

结果日志：`build/driver-tests.log`。主机模拟不验证 N32 外设寄存器的真实电气行为、实时调度延迟或模拟测量精度。

## 工具和资源检查

- `tools/check_env.ps1` 检查本机 ARM GCC、CMake、Ninja 和必要本地源码，全部找到。
- 5 个 `.vscode/*.json` 文件按 JSONC 解析成功，任务名无重复，默认构建任务唯一，调试入口的任务引用存在。
- 按 VS Code 实际使用的 `powershell.exe`（Windows PowerShell 5）运行默认构建和链接故障测试均通过；故障测试兼容 PowerShell 5 的 stderr 行为，日志 `build/vscode-build-task.log`、`build/linker-powershell5.log`。
- 12 个原始 zip/pack 的 SHA256 与 `references/manifest.json` 一致。
- SDK 3562 个文件与 `zip/11.zip` 逐字节一致。
- 重复展开资料已清理，原包保持完整；可按 `references/README.md` 按需恢复短路径。
- GDB multiarch 17.1 使用无操作系统 ABI 后可读取 ARM ELF、源码行、链接符号及启动反汇编；J-Link 9.32 的本机列表包含 N32G452VE，未安装全局旧补丁。
- `.vscode` 与构建脚本调整局限本工程；没有修改用户/系统 PATH，没有安装或替换其他平台的编译器。

## 尚未执行的实机验证

本次未连接 N32 开发板、GP21、屏幕或调试探针，没有烧录记录。尚需实际执行：RT 内核/定时器回调自检、PWM 波形、GP21 已知间隔测量与精度、INTN/SPI 时序、屏幕色序/方向/电源与 gamma、8080 时序、实际按键体验和持续运行水位。

可直接按 [BRINGUP_GP21.md](BRINGUP_GP21.md) 的上板顺序执行。这些项目的结果不能由编译通过或主机测试代替。
