# =============================================================================
#  N32G452 RT-Thread 固件工程  ——  GCC + CMake + Ninja
# -----------------------------------------------------------------------------
#  目标芯片 : Nations N32G452 (ARM Cortex-M4F, 最高 144MHz)
#  默认档位 : xE  (512K Flash / 144K SRAM)  —— 对应 N32G452VEL7
#
#  典型用法:
#     cmake --preset rtthread-lvgl            # 配置 (Debug, xE)
#     cmake --build --preset rtthread-lvgl    # 编译
#  或在 VSCode 中直接按 Ctrl+Shift+B。
# =============================================================================

cmake_minimum_required(VERSION 3.25)

project(n32g452-firmware
    VERSION     1.0.0
    DESCRIPTION "N32G452 RT-Thread GP21 GC9307C firmware (GCC + CMake)"
    LANGUAGES   C ASM)

# CMake 对 CMAKE_SYSTEM_NAME=Generic 的目标平台不追加可执行文件后缀, 默认产物
# 会是一个无扩展名的 "n32g452-firmware"。显式改成 .elf:
#   - 符合嵌入式惯例, 调试器/烧写工具一眼能识别
#   - 使下面的 size / disasm 目标能引用到确定的路径
set(CMAKE_EXECUTABLE_SUFFIX ".elf")

# 导出 compile_commands.json:
#   - VSCode 的 C/C++ 扩展 (configurationProvider=ms-vscode.cmake-tools) 据此
#     得到与真实构建完全一致的头文件路径和宏定义, IntelliSense 不会漂移
#   - clangd / clang-tidy 等工具也依赖此文件
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# -----------------------------------------------------------------------------
# 可配置项
# -----------------------------------------------------------------------------
set(N32_SDK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/Nations.N32G45x_Library.2.6.0"
    CACHE PATH "Nations N32G45x 固件库根目录")

set(N32_VARIANT "xE"
    CACHE STRING "N32G452 容量档位: xB (128K/80K) | xC (256K/144K) | xE (512K/144K)")
set_property(CACHE N32_VARIANT PROPERTY STRINGS xB xC xE)

set(N32_FPU "soft"
    CACHE STRING "浮点 ABI: soft (与厂商一致) | softfp | hard")
set_property(CACHE N32_FPU PROPERTY STRINGS soft softfp hard)

option(N32_USE_NANO "链接 newlib-nano (体积更小, 推荐)" ON)

set(N32_SYSCLK_SRC "hsi_pll"
    CACHE STRING "时钟源: hse_pll (外部晶振+PLL) | hsi_pll (内部RC+PLL, 无需晶振)")
set_property(CACHE N32_SYSCLK_SRC PROPERTY STRINGS hse_pll hsi_pll)

set(N32_SYSCLK_FREQ "128000000"
    CACHE STRING "系统主频 Hz (hse_pll 最高 144MHz, hsi_pll 最高 128MHz)")
set(N32_HSE_VALUE "8000000"
    CACHE STRING "外部晶振频率 Hz (仅 hse_pll 生效)")

set(N32_MIN_HEAP_SIZE  "0x200" CACHE STRING "链接期保留的最小堆空间")
set(N32_MIN_STACK_SIZE "0x800" CACHE STRING "链接期保留的最小栈空间")

# RT-Thread is the sole supported runtime; headless builds may disable LVGL.
set(N32_USE_RTTHREAD ON)
option(N32_USE_LVGL "Build LVGL display and button UI" ON)

# -----------------------------------------------------------------------------
# 引入固件库模块
# -----------------------------------------------------------------------------
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/n32g45x_sdk.cmake")

n32_resolve_variant(${N32_VARIANT} N32_FLASH_SIZE N32_RAM_SIZE)
message(STATUS "N32 target   : N32G452${N32_VARIANT}  Flash=${N32_FLASH_SIZE}  SRAM=${N32_RAM_SIZE}")

# -----------------------------------------------------------------------------
# CPU / 浮点选项
# -----------------------------------------------------------------------------
set(N32_CPU_FLAGS -mcpu=cortex-m4 -mthumb)

if(N32_FPU STREQUAL "hard")
    list(APPEND N32_CPU_FLAGS -mfpu=fpv4-sp-d16 -mfloat-abi=hard)
elseif(N32_FPU STREQUAL "softfp")
    list(APPEND N32_CPU_FLAGS -mfpu=fpv4-sp-d16 -mfloat-abi=softfp)
