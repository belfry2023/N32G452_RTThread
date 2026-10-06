# =============================================================================
#  Nations N32G45x 固件库接入模块
# -----------------------------------------------------------------------------
#  提供两个函数:
#     n32_sdk_configure(<target>)   仅注入宏定义与头文件搜索路径
#     n32_sdk_sources(<out_var>)    收集需要编译的固件库源文件
# =============================================================================

# -----------------------------------------------------------------------------
# 器件容量档位 -> Flash / SRAM 尺寸
#
#   variant   FLASH           SRAM            代表型号
#   xB        128K (0x20000)  80K  (0x14000)  N32G452CBL7 / RBL7 / MBL7
#   xC        256K (0x40000)  144K (0x24000)  N32G452CCL7 / RCL7 / MCL7 / VCL7 / QCL7
#   xE        512K (0x80000)  144K (0x24000)  N32G452CEL7 / REL7 / MEL7 / VEL7 / QEL7
#
# 注: 这里刻意用 if/elseif 而非"嵌套列表查表" —— CMake 的 CACHE 变量只保存
#     扁平字符串, 会把 "xB;0x20000;0x14000" 这类内层分号展开成独立元素,
#     导致 foreach(IN LISTS) 逐项遍历时拿不到成组的三元组。
# -----------------------------------------------------------------------------
function(n32_resolve_variant variant out_flash out_ram)
    if(variant STREQUAL "xB")
        set(_flash 0x20000)
        set(_ram   0x14000)
    elseif(variant STREQUAL "xC")
        set(_flash 0x40000)
        set(_ram   0x24000)
    elseif(variant STREQUAL "xE")
        set(_flash 0x80000)
        set(_ram   0x24000)
    else()
        message(FATAL_ERROR
            "未知的 N32_VARIANT='${variant}'。可选值: "
            "xB (128K Flash/80K SRAM) | xC (256K/144K) | xE (512K/144K)")
    endif()

    set(${out_flash} ${_flash} PARENT_SCOPE)
    set(${out_ram}   ${_ram}   PARENT_SCOPE)
endfunction()

# -----------------------------------------------------------------------------
# 向目标注入 SDK 头文件路径与器件宏
# -----------------------------------------------------------------------------
function(n32_sdk_configure target)
    set(sdk "${N32_SDK_ROOT}")

    if(NOT EXISTS "${sdk}/firmware/CMSIS/device/n32g45x.h")
        message(FATAL_ERROR
            "在 '${sdk}' 下找不到 N32G45x 固件库。\n"
            "请确认 Nations.N32G45x_Library.<版本> 已解压, 或用 -DN32_SDK_ROOT=<路径> 指定。")
    endif()

    target_include_directories(${target} PUBLIC
        "${sdk}/firmware/CMSIS/core"                          # core_cm4.h / cmsis_gcc.h
        "${sdk}/firmware/CMSIS/device"                        # n32g45x.h / system_*.h / n32g45x_conf.h
        "${sdk}/firmware/n32g45x_std_periph_driver/inc"       # 标准外设驱动头文件
    )

    # 器件宏: 三个都由官方 CMSIS-Pack 的 <compile define="..."> 指定
    target_compile_definitions(${target} PUBLIC
        N32G45X                 # 器件大类, n32g45x.h 依赖它选择内核配置
        N32G452                 # 具体子系列
        USE_STDPERIPH_DRIVER    # 启用 n32g45x_conf.h 中的标准外设驱动
    )

    # 注意: 不定义 N32G457_ETH —— 该宏用于带以太网 MAC 的 N32G457。
    #       N32G452 无 ETH 外设, 定义它会导致 n32g45x_conf.h 引入错误的头文件。
endfunction()

# -----------------------------------------------------------------------------
# 收集固件库源文件
# -----------------------------------------------------------------------------
function(n32_sdk_sources out_var)
    set(sdk "${N32_SDK_ROOT}")
    set(srcs
        "${sdk}/firmware/CMSIS/device/system_n32g45x.c"
        "${sdk}/firmware/CMSIS/device/startup/startup_n32g45x_gcc.s"
    )

    # 标准外设驱动: 收集全部 .c
    file(GLOB _drivers "${sdk}/firmware/n32g45x_std_periph_driver/src/*.c")

    # 排除以太网驱动:
    #   n32g45x_eth.c / n32g457_eth.c 依赖 lwIP 协议栈, 且 N32G452 无 ETH 外设。
    #   厂商 GCC Makefile 同样将其排除 (filter-out %/n32g45x_eth.c)。
    list(FILTER _drivers EXCLUDE REGEX ".*/(n32g45x_eth|n32g457_eth)\\.c$")

    list(APPEND srcs ${_drivers})
    set(${out_var} ${srcs} PARENT_SCOPE)
endfunction()
