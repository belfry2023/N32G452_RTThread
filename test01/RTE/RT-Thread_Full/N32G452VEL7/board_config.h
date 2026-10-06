#ifndef N32_BOARD_CONFIG_H
#define N32_BOARD_CONFIG_H

/* N32G452VE/LQFP100. All wiring below is a configurable DEFAULT, not a PCB claim.
 * SPI remapping and PWM timer/channel choices must follow the N32 AF table.
 * HSI/PLL frequency is selected by CMakePresets.json (default 128 MHz). */
#include "n32g45x.h"
#define BSP_PIN(port, bit) (((port) - 'A') * 16 + (bit))

#define BSP_GPIO_CLOCKS (RCC_APB2_PERIPH_GPIOA | RCC_APB2_PERIPH_GPIOB | \
    RCC_APB2_PERIPH_GPIOC | RCC_APB2_PERIPH_GPIOD | RCC_APB2_PERIPH_GPIOE | RCC_APB2_PERIPH_AFIO)

#define BSP_SPI3_SCK_PORT GPIOC
#define BSP_SPI3_SCK_PIN GPIO_PIN_3
#define BSP_SPI3_MISO_PORT GPIOA
#define BSP_SPI3_MISO_PIN GPIO_PIN_0
#define BSP_SPI3_MOSI_PORT GPIOA
#define BSP_SPI3_MOSI_PIN GPIO_PIN_1
#define BSP_SPI3_REMAP_ENABLE 0
#define BSP_SPI3_REMAP GPIO_RMP1_SPI3
#define BSP_TDC_CS_PORT GPIOC
#define BSP_TDC_CS_PIN GPIO_PIN_4
#define BSP_TDC_INT_PIN BSP_PIN('D', 13)
#define BSP_TDC_RESET_PIN BSP_PIN('B', 7) /* -1 if RSTN not connected */
#define BSP_TDC_EN_START_PIN (-1) /* -1: tie GP21 EN_START high externally */
#define BSP_TDC_EN_STOP1_PIN (-1) /* -1: tie GP21 EN_STOP1 high externally */
#define BSP_TDC_SPI_NAME "spi30"
#define BSP_TDC_DEVICE_NAME "tdc0"
#define BSP_TDC_SPI_HZ 2000000u
#define BSP_TDC_REF_HZ 5000000u /* External GP21 reference, NOT SPI SCK */
#define BSP_TDC_WAIT_MS 100u /* Host wait for external START/STOP */

/* GPIO 8080-I: module IM[3:0]=0001, 16-bit bus D0..D15 on a whole port. */
#define BSP_LCD_DATA_PORT GPIOE
#define BSP_LCD_WR_PORT GPIOD
#define BSP_LCD_WR_PIN GPIO_PIN_5
#define BSP_LCD_RS_PORT GPIOD
#define BSP_LCD_RS_PIN GPIO_PIN_11
#define BSP_LCD_CS_PORT GPIOD
#define BSP_LCD_CS_PIN GPIO_PIN_7
#define BSP_LCD_RESET_PORT GPIOD
#define BSP_LCD_RESET_PIN GPIO_PIN_6
#define BSP_LCD_RD_PORT GPIOD
#define BSP_LCD_RD_PIN GPIO_PIN_4
#define BSP_LCD_BL_PORT GPIOD
#define BSP_LCD_BL_PIN GPIO_PIN_3 /* Separate from RD; drive a backlight transistor */
#define BSP_LCD_MADCTL 0x00u
#define BSP_LCD_INVERT 1

#define BSP_KEY_PREV_PIN BSP_PIN('C', 5)
#define BSP_KEY_NEXT_PIN BSP_PIN('C', 6)
#define BSP_KEY_ENTER_PIN BSP_PIN('C', 7)

#define BSP_PWM3_PORT GPIOB
#define BSP_PWM3_PINS (GPIO_PIN_0 | GPIO_PIN_1) /* TIM3 CH3/CH4, default map */
#define BSP_PWM4_PORT GPIOD
#define BSP_PWM4_PIN GPIO_PIN_12 /* TIM4 CH1, remapped */
#define BSP_PWM4_REMAP GPIO_RMP_TIM4
#define BSP_CONSOLE_PORT GPIOA
#define BSP_CONSOLE_TX GPIO_PIN_9
#define BSP_CONSOLE_RX GPIO_PIN_10
#define BSP_CONSOLE_BAUD 115200u

#endif
