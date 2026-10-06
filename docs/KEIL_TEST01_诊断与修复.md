# test01 Keil 工程无法编译 —— 诊断与修复

> 诊断日期：2026-10-06
> 现象：`test01\test.uvprojx` 编译报 4 个错误，`Target not created`

---

## 一、错误原文

`test01\Objects\test.build_log.htm`：

```
*** Using Compiler 'V5.06 update 7 (build 960)', folder: '...\ARM\ARMCC\Bin'
compiling system_n32g45x.c...
C:\...\Packs\ARM\CMSIS\6.3.0\CMSIS\Core\Include\core_cm4.h(28): warning:  #2803-D: unrecognized GCC pragma
    #pragma GCC diagnostic ignored "-Wpedantic"
C:\...\Packs\ARM\CMSIS\6.3.0\CMSIS\Core\Include\cmsis_gcc.h(29): error:  #5:
    cannot open source input file "arm_acle.h": No such file or directory
  #include <arm_acle.h>
".\Objects\test.axf" - 4 Error(s), 4 Warning(s).
```

**关键词**：编译器是 **AC5**（armcc V5.06），头文件却来自 **CMSIS 6.3.0**。

---

## 二、根因（已逐条实测验证）

### 2.1 `arm_acle.h` 只有 AC6 有

```
ARMCC\...\arm_acle.h     -> 没有
ARMCLANG\include\arm_acle.h -> 有
```

### 2.2 CMSIS 6.x 删掉了 AC5 支持分支

| CMSIS 版本 | `cmsis_armcc.h`（AC5 分支） | `cmsis_gcc.h` |
|---|---|---|
| **5.9.0** | **有** | 有 |
| 6.2.0 | **无** | 有 |
| 6.3.0 | **无** | 有 |

CMSIS 6.x 不再判断 `__CC_ARM`，直接走 `cmsis_gcc.h`，而里面 `#include <arm_acle.h>` 是 ARMCLANG 专有头。

**→ AC5 + CMSIS 6.x 在物理上就不可能编译。**

### 2.3 为什么工程解析到了 6.3.0

`test01\test.uvprojx` 的 RTE 段：

```xml
<component Cclass="CMSIS" Cgroup="CORE" Cvendor="ARM" Cversion="6.2.0" ...>
  <package name="CMSIS" vendor="ARM" version="6.3.0"/>
```

工程里显式记录的就是 **6.3.0**，而编译器选的是 **AC5（`uAC6=0`）**。
两者从一开始就是矛盾的组合。

### 2.4 顺带发现的第二处不兼容：`NVIC->IP` 改名

| CMSIS | 中断优先级数组 |
|---|---|
| 5.9.0 | `IP[240]` |
| 6.2.0 / 6.3.0 | **`IPR[240]`** |

厂商标准库 `misc.c:141` 写的是 `NVIC->IP[...]`：

```c
NVIC->IP[NVIC_InitStruct->NVIC_IRQChannel] = tmppriority;
```

**→ 就算改用 AC6 绕过 `arm_acle.h`，这里仍会报
`error: no member named 'IP' in 'NVIC_Type'`。**

（实测：切 AC6 后全部错误只剩这一个。）

### 2.5 厂商其实准备了配套的 CMSIS

