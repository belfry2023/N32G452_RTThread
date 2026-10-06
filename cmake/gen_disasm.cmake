# =============================================================================
#  生成反汇编清单 (由 `cmake --build ... --target disasm` 调用)
# -----------------------------------------------------------------------------
#  add_custom_target 的 COMMAND 不经过 shell, 无法直接用 ">" 重定向,
#  这里用 execute_process 的 OUTPUT_FILE 实现, 且 Windows / Linux 通用。
#
#  传入变量: OBJDUMP / ELF / OUT
# =============================================================================

if(NOT DEFINED OBJDUMP OR NOT DEFINED ELF OR NOT DEFINED OUT)
    message(FATAL_ERROR "gen_disasm.cmake 需要 -DOBJDUMP= -DELF= -DOUT= 三个参数")
endif()

execute_process(
    COMMAND "${OBJDUMP}" -h -S "${ELF}"
    OUTPUT_FILE "${OUT}"
    ERROR_VARIABLE _err
    RESULT_VARIABLE _rc)

if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "objdump 执行失败 (${_rc}): ${_err}")
endif()

message(STATUS "反汇编清单已生成: ${OUT}")
