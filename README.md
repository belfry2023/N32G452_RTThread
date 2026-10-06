# N32G452VE · RT-Thread · GP21 · GC9307C

当前推荐工程使用 **N32G452VE、内部 HSI/PLL 128 MHz、GP21 外部 5 MHz 参考、GC9307C 240×320、LVGL 按键导航**。

## 依赖（已随仓库提供，克隆后可直接编译）

第三方依赖按**只收录构建所需部分**的方式随仓库提供：

| 依赖 | 位置 | 收录范围 | 原包 → 实际 |
|---|---|---|---|
| Nations N32G45x 固件库 2.6.0 | `Nations.N32G45x_Library.2.6.0/` | `firmware/` + `middlewares/rt-thread/` + `projects/.../GPIO/LedBlink/`（打包脚本的模板） | 47 MB → 9 MB |
| LVGL v8.3.11 | `third_party/lvgl/` | `src/` + `demos/{keypad_encoder,stress}` + `lvgl.h` | 96 MB → 15 MB |

未收录的部分：SDK 的 `projects/` 例程（32 MB）、lwIP 与 FreeRTOS（6 MB）；
LVGL 的上游 `.git`（26 MB）与其它示例的图片资源（55 MB，`lv_conf.h` 里对应的
`LV_USE_DEMO_*` 均为 0，不影响编译）。

需要完整原包时：
- SDK — 从国民技术官网下载，覆盖到同目录即可
- LVGL — `git clone --depth 1 --branch release/v8.3 https://gitee.com/mirrors/lvgl.git third_party/lvgl`

`docs/*.pdf`（厂商数据手册与应用笔记）**未收录**（版权归厂商），需要时从官网下载，
文件名见 [docs/SOURCES.md](docs/SOURCES.md)。

先跑 `./tools/check_env.ps1` 确认工具链（arm-none-eabi-gcc / CMake / Ninja）就位。

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

## 三条使用路径

### 1. GCC + CMake（推荐）

三个预设：`rtthread`（无界面）/ `rtthread-lvgl`（带 LVGL）/ `rtthread-lvgl-release`。
链接脚本自检目标 `verify-ld` 会拿 ELF 反查 22 条硬约束（`.data` 双地址、`_sidata` 指向、`KEEP` 段非空等），见 [官方 GCC 教程对照](docs/OFFICIAL_GCC_AUDIT.md)。

### 2. Keil MDK 工作区工程

直接打开 [N32G452_RTThread.uvprojx](keil/N32G452_RTThread.uvprojx)，包含 AC5/AC6 × Headless/LVGL 四个目标，已在 MDK 5.43a、AC5 5.06 update 7、AC6 6.24 上实际编译链接。执行 `./tools/build_keil.ps1` 可重建全部目标，产物在 `build/keil/`。

### 3. Keil CMSIS-Pack（分发给别人用）

```powershell
python tools\build_pack.py       # -> dist\Belfry.N32G452_RTThread.0.1.2.pack
python tools\validate_pack.py    # 7 项校验（ZIP 完整性 / 依赖闭包 / 示例源图 / 中断向量 ...）
```

> `dist/` 是构建产物，**不在仓库里**，需要时用上面的命令生成。

包内含 **5 个组件**：`Kernel`（RT-Thread 内核与设备框架）、`Board`（BSP 驱动、启动文件、分散加载）、
`GP21`（TDC 器件抽象）、`GC9307C`（LCD 器件抽象）、`LVGL`。

**包只提供可复用基础设施，不含应用代码。** `rtconfig.h` / `board_config.h` / `lv_conf.h` /
`gc9307c_panel.h` / `*.sct` 以 `attr="config"` 提供，安装时拷进工程、随你改；
`main()` 与你自己的 `app_*.c` 归使用方工程所有（对照官方 `RealThread.RT-Thread` 包：
其中 `main.c` 与 `app_*` 出现 0 次）。

导入步骤见 [Keil Pack 使用说明](docs/KEIL_PACK.md)，编译与警告对照见 [Keil 验证记录](docs/KEIL_PACK_VALIDATION.md)，
踩坑记录见 [test01 诊断与修复](docs/KEIL_TEST01_诊断与修复.md)。仓库里的 [`test01/`](test01/) 是一个
**已配置好、可编译的最小消费方工程示例**。

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

## 许可

本仓库自有代码供参考使用。第三方组件版权归各自所有者：
RT-Thread（Apache-2.0）、LVGL（MIT）、Nations 固件库与数据手册（国民技术，未随仓库分发）。