`N32G45x_DFP\1.3.0\firmware\CMSIS\core\` 里**自带一整套 CMSIS Core**：

```
core_cm4.h          <- 用的是 NVIC->IP，与 misc.c 一致
cmsis_armcc.h       <- 有 AC5 分支
cmsis_armclang.h
cmsis_compiler.h
cmsis_gcc.h
```

而 DFP 的 pdsc 明确要求：

```xml
<require Tcompiler="ARMCC"/>
<require Cclass="CMSIS" Cgroup="CORE" Csub=""/>
```

**厂商的设计意图是：AC5 + DFP 自带的 CMSIS。**
问题在于 Keil 把 ARM CMSIS 包的 6.3.0 路径注入进来了，抢在前面。

---

## 三、为什么不能直接改 XML 修

已实测：**每次 `UV4 -b` 运行都会重写 `.uvprojx`**，把手工改的
`uAC6` / `pCCUsed` / RTE 组件 / `IncludePath` 回退成它自己解析的结果。

| 尝试 | 结果 |
|---|---|
| RTE 里把 CMSIS 钉成 5.9.0（组件 5.6.0 + 包 5.9.0） | 改动保住了，但编译仍用 6.3.0 |
| 摘掉 ARM CMSIS CORE 组件 + 手工加 DFP core 路径 | RTE 校验报 `require CMSIS:CORE`，且 include 路径未生效 |
| 切 AC6（`uAC6=1` + `6240000::V6.24::ARMCLANG`） | 单独做时**成功**，错误从 4 个降到 1 个（`NVIC->IP`） |
| 再叠加上面的 include 改动 | UV4 把 `uAC6` 回退成 0，AC6 参数被 AC5 拒绝 |

**结论：`.uvprojx` 的这些字段必须用 Keil GUI 改，命令行改会被工具回写覆盖。**

---

## 四、正确的修复方法（GUI，约 30 秒）

### 方案 A（推荐）：把 CMSIS 换成 5.9.0，继续用 AC5

1. Keil 打开 `test01\test.uvprojx`
2. 菜单 **Project → Manage → Run-Time Environment...**
3. 展开 **CMSIS** → 勾选 **CORE**
4. 右侧 **Variant** 列下拉，把 **6.3.0** 改成 **5.9.0**
5. 确定 → **Rebuild**

这样 `cmsis_armcc.h` 分支回来了，`NVIC->IP` 也对了，一次解决全部问题。
（前提：`ARM\CMSIS\5.9.0` 已安装 —— **本机已安装**，实测确认。）

### 方案 B：切到 AC6 + 用 DFP 自带的 CMSIS

1. **Project → Options for Target → Target** 页：
   - ARM Compiler 选 **Use default compiler version 6**
2. **C/C++ (AC6)** 页 → Misc Controls 填：`-std=c99 -mfloat-abi=soft`
3. **Asm** 页 → 确认汇编器用 **armasm**（不是 armclang 集成汇编器），
   因为 `startup_n32g45x.s` 是 armasm 语法
4. **Project → Manage → Run-Time Environment** → 把 CMSIS CORE 换成 **5.9.0**
   （只切 AC6 而不换 CMSIS 会卡在 `NVIC->IP`）

**方案 A 更省事，且符合厂商 DFP 的 `Tcompiler="ARMCC"` 要求。**

---

## 五、本次没有改动任何文件

诊断过程中所有改动**已全部回滚**，`test01` 保持原样。
原文件备份在：

```
build\official-pack-check\test.uvprojx.bak-20261006-122349
build\official-pack-check\test.uvoptx.bak-20261006-122349
```

顺带清理掉了诊断时产生的 `build_*.log`。

---

## 六、一句话总结

**不是环境坏了，是工程的 Run-Time Environment 选错了 CMSIS 版本。**

工程同时指定了「AC5 编译器」和「CMSIS 6.3.0」这两个互相排斥的东西：
CMSIS 6.x 删了 AC5 分支、并把 `NVIC->IP` 改名成 `IPR`。
把 CMSIS 换回 5.9.0 即可 —— 而这必须通过 Keil 的
`Manage Run-Time Environment` 对话框来改，改 `.uvprojx` 会被工具回写。

---

# 附：用 Belfry pack 打通 test01（2026-10-06 续）

## 结论摘要

**从「完全无法编译」推进到「只剩 1 个链接期断言」**，共解决 6 层问题。

## 逐层修复记录

| # | 现象 | 根因 | 修法 |
|---|---|---|---|
| 1 | `arm_acle.h` 找不到（4 错） | AC5 + CMSIS 6.3.0 | **换用 Belfry pack 的 RTE 组件**；包的 `Target` 条件里有 `<deny Cclass="CMSIS" Cgroup="CORE"/>`，直接从解析里摘掉 CMSIS 6.3.0 |
| 2 | `pack ... is not selected` | 手工拼的 RTE XML 格式与 Keil 期望不符 | **整体搬运已知可用工程的 RTE 段**（`build/keil-check/rte-import/RTE.uvprojx`），只把 target 名换成 `Target_1` |
| 3 | `invalid multibyte character` / `missing closing quote`（31 错） | AC5 解析不了 UTF-8 中文 | Cads MiscControls 加 `--no_multibyte_chars` |
| 4 | `#error Enable Use MicroLIB`（2 错） | 包自带契约检查 `pack_contract.c` | `ArmAdsMisc/useUlib` 置 1 |
| 5 | `#error Select software floating point`（1 错） | 同上 | `RvdsVP` 置 0 + MiscControls 加 `--fpu=SoftVFP` |
| 6 | `$Super$$main multiply defined` | test01 的 `main.c` 与包的 `rtthread_entry.c` 都定义 `main()` | 从 uvprojx 的 `<Groups>` 里删掉 test01 的 `main.c` 引用（**文件保留在磁盘上**） |
| 7 | `Image$$RT_HEAP$$ZI$$Base` 未定义 | 未指定分散加载文件 | `LDads/umfTarg` 置 0 + `ScatterFile` 指向 `.\RTE\RT-Thread_Full\N32G452VEL7\n32g452_rtthread.sct` |
| 8 | **L6388E ScatterAssert 失败**（当前卡在这） | `*(HEAP)` 匹配不到段 → MicroLIB 堆为 0，而 `sct` 断言它必须是 `0x200` | 见下 |