elseif(N32_FPU STREQUAL "soft")
    # 不追加任何 -mfpu/-mfloat-abi, 与厂商 GCC Makefile 行为一致 (纯软件浮点)
else()
    message(FATAL_ERROR "N32_FPU 取值非法: '${N32_FPU}' (可选 soft | softfp | hard)")
endif()

# -----------------------------------------------------------------------------
# 编译选项
# -----------------------------------------------------------------------------
# CPU / 浮点选项对所有语言生效 —— C 与汇编都需要 -mcpu/-mthumb
add_compile_options(${N32_CPU_FLAGS})

# 以下开关仅对 C 生效。
# 注意两点:
#   1. 必须用 $<COMPILE_LANGUAGE:C> 隔离; 传给汇编器会得到
#      "option is valid for C/C++ but not for Assembler" 告警。
#   2. 生成表达式的参数必须加引号。N32_CPU_FLAGS 这类值是 "a;b" 列表,
#      直接写 $<$<...>:${LIST}> 会被分号从中间劈开成两个残缺参数。
add_compile_options(
    "$<$<COMPILE_LANGUAGE:C>:-ffunction-sections>"   # 每个函数独立段, 配合 --gc-sections 剔除死代码
    "$<$<COMPILE_LANGUAGE:C>:-fdata-sections>"
    "$<$<COMPILE_LANGUAGE:C>:-fno-common>"
    "$<$<COMPILE_LANGUAGE:C>:-Wall>"
    "$<$<COMPILE_LANGUAGE:C>:-std=gnu11>"
)

set(CMAKE_C_FLAGS_DEBUG   "-Og -g3 -gdwarf-2")
set(CMAKE_C_FLAGS_RELEASE "-Os -g0 -DNDEBUG")
set(CMAKE_ASM_FLAGS_DEBUG   "-g3 -gdwarf-2")
set(CMAKE_ASM_FLAGS_RELEASE "")

# 未指定构建类型时默认 Debug (CMake 默认是空, 会导致无优化也无调试信息)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "" FORCE)
endif()

# -----------------------------------------------------------------------------
# 生成链接脚本 (按容量档位注入 Flash / SRAM 尺寸)
# -----------------------------------------------------------------------------
set(FLASH_ORIGIN   0x08000000)
set(RAM_ORIGIN     0x20000000)
set(FLASH_LENGTH   ${N32_FLASH_SIZE})
set(MIN_HEAP_SIZE  ${N32_MIN_HEAP_SIZE})
set(MIN_STACK_SIZE ${N32_MIN_STACK_SIZE})

math(EXPR RAM_LENGTH "${N32_RAM_SIZE} / 2" OUTPUT_FORMAT HEXADECIMAL)
set(RAM_TOTAL_LENGTH ${N32_RAM_SIZE})
set(N32_LINKER_SCRIPT "${CMAKE_BINARY_DIR}/n32g452_rtthread.ld")
configure_file(linker/n32g452_rtthread.ld.in "${N32_LINKER_SCRIPT}" @ONLY)

# -----------------------------------------------------------------------------
# 时钟配置宏
#   取值对应 system_n32g45x.c 内部定义:
#     SYSCLK_USE_HSI=0  SYSCLK_USE_HSE=1  SYSCLK_USE_HSI_PLL=2  SYSCLK_USE_HSE_PLL=3
# -----------------------------------------------------------------------------
if(N32_SYSCLK_SRC STREQUAL "hse_pll")
    set(N32_SYSCLK_SRC_NUM 3)
elseif(N32_SYSCLK_SRC STREQUAL "hsi_pll")
    set(N32_SYSCLK_SRC_NUM 2)
else()
    message(FATAL_ERROR "N32_SYSCLK_SRC 取值非法: '${N32_SYSCLK_SRC}'")
endif()

# hsi_pll 分支要求 PLL 倍频比 <= 32, 而 HSI=8MHz 且 2 分频后为 4MHz -> 上限 128MHz
if(N32_SYSCLK_SRC STREQUAL "hsi_pll" AND N32_SYSCLK_FREQ GREATER 128000000)
    message(FATAL_ERROR
        "hsi_pll 无法产生 ${N32_SYSCLK_FREQ} Hz: 倍频比上限 32, 即最高 128000000 Hz")
endif()

