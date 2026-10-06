# N32G452VE · RT-Thread · GP21 · GC9307C

当前推荐工程使用 **N32G452VE、内部 HSI/PLL 128 MHz、GP21 外部 5 MHz 参考、GC9307C 240×320、LVGL 按键导航**。

## 立即使用

在 VS Code 打开本目录，Ctrl+Shift+B 构建完整调试版。CMake Tools 选择 `rtthread-lvgl`，调试入口选择 `N32 GP21 + LVGL (HSI / J-Link)`。

```powershell
./tools/check_env.ps1
./tools/build.ps1
./tools/build.ps1 -Preset rtthread-lvgl-release
./tools/run_tests.ps1
./tools/test_linker.ps1
```

产物在 `build/rtthread-lvgl/` 和 `build/rtthread-lvgl-release/`，包含 `n32g452-firmware.elf/.bin/.hex/.map`。另有 `rtthread` 无界面预设；旧裸机、GP22 预设已归档移除。

Keil 可直接打开 [N32G452_RTThread.uvprojx](keil/N32G452_RTThread.uvprojx)，包含 AC5/AC6 × Headless/LVGL 四个目标，已在 MDK 5.43a、AC5 5.06 update 7、AC6 6.24 上实际编译链接。执行 `./tools/build_keil.ps1` 可重建全部目标，产物在 `build/keil/`。工作区工程直接引用原来的 board/inc/app 配置和源码。

可分发安装包在 `dist/Belfry.N32G452_RTThread.0.1.1.pack`；详细导入步骤见 [Keil Pack 使用说明](docs/KEIL_PACK.md)，完整编译、警告和官方示例对照见 [Keil 验证记录](docs/KEIL_PACK_VALIDATION.md)。

## 接线与设备

所有默认接线集中在 [inc/board_config.h](inc/board_config.h)，更换接线后重新构建。默认值需与 PCB 核对；SPI/PWM 必须使用有效复用引脚。

- 原生 RT PIN、USART、PWM、TIM6 hwtimer、SPI bus/device。
- `tdc0` 封装 GP21：独占打开、配置与读回自检、IRQ 采集、状态验证、超时恢复、FIFO、诊断统计；使用数字输入校准范围 2，测量外部 START 到 STOP1。
- `lcd0` 封装 GC9307C：GPIO 8080-I 16 位总线、RGB565、矩形刷新、彩条测试；接入 LVGL 240×40 单缓冲。
- 三个按键替代触摸，通过焦点组操作菜单、数值/控件页和状态页；参考 elec-cdemo 的菜单与返回逻辑。

串口 115200 可运行 `list_device`、`selftest`、`timer_test`、`tdc`、`lcd_test`、`lcd_test off`、`led`。TDC 通信检查顺序为 `tdc stop` → `tdc selftest` → `tdc start`。

详细说明：

- [开发、引脚、设备 API 与上板步骤](docs/BRINGUP_GP21.md)
- [资料来源及压缩包短路径映射](docs/SOURCES.md)
- [软件验证记录](docs/VALIDATION.md)
- [官方 GCC 教程对照与链接审查](docs/OFFICIAL_GCC_AUDIT.md)
- [工作区整理与历史恢复](docs/CLEANUP.md)

## 环境范围与验证边界

沿用本机 GCC/CMake/Ninja、Nations SDK 2.6.0、RT-Thread 3.1.4 和 LVGL 8.3.11。设置仅在当前工作区生效；脚本退出时恢复进程 PATH，没有更换系统工具链或修改其他 STM/NXP/DSP 工程。

当前已完成交叉编译、链接检查和主机侧驱动回归测试。**未连接实板，未烧录；GP21 实际测量精度、GPIO 时序、屏幕显示和模组电源/gamma 参数仍需按上板步骤验证。** 固件不会凭空产生 START/STOP 测量信号。

原始压缩包完整保留；清单和 SHA256 见 `references/manifest.json`。重复展开内容已清理，可按 [短路径恢复说明](references/README.md) 按需展开。原 DSH 工程的旧配置与说明保存在 `zip/history/workspace-before-cleanup-20261004.zip`。