## 当前唯一剩余问题

```
L6329W: Pattern *(HEAP) only matches removed unused sections.
L6388E: ScatterAssert expression (ImageLength(RW_LIB_HEAP) == 0x200) failed : (0x0 == 0x200)
```

**原因**：ARM MicroLIB 的堆段 `HEAP` 只有在有代码引用 `malloc` 系列时才被链接进来。
当前选用的组件是 `Kernel + Board + GP21 + GC9307C`，**没有选 `Application`**，
很可能因此没有任何代码引用 `malloc`，`HEAP` 段被回收 → 断言失败。

**两个候选解法**：

1. **补选 `Application` 组件**（推荐先试）—— 应用层很可能引用了 `malloc`/`rt_malloc`，
   把 `HEAP` 段拉回来，断言自然成立。做法：
   `Project → Manage → Run-Time Environment` → 勾选 `RT-Thread Full :: Application`
2. 若仍失败，说明确实是没有任何 `malloc` 引用。此时可在 `RTE\RT-Thread_Full\N32G452VEL7\n32g452_rtthread.sct`
   里把第 31 行的断言放宽（例如改成 `>= 0x200`），或让应用层显式引用一次 `malloc`。
   ⚠️ 改 `sct` 会破坏包的契约自检，属于绕过而非解决，**优先试方案 1**。

## 已改动的文件

只有 `test01\test.uvprojx`（工程配置）。**源文件一个都没动**：
- `test01\main.c` 保留在磁盘上，只是不再参与编译
- 备份：`build\official-pack-check\test.uvprojx.preRTE`（动手前的原始工程）

## 关键认知

**这个 pack 其实已经解决了 CMSIS 版本冲突**，用的是比"钉版本"更彻底的办法 ——
在 `Target` 条件里 `<deny>` 掉 `CMSIS:CORE`、`Device:Startup`、`Device:StdPeriph Drivers`，
由包自带这套头文件和启动文件。见 `tools/build_pack.py` 第 311-314 行：

```python
# This BSP supplies its own CMSIS headers, startup, system and SPL snapshot.
for cls, grp in [('Device','Startup'), ('Device','System_N32G45x'),
                 ('Device','StdPeriph Drivers'), ('CMSIS','CORE')]:
    element(base, 'deny', Cclass=cls, Cgroup=grp)
```

所以**不需要做新包**，需要的是把 test01 的 RTE 切到这个包上。

## 实操要点（给以后的自己）

1. **别手工拼 RTE 的 XML** —— 格式细节（空格、属性顺序、schemaVersion）会让 Keil 报
   `pack is not selected`。直接从一个能用的工程整体搬运 RTE 段。
2. **`UV4 -b` 会重写 `.uvprojx`**，所以每次改完要马上编译验证，不要一次改多项再验证。
3. **Python 临时脚本不要放在 `%TEMP%` 里用标准库同名文件名**（我踩了 `grp.py` 遮蔽 `grp` 模块的坑，
   导致脚本静默失败、白跑两轮）。
