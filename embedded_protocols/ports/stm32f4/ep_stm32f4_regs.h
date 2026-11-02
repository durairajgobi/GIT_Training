/*
 * ep_stm32f4_regs.h - Minimal STM32F4 register map (RM0090) for the
 * register-level port. Self-contained so the port does not depend on a
 * particular CMSIS/HAL version; layouts are verified with static asserts.
 */
#ifndef EP_STM32F4_REGS_H
#define EP_STM32F4_REGS_H

#include <stddef.h>
#include <stdint.h>

#define REG volatile uint32_t

typedef struct {
    REG SR, DR, BRR, CR1, CR2, CR3, GTPR;
} stm32_usart_t;

typedef struct {
    REG CR1, CR2, SR, DR, CRCPR, RXCRCR, TXCRCR, I2SCFGR, I2SPR;
} stm32_spi_t;

typedef struct {
    REG MODER, OTYPER, OSPEEDR, PUPDR, IDR, ODR, BSRR, LCKR, AFR[2];
} stm32_gpio_t;

typedef struct {
    REG TIR, TDTR, TDLR, TDHR;
} stm32_can_txmb_t;

typedef struct {
    REG RIR, RDTR, RDLR, RDHR;
} stm32_can_rxfifo_t;

typedef struct {
    REG FR1, FR2;
} stm32_can_filter_t;

typedef struct {
    REG MCR, MSR, TSR, RF0R, RF1R, IER, ESR, BTR;
    uint32_t RESERVED0[88];
    stm32_can_txmb_t TX[3];
    stm32_can_rxfifo_t RX[2];
    uint32_t RESERVED1[12];
    REG FMR, FM1R;
    uint32_t RESERVED2;
    REG FS1R;
    uint32_t RESERVED3;
    REG FFA1R;
    uint32_t RESERVED4;
    REG FA1R;
    uint32_t RESERVED5[8];
    stm32_can_filter_t FB[28];
} stm32_can_t;

_Static_assert(offsetof(stm32_usart_t, CR3) == 0x14, "USART layout");
_Static_assert(offsetof(stm32_spi_t, DR) == 0x0C, "SPI layout");
_Static_assert(offsetof(stm32_gpio_t, BSRR) == 0x18, "GPIO layout");
_Static_assert(offsetof(stm32_can_t, TX) == 0x180, "CAN TX mailboxes");
_Static_assert(offsetof(stm32_can_t, RX) == 0x1B0, "CAN RX FIFOs");
_Static_assert(offsetof(stm32_can_t, FMR) == 0x200, "CAN filter master");
_Static_assert(offsetof(stm32_can_t, FA1R) == 0x21C, "CAN filter activation");
_Static_assert(offsetof(stm32_can_t, FB) == 0x240, "CAN filter banks");

/* via uintptr_t so the port also compile-checks on 64-bit hosts */
#define STM32_PERIPH(type, addr) ((type *)(uintptr_t)(addr))
#define STM32_USART2 STM32_PERIPH(stm32_usart_t, 0x40004400u)
#define STM32_SPI1   STM32_PERIPH(stm32_spi_t, 0x40013000u)
#define STM32_CAN1   STM32_PERIPH(stm32_can_t, 0x40006400u)
#define STM32_GPIO(port) STM32_PERIPH(stm32_gpio_t, 0x40020000u + 0x400u * (uint32_t)(port))

#define NVIC_ISER STM32_PERIPH(REG, 0xE000E100u)
#define NVIC_ICER STM32_PERIPH(REG, 0xE000E180u)
static inline void nvic_enable(unsigned irq) { NVIC_ISER[irq >> 5] = 1u << (irq & 31u); }
static inline void nvic_disable(unsigned irq) { NVIC_ICER[irq >> 5] = 1u << (irq & 31u); }

#define IRQ_CAN1_RX0 20u
#define IRQ_USART2   38u

/* USART */
#define USART_SR_PE   (1u << 0)
#define USART_SR_FE   (1u << 1)
#define USART_SR_NF   (1u << 2)
#define USART_SR_ORE  (1u << 3)
#define USART_SR_RXNE (1u << 5)
#define USART_SR_TC   (1u << 6)
#define USART_SR_TXE  (1u << 7)
#define USART_CR1_RE     (1u << 2)
#define USART_CR1_TE     (1u << 3)
#define USART_CR1_RXNEIE (1u << 5)
#define USART_CR1_TCIE   (1u << 6)
#define USART_CR1_TXEIE  (1u << 7)
#define USART_CR1_PS     (1u << 9)
#define USART_CR1_PCE    (1u << 10)
#define USART_CR1_M      (1u << 12)
#define USART_CR1_UE     (1u << 13)
#define USART_CR3_EIE    (1u << 0)
#define USART_CR3_RTSE   (1u << 8)
#define USART_CR3_CTSE   (1u << 9)

/* SPI */
#define SPI_CR1_CPHA     (1u << 0)
#define SPI_CR1_CPOL     (1u << 1)
#define SPI_CR1_MSTR     (1u << 2)
#define SPI_CR1_SPE      (1u << 6)
#define SPI_CR1_LSBFIRST (1u << 7)
#define SPI_CR1_SSI      (1u << 8)
#define SPI_CR1_SSM      (1u << 9)
#define SPI_CR1_DFF      (1u << 11)
#define SPI_SR_RXNE      (1u << 0)
#define SPI_SR_TXE       (1u << 1)
#define SPI_SR_BSY       (1u << 7)

/* bxCAN */
#define CAN_MCR_INRQ  (1u << 0)
#define CAN_MCR_SLEEP (1u << 1)
#define CAN_MCR_TXFP  (1u << 2)
#define CAN_MCR_ABOM  (1u << 6)
#define CAN_MSR_INAK  (1u << 0)
#define CAN_TSR_TME0  (1u << 26)
#define CAN_RF0R_FMP0 (3u << 0)
#define CAN_RF0R_RFOM0 (1u << 5)
#define CAN_IER_FMPIE0 (1u << 1)
#define CAN_ESR_BOFF  (1u << 2)
#define CAN_BTR_LBKM  (1u << 30)
#define CAN_BTR_SILM  (1u << 31)
#define CAN_TIR_TXRQ  (1u << 0)
#define CAN_TIR_RTR   (1u << 1)
#define CAN_TIR_IDE   (1u << 2)
#define CAN_FMR_FINIT (1u << 0)

#endif /* EP_STM32F4_REGS_H */
