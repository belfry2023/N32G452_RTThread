# 第三方组件与许可证声明

本仓库**自有代码**以 [MIT](LICENSE) 发布。本文件列出仓库内随附的第三方组件、
其许可证、原文位置与上游来源 —— 这些组件**不受 MIT 覆盖**。

> 打包成 CMSIS-Pack 时，同一份声明会随包分发：
> `LICENSE.txt`（包自身说明）+ `Licenses/RT-Thread-Apache-2.0.txt` + `Licenses/LVGL-MIT.txt`。

## 一、随仓库分发的第三方组件

| 组件 | 版本 | 许可证 | 仓库内位置 | 许可证原文 | 上游 |
|---|---|---|---|---|---|
| RT-Thread 内核 | 3.1.4 | Apache-2.0 | `Nations.N32G45x_Library.2.6.0/middlewares/rt-thread/` | `.../rt-thread/LICENSE`、`pack/licenses/RT-Thread-Apache-2.0.txt` | [RT-Thread/rt-thread](https://github.com/RT-Thread/rt-thread) |
| LVGL | 8.3.11 | MIT | `third_party/lvgl/` | `third_party/lvgl/LICENCE.txt` | [lvgl/lvgl](https://github.com/lvgl/lvgl) |
| Nations N32G45x 固件库 | 2.6.0 | Nations 自有（BSD 风格，见下） | `Nations.N32G45x_Library.2.6.0/firmware/` | 各源文件头部声明 | 国民技术官网 |
| Arm CMSIS（Cortex-M 头文件） | 随 SDK | Apache-2.0 | `Nations.N32G45x_Library.2.6.0/firmware/CMSIS/` | 各头文件头部声明 | [ARM-software/CMSIS_5](https://github.com/ARM-software/CMSIS_5) |
| Nationstech N32G45x 器件支持包（DFP） | 1.3.0 | Nations 自有 | `zip/Nations.N32G45x_DFP.1.3.0.pack` | 包内 `*.pdsc` | 国民技术官网 |

**DFP 说明**：入库的是国民技术原始包，`tools/build_pack.py` 只在**输出副本**里
把归档名与 `pdsc` 名从 `Nations.*` 改为 `Nationstech.*`（`validate_pack.py` 会
逐字节比对两者，确认除文件名外内容完全相同）。原始包本身不被修改。

## 二、未随仓库分发的内容

| 内容 | 原因 | 获取方式 |
|---|---|---|
| `docs/*.pdf`（数据手册、应用笔记、屏/TDC 规格书） | 版权归厂商，不适合转存到个人仓库 | 见 [SOURCES.md](docs/SOURCES.md#如何补齐-docspdf) |
| SDK 的 `projects/` 例程、lwIP、FreeRTOS | 体积大且构建不用 | 从国民技术官网下载完整 SDK |
| LVGL 上游 `.git` 与其它 demo 资源 | 体积大 | `git clone` 上游 |
| `zip/1.zip` … `zip/11.zip` 原始下载包 | 体积 40 MB+，可重新下载 | 厂商官网 |
| `jlink_patch/` | 可重新生成 | J-Link 安装目录 |
| `dist/*.pack` | 构建产物 | `python tools/build_pack.py` |

## 三、被修改过的第三方文件

改动都发生在**构建产物或本仓库自建文件**中，`Nations.N32G45x_Library.2.6.0/`
目录内的文件**逐字节未改**（可对 `zip/11.zip` 校验）：

| 文件 | 改动 | 载体 |
|---|---|---|
| `board/startup_n32g45x_rtthread.s` | 由 SDK 的 `startup_n32g45x.s` 派生：向量表保留原 102 项，入口改为 GCC 的 `Reset_Handler → entry`，`.data/.bss` 初始化按链接脚本符号重写 | 本仓库新文件（Nations 声明随文件保留） |
| `rt_hwtimer.c` | 补上 `HWTIMER_CTRL_INFO_GET` 分支缺失的 `break`（原版会 fallthrough） | CMake 写入 `build/<preset>/rt_hwtimer.c`，SDK 源文件不动 |
| `context_gcc.S` / `cpuport.c` | MSP 对齐后再调用 HardFault C 诊断；补 GNU EABI 栈对齐属性 | 以补丁说明形式记录，SDK 源文件不动 |
| Keil 启动/分散加载（`keil/generated/`、`pack/keil/*.sct`） | N32G45x 中断向量表 + AC5/AC6 双语法 + 固定 MSP/堆预留 | 打包脚本生成 |

## 四、Nations 源代码再分发声明

```
Copyright (c) 2019, Nations Technologies Inc.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

- Redistributions of source code must retain the above copyright notice,
  this list of conditions and the disclaimer below.

Nations' name may not be used to endorse or promote products derived from
this software without specific prior written permission.

DISCLAIMER: THIS SOFTWARE IS PROVIDED BY NATIONS "AS IS" AND ANY EXPRESS OR
IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT ARE
DISCLAIMED. IN NO EVENT SHALL NATIONS BE LIABLE FOR ANY DIRECT, INDIRECT,
INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

## 五、名称与商标

本项目是个人维护的集成工程，**不是**国民技术（Nations / NSING）、RT-Thread、
LVGL 或 Arm 的官方发布物，也未获得其背书。产品名称与商标归各自所有者。