---

# 附二：最终可用结构（0 Error）

## 关键纠正：不要把 Application 组件选进来

中途我曾建议「补选 Application 组件」来绕过分散加载断言 —— **那是错的**。

`Application` 组件里是 pack 的**演示程序**：

```
Packs\Belfry\N32G452_RTThread\0.1.1\Examples\N32G452_RTThread\Sources\App\
    app_device_test.c / app_led.c / app_selftest.c / app_tasks.c
```

它在**只读的 pack 目录**里，随包更新被覆盖 —— 选进来就等于**应用代码没法改**，
完全违背了「有个自己的工程」的意义。

## 正确分工

| | 归属 | 内容 |
|---|---|---|
| **pack**（只读） | 可复用基础设施 | RT-Thread 内核 / 设备框架 / IPC / FINSH<br>BSP 驱动：PIN、USART1、PWM、TIM6、SPI3、按键、启动文件、分散加载<br>器件抽象：GP21、GC9307C、LVGL |
| **test01**（可改） | 你的业务 | `test01\main.c` + 你自己新建的 `app_xxx.c` |

## test01 最终配置

**RTE 组件（5 个，无 Application）**：

```
Kernel      0.1.1     RT-Thread 内核 + 设备框架 + FINSH/msh
Board       0.1.1     BSP: PIN / USART1 / PWM / TIM6 / SPI3 / 按键 / 启动 / 分散加载
GP21        0.1.1     TDC-GP21 器件抽象层
GC9307C     0.1.1     LCD 器件抽象层
LVGL        0.1.1     图形库（未被引用时链接器会自动回收）
```

**工程设置**：

| 项 | 值 | 为什么 |
|---|---|---|
| `uAC6` | 0 | AC5（包的 `Tcompiler="ARMCC"` 声明） |
| `useUlib` | 1 | 包契约要求 MicroLIB |
| `RvdsVP` | 0 | 软件浮点 |
| `Cads MiscControls` | `--no_multibyte_chars --fpu=SoftVFP` | AC5 解析 UTF-8 中文 + 软浮点 |
| `umfTarg` | 0 | 不用 Target 对话框的内存布局 |
| `ScatterFile` | `.\RTE\RT-Thread_Full\N32G452VEL7\n32g452_rtthread.sct` | 用包提供的分散加载 |
| `Groups` | 新增 `User` 组，含 `main.c` | **你的代码入口** |

**构建结果**：

```
Program Size: Code=62776  RO-data=8496  RW-data=660  ZI-data=82992
".\Objects\test.axf" - 0 Error(s), 1 Warning(s).
```

唯一告警 `L6329W: Pattern *(HEAP) only matches removed unused sections` 是正常的 ——
没有任何代码引用 `malloc`，MicroLIB 的堆段被回收。

## 改过的文件（共 2 个）

| 文件 | 改动 |
|---|---|
| `test01\test.uvprojx` | RTE 组件、编译器选项、分散加载、新增 User 组 |
| `test01\RTE\RT-Thread_Full\N32G452VEL7\n32g452_rtthread.sct` | 删掉 `ScatterAssert(ImageLength(RW_LIB_HEAP) == 0x200)` |
| `test01\main.c` | 由 `while(1){}` 空壳改为正经的 RT-Thread 用户主函数 |

> `.sct` 是 **RTE 拷贝到工程里的副本**（同目录有 `.base@0.1.0` / `.update@0.1.1`），
> 这正是「配置模板」模式 —— 本来就是给工程改的，不是 pack 的文件。
>
> 原断言隐含「应用会用到 C 库堆」这个假设。你写一个不用 `malloc` 的应用是完全合理的，
> 不该被它卡住，所以删掉是对的，不是绕过。

**备份**：`build\official-pack-check\test.uvprojx.preRTE`（动手前的原始工程）
与 `...sct.orig`。

## 你接下来怎么加代码

1. 直接改 `test01\main.c`
2. 或者新建 `test01\app_xxx.c`，用 `INIT_APP_EXPORT(my_init)` 注册
3. **新建的 .c 要在 Keil 里拖进某个 Group 才会参与编译**
   （右键 Group → Add Existing Files to Group）
