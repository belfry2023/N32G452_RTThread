# =============================================================================
#  arm-none-eabi 交叉编译工具链定义  (N32G452 / Cortex-M4F)
# -----------------------------------------------------------------------------
#  用法:
#     cmake -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi-toolchain.cmake ..
#  通常不直接调用, 而是通过 CMakePresets.json 自动加载。
# =============================================================================

set(CMAKE_SYSTEM_NAME      Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

# 交叉编译产物无法在本机执行, 因此编译器探测只做编译不做链接。
# 必须在 project() 之前设置。
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# -----------------------------------------------------------------------------
# 工具链前缀 (可覆盖, 例如换成 xpack / ARM 官方工具链)
# -----------------------------------------------------------------------------
set(N32_TOOLCHAIN_PREFIX "arm-none-eabi-"
    CACHE STRING "GNU Arm Embedded 工具链可执行文件前缀")

find_program(N32_CC      ${N32_TOOLCHAIN_PREFIX}gcc      REQUIRED)
find_program(N32_CXX     ${N32_TOOLCHAIN_PREFIX}g++      REQUIRED)
find_program(N32_ASM     ${N32_TOOLCHAIN_PREFIX}gcc      REQUIRED)
find_program(N32_OBJCOPY ${N32_TOOLCHAIN_PREFIX}objcopy  REQUIRED)
find_program(N32_OBJDUMP ${N32_TOOLCHAIN_PREFIX}objdump  REQUIRED)
find_program(N32_SIZE    ${N32_TOOLCHAIN_PREFIX}size     REQUIRED)
find_program(N32_NM      ${N32_TOOLCHAIN_PREFIX}nm       REQUIRED)
find_program(N32_READELF ${N32_TOOLCHAIN_PREFIX}readelf  REQUIRED)

# -----------------------------------------------------------------------------
# GDB: 优先 arm-none-eabi-gdb; 若工具链未附带(如 msys2 的 arm-none-eabi-gcc 包
#      不含 gdb), 则回退到 gdb-multiarch —— 它同样支持 ARM 目标。
# -----------------------------------------------------------------------------
# 先在编译器同目录找 (官方 GNU Arm Embedded 工具链自带 arm-none-eabi-gdb);
# 同目录没有才退到 PATH, 并优先 gdb-multiarch (版本通常远新于第三方 IDE 捆绑的 GDB)。
get_filename_component(_n32_bin_dir "${N32_CC}" DIRECTORY)

find_program(N32_GDB
    NAMES ${N32_TOOLCHAIN_PREFIX}gdb
    HINTS "${_n32_bin_dir}"
    NO_DEFAULT_PATH)

if(NOT N32_GDB)
    find_program(N32_GDB NAMES gdb-multiarch ${N32_TOOLCHAIN_PREFIX}gdb)
endif()

if(NOT N32_GDB)
    message(WARNING
        "未找到 arm-none-eabi-gdb 或 gdb-multiarch, 调试功能将不可用。\n"
        "  msys2 用户可执行: pacman -S mingw-w64-x86_64-gdb-multiarch")
endif()

set(CMAKE_C_COMPILER   "${N32_CC}"      CACHE FILEPATH "" FORCE)
set(CMAKE_CXX_COMPILER "${N32_CXX}"     CACHE FILEPATH "" FORCE)
set(CMAKE_ASM_COMPILER "${N32_ASM}"     CACHE FILEPATH "" FORCE)
set(CMAKE_OBJCOPY      "${N32_OBJCOPY}" CACHE FILEPATH "" FORCE)
set(CMAKE_OBJDUMP      "${N32_OBJDUMP}" CACHE FILEPATH "" FORCE)
set(CMAKE_SIZE         "${N32_SIZE}"    CACHE FILEPATH "" FORCE)
set(CMAKE_NM           "${N32_NM}"      CACHE FILEPATH "" FORCE)
set(CMAKE_READELF      "${N32_READELF}" CACHE FILEPATH "" FORCE)
if(N32_GDB)
    set(CMAKE_GDB "${N32_GDB}" CACHE FILEPATH "" FORCE)
endif()

# -----------------------------------------------------------------------------
# 查找规则: 程序在本机找, 头文件/库只在目标工具链里找
# -----------------------------------------------------------------------------
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# -----------------------------------------------------------------------------
# 汇总信息
# -----------------------------------------------------------------------------
# 工具链文件会被 C / ASM 两次语言探测各加载一次, 用标志位避免重复打印
if(NOT DEFINED _N32_TOOLCHAIN_REPORTED)
    set(_N32_TOOLCHAIN_REPORTED TRUE)

    execute_process(COMMAND "${N32_CC}" -dumpversion
                    OUTPUT_VARIABLE _n32_gcc_ver
                    OUTPUT_STRIP_TRAILING_WHITESPACE)
    message(STATUS "N32 toolchain: arm-none-eabi-gcc ${_n32_gcc_ver}")
    message(STATUS "N32 GDB      : ${N32_GDB}")
endif()
