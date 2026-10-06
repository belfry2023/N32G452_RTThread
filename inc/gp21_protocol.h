#ifndef GP21_PROTOCOL_H
#define GP21_PROTOCOL_H
#include <stdint.h>
#include <stddef.h>

#define GP21_RESET 0x50u
#define GP21_INIT 0x70u
#define GP21_STATUS_ERRORS 0x0600u
/* Digital external START -> first STOP1, calibrated measurement range 2.
 * Values use the 32-bit layout including the low ID byte. */
/**
 * @brief 生成 GP21 数字量程 2 的七个默认配置寄存器。
 * @param regs 可写的 7 个 uint32_t 数组，不能为空。
 * @details 用法：创建配置时调用，本函数只填数组，不访问硬件。
 *          动作：选择校准结果、外部 START 到第一个 STOP1、上升沿和 ALU/超时中断；低字节用于通信回读校验。
 */
void gp21_default_config(uint32_t regs[7]);
/**
 * @brief 将一个配置值编码为 GP21 SPI 写帧。
 * @param addr 寄存器 0..6；value 为 32 位完整值；frame 为至少 5 字节的输出缓冲。
 * @return 0=编码成功，-1=地址越界或输出为空。
 * @details 用法：先编码，再由调用者发送 frame；本函数是无硬件依赖的纯逻辑。
 *          动作：首字节写入 0x80|addr，后四字节按高字节在前排列。
 */
int gp21_write_frame(uint8_t addr, uint32_t value, uint8_t frame[5]);
/**
 * @brief 将 GP21 返回的四个大端字节合成为 32 位数。
 * @return 解码后的无符号数值。
 * @details 用法：输入指针必须指向至少 4 个有效字节；不校验空指针。
 *          动作：按 24、16、8、0 位移组合四字节，不执行有效性判断或单位换算。
 */
uint32_t gp21_decode_u32(const uint8_t bytes[4]);
/**
 * @brief 检查当前数字量程 2 结果是否符合驱动所用测量配置。
 * @param status 状态寄存器；result 为已解码的 RES0 原始数值。
 * @return 非 0=有效，0=错误状态、命中数量不符、零值或最高位置位。
 * @details 用法：原始结果换算前调用。
 *          动作：检查错误位、START/STOP 命中数以及结果范围；不验证外部接线或参考时钟的实际精度。
 */
int gp21_result_valid(uint16_t status, uint32_t result);
/**
 * @brief 将 GP21 校准后的 16.16 定点结果转换为皮秒。
 * @param raw 原始定点结果；reference_hz 为实际外部参考频率；divider 为参考分频 1、2 或 4。
 * @return 四舍五入后的皮秒数；参考频率为 0 或分频无效时返回 0。
 * @details 用法：先用 gp21_result_valid() 验证，再传入当前配置的参考频率和分频。
 *          动作：分别计算整数/小数部分，再按 10^12 × divider / reference_hz 换算，避免大数中间值溢出。
 */
uint64_t gp21_result_ps(uint32_t raw, uint32_t reference_hz, uint8_t divider);
#endif