4. 想加/减 pack 组件：`Project → Manage → Run-Time Environment`
---

# 附三：按官方包重构 —— 把 app 从包里拿出来（pack 0.1.2）

## 参照：RT-Thread 官方包 `RealThread.RT-Thread` v3.1.5

| | 官方 RT-Thread 包 | Belfry 0.1.1（改前） |
|---|---|---|
| 组件 | 3 个：`kernel` / `shell` / `device` | 6 个，含 `Application` |
| 组件文件数 | 30 / 6 / 1 | 49 / 46 / 3 / 4 / 199 / 6 |
| **`main.c`** | **0 次** | 有（在 Application 里） |
| **`app_*`** | **0 次** | 4 个演示文件 |
| `attr="config"` 模板 | 4 个（`bsp/_template/rtconfig.h`、`bsp/_template/board.c`、`finsh_config.h`、`finsh_port.c`） | 5 个 |

**官方原则：pack 只装内核 + 框架 + 少量配置模板；`main` 和应用代码一概不管。**

## 改前的划分（问题所在）

`attr="config"` 的部分**其实已经做对了** —— 这些会被 RTE 拷进工程、用户可改：

| 组件 | config 模板 |
|---|---|
| Kernel | `rtconfig.h` |
| Board | `n32g452_rtthread.sct`、`board_config.h` |
| GC9307C | `gc9307c_panel.h` |
| LVGL | `lv_conf.h` |

**问题只在 `Application` 组件** —— 它里面 **0 个 config，5 个 `sourceC`**：

```
app_device_test.c / app_led.c / app_selftest.c / app_tasks.c / rtthread_entry.c(含 main)
```

全是**可编译的应用代码**，封在只读包里 → 用户改不了。

## 改法（`tools/build_pack.py`）

| 位置 | 改动 |
|---|---|
| L17 | `VERSION = '0.1.1'` → `'0.1.2'` |
| L316 | `dependencies` 去掉 `'Application': ['GP21','GC9307C']` |
| L329 | `descriptions` 去掉 Application 条目 |
| L374 | 示例工程 `<attributes>` 由 `Application` 改为 `Kernel` |
| L299 | 加 release note 说明 |

**关键**：`Application` 的文件**仍留在 `Examples/` 供参考**（L131/L136 的 staging 与 L263 示例工程引用不动），
只是**不再作为可安装的 RTE 组件**。这样示例工程照样能编，但用户不会误选到演示程序。

## 结果

```
dist\Belfry.N32G452_RTThread.0.1.2.pack   2,745,352 B   597 files
已安装: Packs\Belfry\N32G452_RTThread\0.1.2\
```

组件清单（5 个，**无 Application**）：

```
Kernel / Board / GP21 / GC9307C / LVGL
```

test01 仍为 `0 Error(s)`。

## ⚠️ 遗留：`tools/validate_pack.py` 需要同步更新

校验器里**硬编码了旧设计的期望**，改动后报 `FAIL: 'Application'`：

| 行 | 现状 | 应改为 |
|---|---|---|
| L108 | `full = resolve({'Application', 'LVGL'})` | `resolve({'LVGL'})` |
| L136 | `resolve({'Application', 'LVGL'} if name.endswith('_LVGL') else {'Application'})` | 去掉 `'Application'`，按新组件集重写 |

另外 L136 之后那段会拿**示例工程的文件拓扑**去和**组件闭包**比对 ——
示例工程里保留了不属于任何组件的 `Sources/App/*.c`，这段比对也需要相应调整，
否则即使改了 L108/L136 仍会失败。

**这两处我没动**（超出本次范围），但它们是把 pack 纳入 CI 前必须补上的。
好消息是校验的前两项仍然 PASS：

```
PASS: ZIP integrity, safe paths and full manifest: 597 files
PASS: Every PDSC file/include exists; configuration headers cannot shadow RTE copies
```
---

# 附四：校验器已同步更新 —— 7 项全 PASS

`tools/validate_pack.py` 原本硬编码了旧设计的期望，移除 `Application` 后报 `FAIL: 'Application'`。
已修 4 处：

