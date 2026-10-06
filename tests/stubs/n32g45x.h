#ifndef MOCK_N32_H
#define MOCK_N32_H
#include <stdint.h>
typedef struct { volatile uint32_t PBSC,PBC,POD; } GPIO_Module;
extern GPIO_Module mock_ports[5];
#define GPIOA (&mock_ports[0])
#define GPIOB (&mock_ports[1])
#define GPIOC (&mock_ports[2])
#define GPIOD (&mock_ports[3])
#define GPIOE (&mock_ports[4])
#define GPIO_PIN_0 (1u<<0)
#define GPIO_PIN_1 (1u<<1)
#define GPIO_PIN_3 (1u<<3)
#define GPIO_PIN_4 (1u<<4)
#define GPIO_PIN_5 (1u<<5)
#define GPIO_PIN_6 (1u<<6)
#define GPIO_PIN_7 (1u<<7)
#define GPIO_PIN_9 (1u<<9)
#define GPIO_PIN_10 (1u<<10)
#define GPIO_PIN_11 (1u<<11)
#define GPIO_PIN_12 (1u<<12)
#define GPIO_RMP1_SPI3 0
#define GPIO_RMP_TIM4 0
#define RCC_APB2_PERIPH_AFIO 1
#define RCC_APB2_PERIPH_GPIOA 2
#define RCC_APB2_PERIPH_GPIOB 4
#define RCC_APB2_PERIPH_GPIOC 8
#define RCC_APB2_PERIPH_GPIOD 16
#define RCC_APB2_PERIPH_GPIOE 32
#define ENABLE 1
#define GPIO_Mode_Out_PP 1
#define GPIO_Speed_50MHz 1
typedef struct { uint32_t Pin,GPIO_Mode,GPIO_Speed; } GPIO_InitType;
typedef struct { uint32_t Pclk2Freq; } RCC_ClocksType;
extern uint32_t SystemCoreClock;
static inline void RCC_EnableAPB2PeriphClk(uint32_t c,int en) {(void)c;(void)en;}
static inline void GPIO_InitPeripheral(GPIO_Module *p,GPIO_InitType *g) {(void)p;(void)g;}
static inline void RCC_GetClocksFreqValue(RCC_ClocksType *c) {c->Pclk2Freq=64000000;}
#define __NOP() ((void)0)
#endif
