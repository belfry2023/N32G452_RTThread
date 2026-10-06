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

## 如何补齐 docs/*.pdf

`docs/` 下的厂商 PDF **未随仓库分发**（版权归厂商，见 [THIRD_PARTY_NOTICES](../THIRD_PARTY_NOTICES.md)），
`.gitignore` 里以 `docs/*.pdf`、`docs/*.PDF` 排除。

> **缺这些文件不影响编译。** 构建只需要 SDK、LVGL 与源码，三者都已随仓库提供并已裁剪入库；
> PDF 只是**设计依据**。克隆后直接 `./tools/build.ps1` 即可出固件。

需要复现资料依据时，按下表把文件放回 `docs/`。**文件名必须与左列完全一致**——
本文件与 README 的引用都以文件名为准（大小写敏感，含中文与空格）。

### 国民技术（Nations / NSING）

入口：[官网 N32G45x 产品页](https://www.nationstech.com/product/general/n32g/n32g45x/) → 「资料下载」；
国际站镜像 [nsingtech.com](https://www.nsingtech.com/product/general/n32g/n32g45x/)。
按类别找：**数据手册**（Datasheet）、**应用笔记**（Application Note）、**软件资源**（SDK / Pack）。

| 文件名 | 类别 | 用在哪里 |
| --- | --- | --- |
| `CN_DS_N32G452_Series_Datasheet_V2.5.0.pdf` | 数据手册 | 容量、引脚复用表、时钟边界、电气参数 |
| `CN_AN_Universal_MCU_GCC_Development_V1.2.0.pdf` | 应用笔记 | GCC 启动文件、链接脚本、`__libc_init_array` 流程 |
| `AN_通用MCU RT_Thread设备注册应用笔记V1.0.0.pdf` | 应用笔记 | RT 设备注册与 `INIT_*_EXPORT` 初始化阶段 |
| `AN_N32G4FR_N32G45x_N32WB452系列HSI频率调节应用笔记V1.0.0.pdf` | 应用笔记 | 内部 HSI 校准（本工程用 HSI/PLL，未烧校准值） |
| `AN_N32G4FR_N32G45x_N32WB452系列缓慢上电应用笔记V1.0.0.pdf` | 应用笔记 | 上电时序背景 |
| `AN_N32G45x_N32G4FR_N32WB452系列RSRAM奇偶校验出错检测应用笔记V1.0.0.pdf` | 应用笔记 | R-SRAM 与 SRAM 校验 |
| `AN_N32G45x系列安全启动应用笔记V1.2.0.pdf` | 应用笔记 | 安全启动/保护位（本工程未改动这些配置） |
| `N32G45x_FR_WB系列芯片IAP升级应用笔记_V1.1.0.pdf` | 应用笔记 | IAP 升级区划分（本工程未启用） |
| `N32G45X_FR_WB series chip IAP upgrade application note_V1.1.0.pdf` | 应用笔记 | 上一条的英文版 |

软件资源（已裁剪入库，无需下载也能编译）：

| 名称 | 获取方式 | 仓库内情况 |
| --- | --- | --- |
| `Nations.N32G45x_Library.2.6.0`（SDK） | 官网「软件资源」里的固件库压缩包（原包 `11.zip`） | 只保留 `firmware/` + `middlewares/rt-thread/` + `LedBlink` 模板（47 MB → 9 MB）；需要完整包时解压覆盖回同目录 |
| `Nations.N32G45x_DFP.1.3.0.pack` | 官网，或 Keil Pack Installer 搜 `N32G45x` | **已入库**（`zip/`，146 KB）。打包与校验脚本依赖它，见 [THIRD_PARTY_NOTICES](../THIRD_PARTY_NOTICES.md) |

### 屏与 TDC 器件

| 文件名 | 发布方 | 获取方式 |
| --- | --- | --- |
| `gc9307c.pdf` | 屏驱动 IC 原厂（GC9307C） | IC 手册一般不公开挂网，向**屏模组供应商**索取；到手后重点核对 8080-I 时序、窗口指令、RGB565 与 gamma 表 |
| `C20450283_其他接口_TDC-GP22+5K+T&R_规格书_WJ1252922.PDF` | 经销商规格书（`C20450283` 是**立创商城料号**） | [立创商城](https://www.szlcsc.com/) 搜 `C20450283` 下载 |
| TDC-GP21 手册 | ScioSense（原 acam） | [TDC-GP21 Datasheet](https://www.sciosense.com/wp-content/uploads/2023/12/TDC-GP21-Datasheet.pdf)。**未收录，本地也没有副本** |

> ⚠️ 本地那份是 **GP22** 规格书。GP22 是 GP21 的扩展型号，寄存器与 SPI 时序基本兼容，
> 但**不能拿 GP22 的扩展功能当 GP21 的依据**——`board/drv_gp21.c` 只按 GP21 手册实现。
> 需要完整 GP21 手册时从 ScioSense 官网下载。

### 补齐后怎么确认

1. 文件名对照上表逐字核对（含中文、空格、大小写）。
2. 跑 `./tools/check_env.ps1` 和 `./tools/build.ps1` 确认构建仍然通过——**PDF 与构建无关**，
   这一步只是排除"顺手改坏了别的文件"。
3. 资料与代码不一致时**以实测为准**；文档与代码的差异记录在
   [OFFICIAL_GCC_AUDIT.md](OFFICIAL_GCC_AUDIT.md) 和 [VALIDATION.md](VALIDATION.md)。

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