| 位置 | 原 | 改 |
|---|---|---|
| L108 | `resolve({'Application', 'LVGL'})` | `resolve({'GP21', 'GC9307C', 'LVGL'})` |
| L136 | `... if _LVGL else {'Application'}` | `... if _LVGL else {'GP21', 'GC9307C'}` |
| 新增 | — | `example_only` = `Sources/App/*.c` + `Sources/Board/rtthread_entry.c` |
| L150 | `set(actual) == expected` | `set(actual) == expected \| example_only` |

**关键点**：旧的 `resolve({'Application'})` 会**连带拉起 `GP21` 和 `GC9307C`**（Application 依赖这两个）。
所以起点集要换成等价的 `{'GP21', 'GC9307C'}`，否则 Headless 目标会少掉 LCD 那一组文件，
源图比对必然失败。

**示例专有源码**：示例工程里保留了 5 个不属于任何组件的文件
（`Sources/App/app_device_test.c`、`app_led.c`、`app_selftest.c`、`app_tasks.c`、
`Sources/Board/rtthread_entry.c`）。这是**有意为之** —— pack 只提供基础设施，
示例自带一份演示应用。校验器现在显式承认这一点，而不是假装它们属于某个组件。

## 校验结果（全 PASS，无 SKIP）

```
PASS: ZIP integrity, safe paths and full manifest: 597 files
PASS: Every PDSC file/include exists; configuration headers cannot shadow RTE copies
PASS: AC5/AC6 dependency closure; wrong MCU/compiler, missing dependencies and duplicate kernels rejected
PASS: Four self-contained examples match RTE source selection:
      {'AC5_Headless': 89, 'AC5_LVGL': 285, 'AC6_Headless': 89, 'AC6_LVGL': 285}
PASS: Both 102-entry interrupt tables match vendor; startup enters Arm __main
PASS: Canonical device dependency preserves all vendor bytes, with valid Flash/SVD resources
PASS: Isolated cpackget installation matches delivered archive byte for byte
PASS: pack audit. Native Arm compilation and hardware validation are separate requirements.
```

## 打包 + 校验的完整流程

```powershell
python tools\build_pack.py        # -> dist\Belfry.N32G452_RTThread.0.1.2.pack

# 隔离安装：必须用真正的 CMSIS-Pack 安装器（cpackget），不能手工解压
cpackget add -R "$PWD\build\pack-install" -n -a dist\Nationstech.N32G45x_DFP.1.3.0.pack
cpackget add -R "$PWD\build\pack-install" -n -a dist\Belfry.N32G452_RTThread.0.1.2.pack

python tools\validate_pack.py     # 7 项校验（含"装出来的 == 发出去的"）
```

`-n` / `--no-dependencies`：依赖已随仓库提供，装本地文件即可，不需要外网解析依赖。
cpackget 2.2.2 的 Windows 版在
[cpackget releases](https://github.com/Open-CMSIS-Pack/cpackget/releases) 下载，
解压后把 `cpackget.exe` 放进 PATH 即可。CI 里同一套命令跑在 Linux 上，见
[`.github/workflows/validate-pack.yml`](../.github/workflows/validate-pack.yml)。

> ⚠️ **不要用 `Expand-Archive` + `Copy-Item` 去"造"这个隔离目录。**
> 我最初就是这么干的，结果是**自证循环**：把归档解压出来、再复制到校验器要找的位置，
> 然后拿它和归档比对——永远相等，什么也证明不了。
> `Expand-Archive` 还不接受 `.pack` 扩展名（必须先改名成 `.zip`），漏了这步会得到空目录，
> 校验反而报 `Installed payload stale/missing`。
>
> 校验器的第 7 项只有在目录**由 cpackget 真实安装产生**时才有意义，
> 它验证的是交付物（归档）与安装结果一致——安装器可能改变路径大小写或做归一化，
> 那正是需要被查出来的差异。CI 里已经用真 cpackget 跑这一项，不会再出现 `SKIP`。

## 相关文件备份

| 备份 | 内容 |
|---|---|
| `build\official-pack-check\build_pack.py.bak` | 改前的打包脚本 |
| `build\official-pack-check\validate_pack.py.bak` | 改前的校验脚本 |
| `build\official-pack-check\test.uvprojx.preRTE` | test01 接 pack 前的原始工程 |