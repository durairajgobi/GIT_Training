/*
 * STM32F4 USART2 backend for ep_uart_t (interrupt driven, no HAL).
 *
 *   ep_stm32f4_uart_init(&uart, &cfg, 42000000);      // APB1 = 42 MHz
 *   ep_serial_line_init(&rs485, &uart, &line_cfg, &clock);
 *
 * TXE feeds bytes; when the ring is empty TXE is masked and TC is enabled.
 * TC fires after the last STOP bit -> ep_uart_isr_tx_complete() -> the
 * RS-485 adapter releases DE at exactly the right moment.
 */
#include "ep/ep_uart.h"
#include "ep_stm32f4_regs.h"

static ep_uart_t *g_uart2;
static uint32_t g_pclk;
static uint8_t g_rx[256], g_tx[256];

static ep_status_t u2_configure(void *ctx, const ep_uart_config_t *cfg)
{
    stm32_usart_t *u = ctx;
    uint32_t brr;
    int32_t err_ppm;
    /* OVER16: BRR = mantissa:fraction(4 bits) = USARTDIV * 16 */
    if (ep_uart_calc_divisor(g_pclk, cfg->baud, 16, 4, &brr, &err_ppm) != EP_OK || brr > 0xFFFF)
        return EP_ERR_PARAM;
    if (err_ppm > 20000 || err_ppm < -20000)
        return EP_ERR_UNSUPPORTED; /* > 2 % baud error will not work */

    uint32_t cr1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_RXNEIE;
    /* M=1 when the frame carries 9 bits including parity */
    unsigned frame_bits = cfg->data_bits + (cfg->parity != EP_PARITY_NONE ? 1u : 0u);
    if (frame_bits == 9)
        cr1 |= USART_CR1_M;
    else if (frame_bits != 8)
        return EP_ERR_UNSUPPORTED;
    if (cfg->parity != EP_PARITY_NONE)
        cr1 |= USART_CR1_PCE | (cfg->parity == EP_PARITY_ODD ? USART_CR1_PS : 0u);

    u->CR1 = 0;
    u->BRR = brr;
    u->CR2 = cfg->stop_bits == 2 ? (2u << 12) : 0u;
    u->CR3 = USART_CR3_EIE | (cfg->flow == EP_FLOW_RTSCTS ? (USART_CR3_RTSE | USART_CR3_CTSE) : 0u);
    u->CR1 = cr1 | USART_CR1_UE;
    return EP_OK;
}

static void u2_tx_kick(void *ctx)
{
    stm32_usart_t *u = ctx;
    u->CR1 |= USART_CR1_TXEIE;
}

static void u2_crit_enter(void *ctx) { (void)ctx; nvic_disable(IRQ_USART2); }
static void u2_crit_exit(void *ctx) { (void)ctx; nvic_enable(IRQ_USART2); }

static const ep_uart_ops_t usart2_ops = {
    .configure = u2_configure,
    .tx_kick = u2_tx_kick,
    .critical_enter = u2_crit_enter,
    .critical_exit = u2_crit_exit,
};

ep_status_t ep_stm32f4_uart_init(ep_uart_t *uart, const ep_uart_config_t *cfg, uint32_t pclk_hz);

ep_status_t ep_stm32f4_uart_init(ep_uart_t *uart, const ep_uart_config_t *cfg, uint32_t pclk_hz)
{
    /* Clock enable (RCC_APB1ENR.USART2EN) and PA2/PA3 AF7 are board setup. */
    g_pclk = pclk_hz;
    g_uart2 = uart;
    ep_status_t st = ep_uart_init(uart, &usart2_ops, STM32_USART2, cfg, g_rx, sizeof(g_rx), g_tx, sizeof(g_tx));
    if (st == EP_OK)
        nvic_enable(IRQ_USART2);
    return st;
}

void USART2_IRQHandler(void);

void USART2_IRQHandler(void)
{
    stm32_usart_t *u = STM32_USART2;
    uint32_t sr = u->SR;

    if (sr & (USART_SR_RXNE | USART_SR_ORE | USART_SR_FE | USART_SR_PE | USART_SR_NF)) {
        uint8_t err = 0;
        if (sr & USART_SR_ORE) err |= EP_UART_ERR_OVERRUN;
        if (sr & USART_SR_FE) err |= EP_UART_ERR_FRAMING;
        if (sr & USART_SR_PE) err |= EP_UART_ERR_PARITY;
        if (sr & USART_SR_NF) err |= EP_UART_ERR_NOISE;
        uint8_t byte = (uint8_t)u->DR; /* SR-then-DR read clears the error flags */
        ep_uart_isr_rx(g_uart2, byte, err);
    }

    if ((u->CR1 & USART_CR1_TXEIE) && (sr & USART_SR_TXE)) {
        uint8_t b;
        if (ep_uart_isr_tx_next(g_uart2, &b)) {
            u->DR = b;
        } else {
            u->CR1 = (u->CR1 & ~USART_CR1_TXEIE) | USART_CR1_TCIE;
        }
    }

    if ((u->CR1 & USART_CR1_TCIE) && (sr & USART_SR_TC)) {
        u->CR1 &= ~USART_CR1_TCIE;
        u->SR = ~USART_SR_TC; /* rc_w0 */
        ep_uart_isr_tx_complete(g_uart2);
    }
}