# -----------------------------------------------------------------------------
# 固件库静态库
# -----------------------------------------------------------------------------
n32_sdk_sources(N32_SDK_SOURCES)
# 启动文件必须直接编入可执行文件, 不能放进静态库 (否则可能不被拉取)
list(FILTER N32_SDK_SOURCES EXCLUDE REGEX "startup_.*\\.s$")

add_library(n32g45x_sdk STATIC ${N32_SDK_SOURCES})
n32_sdk_configure(n32g45x_sdk)
target_compile_definitions(n32g45x_sdk PRIVATE SYSCLK_SRC=${N32_SYSCLK_SRC_NUM}
    SYSCLK_FREQ=${N32_SYSCLK_FREQ} HSE_VALUE=${N32_HSE_VALUE})
target_compile_options(n32g45x_sdk PRIVATE -Wno-unused-parameter)

# -----------------------------------------------------------------------------
# 应用可执行文件
# -----------------------------------------------------------------------------
add_executable(${PROJECT_NAME}
    board/startup_n32g45x_rtthread.s
    src/syscalls.c)

n32_sdk_configure(${PROJECT_NAME})
target_include_directories(${PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/inc")

target_compile_definitions(${PROJECT_NAME} PRIVATE
    SYSCLK_SRC=${N32_SYSCLK_SRC_NUM}
    SYSCLK_FREQ=${N32_SYSCLK_FREQ}
    HSE_VALUE=${N32_HSE_VALUE}
    N32_VARIANT_${N32_VARIANT})

# ---- RT-Thread 内核 ----
file(GLOB RT_KERNEL_SOURCES CONFIGURE_DEPENDS
     "${N32_SDK_ROOT}/middlewares/rt-thread/src/*.c")

# ---- CPU 端口 (GCC) ----
set(RT_PORT_SOURCES
    "${N32_SDK_ROOT}/middlewares/rt-thread/libcpu/cortex-m4/cpuport.c"
    "${N32_SDK_ROOT}/middlewares/rt-thread/libcpu/cortex-m4/context_gcc.S")

# ---- SPI 框架 (rt_spi_bus / rt_spi_device / rt_spi_* API) ----
# 只取框架核心, 不含 enc28j60 / spi_msd / sfud 等具体器件驱动
set(RT_SPI_SOURCES
    "${N32_SDK_ROOT}/middlewares/rt-thread/components/drivers/spi/spi_core.c"
    "${N32_SDK_ROOT}/middlewares/rt-thread/components/drivers/spi/spi_dev.c")

# ---- 串口设备框架 ----
# board/drv_usart.c 实现 rt_uart_ops, 框架侧提供 rt_hw_serial_register /
# rt_hw_serial_isr / 接收环形缓冲。msh 的输入通道依赖它。
# serial.c 的 TX 分支用到 rt_completion, DMA 分支用到 rt_data_queue,
# 这两个在 components/drivers/src 下, 必须一起编, 否则链接期报未定义。
# 实际是否启用由 board/rtconfig.h 的 RT_USING_SERIAL 决定 (serial.c 内部有开关)。
set(RT_SERIAL_SOURCES
    "${N32_SDK_ROOT}/middlewares/rt-thread/components/drivers/serial/serial.c"
    "${N32_SDK_ROOT}/middlewares/rt-thread/components/drivers/src/completion.c"
    "${N32_SDK_ROOT}/middlewares/rt-thread/components/drivers/src/dataqueue.c")

# ---- PIN 设备框架 ----
# board/drv_gpio.c 提供 rt_pin_ops 实现, 框架侧是 misc/pin.c,
# 它实现 rt_device_pin_register / rt_pin_mode / rt_pin_read / rt_pin_write。
# 启用条件同样是 board/rtconfig.h 的 RT_USING_PIN。
set(RT_PIN_SOURCES
    "${N32_SDK_ROOT}/middlewares/rt-thread/components/drivers/misc/pin.c")

# ---- PWM 设备框架 ----
# board/drv_pwm.c 提供 rt_pwm_ops 实现(呼吸灯), 框架侧是 misc/rt_drv_pwm.c,
# 它实现 rt_device_pwm_register / rt_pwm_set / rt_pwm_enable。
# 启用条件同样是 board/rtconfig.h 的 RT_USING_PWM。
set(RT_PWM_SOURCES
    "${N32_SDK_ROOT}/middlewares/rt-thread/components/drivers/misc/rt_drv_pwm.c")

# Vendor RT-Thread 3.1.4 hwtimer: repair INFO_GET fallthrough in a build copy.
set(_hwtimer_source "${N32_SDK_ROOT}/middlewares/rt-thread/components/drivers/hwtimer/hwtimer.c")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_hwtimer_source}")
file(READ "${_hwtimer_source}" _hwtimer)
string(REPLACE "    case HWTIMER_CTRL_MODE_SET:" "    break;\n    case HWTIMER_CTRL_MODE_SET:" _hwtimer "${_hwtimer}")
file(WRITE "${CMAKE_BINARY_DIR}/rt_hwtimer.c" "${_hwtimer}")
target_sources(${PROJECT_NAME} PRIVATE "${CMAKE_BINARY_DIR}/rt_hwtimer.c")

# ---- FINSH / msh 命令行 ----
# cmd.c / shell.c / symbol.c 内部有 #ifdef RT_USING_FINSH 保护;
# 其余 finsh_*.c 只在 rtconfig.h 打开 RT_USING_FINSH 时才应参与编译。
# 当前固件固定启用 msh，作为上板诊断入口。
set(N32_RT_FINSH ON)
if(N32_RT_FINSH)
    set(RT_FINSH_SOURCES
        "${N32_SDK_ROOT}/middlewares/rt-thread/components/finsh/cmd.c"
        "${N32_SDK_ROOT}/middlewares/rt-thread/components/finsh/finsh_compiler.c"
        "${N32_SDK_ROOT}/middlewares/rt-thread/components/finsh/finsh_error.c"
        "${N32_SDK_ROOT}/middlewares/rt-thread/components/finsh/finsh_heap.c"
        "${N32_SDK_ROOT}/middlewares/rt-thread/components/finsh/finsh_init.c"
        "${N32_SDK_ROOT}/middlewares/rt-thread/components/finsh/finsh_node.c"
        "${N32_SDK_ROOT}/middlewares/rt-thread/components/finsh/finsh_ops.c"
        "${N32_SDK_ROOT}/middlewares/rt-thread/components/finsh/finsh_parser.c"
        "${N32_SDK_ROOT}/middlewares/rt-thread/components/finsh/finsh_token.c"
        "${N32_SDK_ROOT}/middlewares/rt-thread/components/finsh/finsh_var.c"
        "${N32_SDK_ROOT}/middlewares/rt-thread/components/finsh/finsh_vm.c"
        "${N32_SDK_ROOT}/middlewares/rt-thread/components/finsh/msh.c"
        "${N32_SDK_ROOT}/middlewares/rt-thread/components/finsh/shell.c"
        "${N32_SDK_ROOT}/middlewares/rt-thread/components/finsh/symbol.c")
    # msh_file.c 提供文件系统相关的 msh 命令, 本工程无 DFS, 不编译
else()
    set(RT_FINSH_SOURCES "")
endif()

# ---- 板级移植层 + 应用层 ----
file(GLOB RT_BOARD_SOURCES CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_SOURCE_DIR}/board/*.c")
# LVGL 移植层依赖 lvgl.h, 未启用 LVGL 时必须排除, 否则编译失败
if(NOT N32_USE_LVGL)
    list(FILTER RT_BOARD_SOURCES EXCLUDE REGEX "/lv_port_.*\\.c$")
endif()
file(GLOB RTAPP_SOURCES CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_SOURCE_DIR}/app/*.c")

target_sources(${PROJECT_NAME} PRIVATE
    ${RT_KERNEL_SOURCES}
    ${RT_PORT_SOURCES}
    ${RT_SPI_SOURCES}
    ${RT_SERIAL_SOURCES}
    ${RT_PIN_SOURCES}
    ${RT_PWM_SOURCES}
    ${RT_FINSH_SOURCES}
    ${RT_BOARD_SOURCES}
    ${RTAPP_SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/board"          # rtconfig.h 必须能被 <rtconfig.h> 找到
    "${N32_SDK_ROOT}/middlewares/rt-thread/include"
    "${N32_SDK_ROOT}/middlewares/rt-thread/components/drivers/include"
    # serial.h 里写的是 #include "completion.h" / "dataqueue.h", 相对 ipc 目录
    "${N32_SDK_ROOT}/middlewares/rt-thread/components/drivers/include/ipc"
    "${N32_SDK_ROOT}/middlewares/rt-thread/components/finsh")

# 只把 .S 汇编文件标成 ASM, 不能把 cpuport.c 也带上
set_source_files_properties(
    "${N32_SDK_ROOT}/middlewares/rt-thread/libcpu/cortex-m4/context_gcc.S"
    PROPERTIES LANGUAGE ASM)

target_compile_definitions(${PROJECT_NAME} PRIVATE N32_USE_RTTHREAD=1)

list(LENGTH RT_KERNEL_SOURCES _rt_kernel_count)
message(STATUS "RT-Thread: 内核 ${_rt_kernel_count} 个源文件 + 板级 + 应用层已启用")
message(STATUS "RT-Thread: RAM 低半 = ${RAM_LENGTH}, 高半作内核堆 (见 board/board.h)")

# -----------------------------------------------------------------------------
# LVGL 图形库接入
# -----------------------------------------------------------------------------
if(N32_USE_LVGL)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/lvgl/lvgl.h")
        message(FATAL_ERROR
            "找不到 third_party/lvgl。请先克隆 LVGL v8.3:\n"
            "  git clone --depth 1 --branch release/v8.3 "
            "https://gitee.com/mirrors/lvgl.git third_party/lvgl")
    endif()

    file(GLOB_RECURSE LVGL_SOURCES CONFIGURE_DEPENDS
         "${CMAKE_CURRENT_SOURCE_DIR}/third_party/lvgl/src/*.c")

    target_sources(${PROJECT_NAME} PRIVATE ${LVGL_SOURCES})

    # ---- LVGL 官方示例 (demos/ 不在 src/ 下, 要单独加) ----
    # 只接入"不带大图片资源"的两个:
    #   keypad_encoder  正好演示用按键/编码器操作控件 —— 与本工程的无触摸方案一致
    #   stress          压力测试, 用来验证长时间跑不出问题
    # 另外三个 demo 各自带着几百 KB ~ 几 MB 的图片数组 (widgets 的头像 1.6MB 源码,
    # music 的封面 9.6MB 源码, benchmark 的色轮 705KB 源码), 接进来会直接撑爆
    # 512KB Flash, 因此不接入。详见 README 的说明。
    file(GLOB_RECURSE LVGL_DEMO_SOURCES CONFIGURE_DEPENDS
         "${CMAKE_CURRENT_SOURCE_DIR}/third_party/lvgl/demos/keypad_encoder/*.c"
         "${CMAKE_CURRENT_SOURCE_DIR}/third_party/lvgl/demos/stress/*.c")
    if(LVGL_DEMO_SOURCES)
        target_sources(${PROJECT_NAME} PRIVATE ${LVGL_DEMO_SOURCES})
        set_source_files_properties(${LVGL_DEMO_SOURCES} PROPERTIES COMPILE_OPTIONS "-w")
        list(LENGTH LVGL_DEMO_SOURCES _lvgl_demo_count)
        message(STATUS "LVGL: ${_lvgl_demo_count} 个示例源文件 (keypad_encoder / stress)")
    endif()

    target_include_directories(${PROJECT_NAME} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/lvgl")   # lvgl.h
        # board/ 已在 RT-Thread 分支加入, lv_conf.h 放在那里

    target_compile_definitions(${PROJECT_NAME} PRIVATE
        LV_CONF_INCLUDE_SIMPLE=1     # 让 lvgl.h 用 #include "lv_conf.h" 找到 board/lv_conf.h
        APP_USE_LVGL=1)              # 应用层切换到带界面的分支

    # LVGL 是第三方代码, 屏蔽其自身告警, 保持应用告警可见（SDK 告警仍保留）
    set_source_files_properties(${LVGL_SOURCES} PROPERTIES COMPILE_OPTIONS "-w")

    list(LENGTH LVGL_SOURCES _lvgl_count)
    message(STATUS "LVGL: ${_lvgl_count} 个源文件已接入 (v8.3, 单缓冲 240x40)")
endif()

target_link_libraries(${PROJECT_NAME} PRIVATE n32g45x_sdk)

target_link_options(${PROJECT_NAME} PRIVATE
    ${N32_CPU_FLAGS}
    -T${N32_LINKER_SCRIPT}
    -Wl,--gc-sections
    -Wl,-Map=${CMAKE_BINARY_DIR}/${PROJECT_NAME}.map
    -Wl,--print-memory-usage
    # .data 的运行地址在 RAM、加载地址在 Flash, 于是覆盖 Flash 的那个 LOAD 段
    # 同时带 W 与 X 属性。裸机没有操作系统加载器, 这不构成问题, 关掉提示即可。
    -Wl,--no-warn-rwx-segments
    --specs=nosys.specs)

if(N32_USE_NANO)
    target_link_options(${PROJECT_NAME} PRIVATE --specs=nano.specs)
endif()

# 链接脚本变化时触发重新链接
set_target_properties(${PROJECT_NAME} PROPERTIES
    LINK_DEPENDS "${N32_LINKER_SCRIPT};${CMAKE_CURRENT_SOURCE_DIR}/cmake/verify_link.cmake")

# -----------------------------------------------------------------------------
# 生成 .hex / .bin 与体积报告
# -----------------------------------------------------------------------------
set(N32_HEX "${CMAKE_BINARY_DIR}/${PROJECT_NAME}.hex")
set(N32_BIN "${CMAKE_BINARY_DIR}/${PROJECT_NAME}.bin")
set(N32_ELF "${CMAKE_BINARY_DIR}/${PROJECT_NAME}.elf")

set(N32_VERIFY_COMMAND ${CMAKE_COMMAND}
    -DELF=$<TARGET_FILE:${PROJECT_NAME}>
    -DOBJDUMP=${CMAKE_OBJDUMP} -DNM=${CMAKE_NM} -DREADELF=${CMAKE_READELF}
    -DFLASH_ORIGIN=${FLASH_ORIGIN} -DFLASH_LEN=${FLASH_LENGTH}
    -DRAM_ORIGIN=${RAM_ORIGIN} -DRAM_LEN=${RAM_LENGTH} -DRAM_TOTAL_LEN=${RAM_TOTAL_LENGTH}
    -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/verify_link.cmake")

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${N32_VERIFY_COMMAND}
    COMMAND ${CMAKE_OBJCOPY} -O ihex   $<TARGET_FILE:${PROJECT_NAME}> ${N32_HEX}
    COMMAND ${CMAKE_OBJCOPY} -O binary -S $<TARGET_FILE:${PROJECT_NAME}> ${N32_BIN}
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${PROJECT_NAME}>
    COMMENT "生成 Intel HEX / 原始 BIN 并统计体积"
    VERBATIM)

# -----------------------------------------------------------------------------
# 便捷目标
# -----------------------------------------------------------------------------
add_custom_target(size
    COMMAND ${CMAKE_SIZE} -A -x $<TARGET_FILE:${PROJECT_NAME}>
    COMMAND ${CMAKE_SIZE} -B $<TARGET_FILE:${PROJECT_NAME}>
    DEPENDS ${PROJECT_NAME}
    COMMENT "详细段体积统计")

# 注意: add_custom_target 的 COMMAND 不经过 shell, 因此 "objdump ... > out.lst"
# 里的重定向不会生效 (会被当作 objdump 的参数)。改用一个 CMake 脚本,
# 通过 execute_process() 的 OUTPUT_FILE 完成重定向, 跨平台可用。
add_custom_target(disasm
    COMMAND ${CMAKE_COMMAND}
            -DOBJDUMP=${CMAKE_OBJDUMP}
            -DELF=$<TARGET_FILE:${PROJECT_NAME}>
            -DOUT=${CMAKE_BINARY_DIR}/${PROJECT_NAME}.lst
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/gen_disasm.cmake"
    DEPENDS ${PROJECT_NAME}
    COMMENT "反汇编 -> ${PROJECT_NAME}.lst")

# -----------------------------------------------------------------------------
#  链接脚本正确性检查
#
#  为什么值得单独做一个目标:
#      链接脚本写错往往【不会链接失败】, 而是"能烧进去、上电就死"或者
#      "跑一会儿莫名崩"。靠肉眼看 .ld 看不出来, 必须拿链接产物反查:
#          .data 是不是真的有两个地址(AT> FLASH 生效了吗)
#          _sidata 是不是真的等于 .data 的 LMA(启动拷贝源对不对)
#          .bss 会不会白占 Flash
#          KEEP() 收集的 .rti_fn / FSymTab 有没有被 --gc-sections 回收
#      这些检查全部基于 ELF 实测值, 不依赖人看。
# -----------------------------------------------------------------------------
add_custom_target(verify-ld
    COMMAND ${N32_VERIFY_COMMAND}
    DEPENDS ${PROJECT_NAME}
    COMMENT "检查链接脚本与启动代码是否一致")

message(STATUS "链接脚本     : ${N32_LINKER_SCRIPT}")
message(STATUS "输出         : ${N32_ELF} / .hex / .bin")
