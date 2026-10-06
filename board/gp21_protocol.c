#include "gp21_protocol.h"

/**
 * @brief 生成 GP21 数字量程 2 的七个默认配置寄存器。
 * @param regs 可写的 7 个 uint32_t 数组，不能为空。
 * @details 用法：创建配置时调用，本函数只填数组，不访问硬件。
 *          动作：选择校准结果、外部 START 到第一个 STOP1、上升沿和 ALU/超时中断；低字节用于通信回读校验。
 */
void gp21_default_config(uint32_t regs[7])
{
    /* GP21 DB_GP21_en V1.6, sections 3.1 and 4.2.
     * Fire generator off; DIV_FIRE=2 retained; CLKHS continuously on;
     * calibrated range 2, automatic calibration, rising edges.
     * Range 2 ALU computes HIT2-HIT1: HIT1=1 (START), HIT2=2 (STOP1).
     * DB_GP21_en V1.6 register-1 table (3-6), flow (4-13), example (6-2).
     * HITIN1=2 includes START + one STOP; digital frontend. */
    regs[0] = 0x020668a0u;
    regs[1] = 0x210200a1u;
    regs[2] = 0xa00000a2u; /* ALU + timeout interrupt, all DELVAL=0 */
    regs[3] = 0x380000a3u; /* error result; longest range-2 timeout */
    regs[4] = 0x200000a4u; /* retain reserved default bit 29 */
    regs[5] = 0x000000a5u;
    regs[6] = 0x000000a6u;
}

/**
 * @brief 将一个配置值编码为 GP21 SPI 写帧。
 * @param addr 寄存器 0..6；value 为 32 位完整值；frame 为至少 5 字节的输出缓冲。
 * @return 0=编码成功，-1=地址越界或输出为空。
 * @details 用法：先编码，再由调用者发送 frame；本函数是无硬件依赖的纯逻辑。
 *          动作：首字节写入 0x80|addr，后四字节按高字节在前排列。
 */
int gp21_write_frame(uint8_t addr, uint32_t value, uint8_t frame[5])
{
    if (addr > 6 || !frame) return -1;
    frame[0] = (uint8_t)(0x80u | addr);
    frame[1] = (uint8_t)(value >> 24);
    frame[2] = (uint8_t)(value >> 16);
    frame[3] = (uint8_t)(value >> 8);
    frame[4] = (uint8_t)value;
    return 0;
}

/**
 * @brief 将 GP21 返回的四个大端字节合成为 32 位数。
 * @return 解码后的无符号数值。
 * @details 用法：输入指针必须指向至少 4 个有效字节；不校验空指针。
 *          动作：按 24、16、8、0 位移组合四字节，不执行有效性判断或单位换算。
 */
uint32_t gp21_decode_u32(const uint8_t b[4])
{
    return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) |
           ((uint32_t)b[2] << 8) | b[3];
}

/**
 * @brief 检查当前数字量程 2 结果是否符合驱动所用测量配置。
 * @param status 状态寄存器；result 为已解码的 RES0 原始数值。
 * @return 非 0=有效，0=错误状态、命中数量不符、零值或最高位置位。
 * @details 用法：原始结果换算前调用。
 *          动作：检查错误位、START/STOP 命中数以及结果范围；不验证外部接线或参考时钟的实际精度。
 */
int gp21_result_valid(uint16_t status, uint32_t result)
{
    return !(status & GP21_STATUS_ERRORS) && (status & 7u) == 1u &&
           ((status >> 3) & 7u) == 2u && result > 0 && !(result & 0x80000000u);
}

/**
 * @brief 将 GP21 校准后的 16.16 定点结果转换为皮秒。
 * @param raw 原始定点结果；reference_hz 为实际外部参考频率；divider 为参考分频 1、2 或 4。
 * @return 四舍五入后的皮秒数；参考频率为 0 或分频无效时返回 0。
 * @details 用法：先用 gp21_result_valid() 验证，再传入当前配置的参考频率和分频。
 *          动作：分别计算整数/小数部分，再按 10^12 × divider / reference_hz 换算，避免大数中间值溢出。
 */
uint64_t gp21_result_ps(uint32_t raw, uint32_t reference_hz, uint8_t divider)
{
    if (!reference_hz || (divider != 1 && divider != 2 && divider != 4)) return 0;
    /* Split to avoid overflowing uint64_t for large 16.16 values. */
    uint64_t scale = 1000000000000ULL * divider;
    uint64_t whole = (uint64_t)(raw >> 16) * scale;
    uint64_t fraction = ((uint64_t)(raw & 0xffffu) * scale + 32768u) / 65536u;
    return (whole + fraction + reference_hz / 2u) / reference_hz;
}
