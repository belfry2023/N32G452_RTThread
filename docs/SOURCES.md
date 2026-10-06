# 资料来源与采用范围

## 本地资料

已检查 `docs/` 的文档及 `zip/` 的压缩包目录与说明。驱动以原厂资料和当前源码 API 为依据，旧工程中的 GP22/HSE 假设已从当前配置中替换。官方 GCC 教程已全文阅读，并与 SDK GCC_demo/启动汇编/链接脚本逐项对照，结果见 `OFFICIAL_GCC_AUDIT.md`；本次对应提取与内存图核查文件在 `build/official-review/`。

| 资料 | 采用内容 |
| --- | --- |
| `CN_DS_N32G452_Series_Datasheet_V2.5.0.pdf` | N32G452VE 容量、引脚复用、时钟边界 |
| `CN_AN_Universal_MCU_GCC_Development_V1.2.0.pdf` | GCC/CMSIS 启动与链接结构 |
| `AN_通用MCU RT_Thread设备注册应用笔记V1.0.0.pdf` | RT 设备注册与初始化阶段 |
| HSI 频率调节应用笔记及例程 | 内部时钟与校准参考；本工程未烧写芯片校准值 |
| 缓慢上电、RSRAM、安全启动、IAP 等笔记 | 启动及工程背景参考，未改变保护位、升级区或安全启动配置 |
| `gc9307c.pdf` | 8080-I 接口、窗口指令、RGB565、复位参数、gamma 与时序 |
| 本地 TDC-GP22 规格书 | 历史器件参考；当前 GP21 驱动不以 GP22 扩展功能作为依据 |
| `Nations.N32G45x_Library.2.6.0` | SPL/CMSIS 与 RT-Thread 3.1.4 内核、PIN/PWM/SPI/hwtimer 框架 |
| `third_party/lvgl` | 本地 LVGL 8.3.11 的显示、输入、group API |

GC9307C 电源/gamma 表来自手册复位基线，不是已验证的模组厂商专用初始化。后续若获得实际屏模组初始化程序，应与 `board/gc9307c_panel.h` 比较。

## 联网补充资料

- [Sciosense TDC-GP21 Datasheet](https://www.sciosense.com/wp-content/uploads/2023/12/TDC-GP21-Datasheet.pdf)：DB_GP21_en V1.6，2014-03-13，SPI 协议、配置寄存器、范围 2 的 HIT 选择、ID/状态读取、定点结果换算。本次通过网页解析内容核查；完整 PDF 未成功下载到工程，不能将本地 GP22 PDF 当作 GP21 手册。
- [belfry2023/elec-cdemo](https://github.com/belfry2023/elec-cdemo/tree/053e7b4b7583890ab23c382f4736d60e90970d36)：通过 GitHub 读取 `Application/ui/app_ui.c`、`Modules/ui/module_ui.c`，参考菜单/父子页面/焦点操作思路。原工程为 STM32/OLED，当前使用 LVGL 重写界面，没有直接编入其 STM32 硬件代码。

## 压缩包短路径

保留原始包便于校验与追溯，展开时移除冗长根目录。重复展开副本已清理。`python tools/prepare_references.py` 只更新清单，添加 `gcc`、`rtdev hsi` 或 `all` 可按需展开，工具不会覆盖不同内容的既有文件。

| 原包 | 短路径 | 内容 |
| --- | --- | --- |
| `1.zip` | `references/rtdev` | RT 设备注册 |
| `2.zip` | `references/hsi` | HSI 调整 |
| `3.zip` | `references/poweron` | 缓慢上电 |
| `4.zip` | `references/iap` | IAP |
| `5.zip` | `references/gcc` | GCC |
| `6.zip` | `references/rsram` | SRAM 奇偶校验 |
| `7.zip` | `references/mmu` | Flash/MMU 保护 |
| `8.zip` | `references/secure` | 安全启动 |
| `9.zip` | `references/jlink` | J-Link 支持 |
| `10.zip` | `references/flash` | Flash 算法 |
| `11.zip` | 沿用已展开的 `Nations.N32G45x_Library.2.6.0` | SDK |
| `Nations.N32G45x_DFP.1.3.0.pack` | 保留原 pack，已建立清单 | CMSIS 器件包 |

SHA256、条目数、原根目录见 `references/manifest.json`。历史自有代码/配置/说明归档在 `zip/history/workspace-before-cleanup-20261004.zip`。SDK 全部文件已逐字节核对 `11.zip`；未替换全局编译器、调试器或供应商 SDK 文件。
