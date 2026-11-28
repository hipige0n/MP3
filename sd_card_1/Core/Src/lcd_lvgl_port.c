#include "mp3_player.h"
/* Standalone ST7789-family SPI hello world, STM32F407 / HSE 8MHz.
 * Software SPI on PE7(SCK), PE8(SDA/MOSI), PE9(CS), PE10(RESET), PE11(RS/DC).
 * No audio/SD/TP/UART/DMA. Bare-panel power/backlight require external wiring.
 */
#include "main.h"
#include "lvgl.h"

void SystemClock_Config(void);
#define SCK GPIO_PIN_7
#define MOSI GPIO_PIN_8
#define CS GPIO_PIN_9
#define RST GPIO_PIN_10
#define DC GPIO_PIN_11
static void pin(uint16_t p,int high){GPIOE->BSRR=high?p:((uint32_t)p<<16);}
/* Each table entry lowers SCK and sets MOSI in one atomic BSRR write.
 * Eight explicit NOPs per phase keep setup/high times conservative for Dupont wiring.
 */
static const uint32_t spi_nibble[16][4] = {
    {(SCK << 16) | (MOSI << 16), (SCK << 16) | (MOSI << 16), (SCK << 16) | (MOSI << 16), (SCK << 16) | (MOSI << 16)},
    {(SCK << 16) | (MOSI << 16), (SCK << 16) | (MOSI << 16), (SCK << 16) | (MOSI << 16), (SCK << 16) | MOSI},
    {(SCK << 16) | (MOSI << 16), (SCK << 16) | (MOSI << 16), (SCK << 16) | MOSI, (SCK << 16) | (MOSI << 16)},
    {(SCK << 16) | (MOSI << 16), (SCK << 16) | (MOSI << 16), (SCK << 16) | MOSI, (SCK << 16) | MOSI},
    {(SCK << 16) | (MOSI << 16), (SCK << 16) | MOSI, (SCK << 16) | (MOSI << 16), (SCK << 16) | (MOSI << 16)},
    {(SCK << 16) | (MOSI << 16), (SCK << 16) | MOSI, (SCK << 16) | (MOSI << 16), (SCK << 16) | MOSI},
    {(SCK << 16) | (MOSI << 16), (SCK << 16) | MOSI, (SCK << 16) | MOSI, (SCK << 16) | (MOSI << 16)},
    {(SCK << 16) | (MOSI << 16), (SCK << 16) | MOSI, (SCK << 16) | MOSI, (SCK << 16) | MOSI},
    {(SCK << 16) | MOSI, (SCK << 16) | (MOSI << 16), (SCK << 16) | (MOSI << 16), (SCK << 16) | (MOSI << 16)},
    {(SCK << 16) | MOSI, (SCK << 16) | (MOSI << 16), (SCK << 16) | (MOSI << 16), (SCK << 16) | MOSI},
    {(SCK << 16) | MOSI, (SCK << 16) | (MOSI << 16), (SCK << 16) | MOSI, (SCK << 16) | (MOSI << 16)},
    {(SCK << 16) | MOSI, (SCK << 16) | (MOSI << 16), (SCK << 16) | MOSI, (SCK << 16) | MOSI},
    {(SCK << 16) | MOSI, (SCK << 16) | MOSI, (SCK << 16) | (MOSI << 16), (SCK << 16) | (MOSI << 16)},
    {(SCK << 16) | MOSI, (SCK << 16) | MOSI, (SCK << 16) | (MOSI << 16), (SCK << 16) | MOSI},
    {(SCK << 16) | MOSI, (SCK << 16) | MOSI, (SCK << 16) | MOSI, (SCK << 16) | (MOSI << 16)},
    {(SCK << 16) | MOSI, (SCK << 16) | MOSI, (SCK << 16) | MOSI, (SCK << 16) | MOSI}
};
__STATIC_FORCEINLINE void spi_phase_delay(void)
{
    __NOP(); __NOP(); __NOP(); __NOP();
    __NOP(); __NOP(); __NOP(); __NOP();
}
__STATIC_FORCEINLINE void spi_bit(uint32_t state)
{
    GPIOE->BSRR = state;
    spi_phase_delay();
    GPIOE->BSRR = SCK;
    spi_phase_delay();
}
__STATIC_FORCEINLINE void spi_nibble_send(const uint32_t *states)
{
    spi_bit(states[0]);
    spi_bit(states[1]);
    spi_bit(states[2]);
    spi_bit(states[3]);
}
static void byte(uint8_t value)
{
    spi_nibble_send(spi_nibble[value >> 4]);
    spi_nibble_send(spi_nibble[value & 15U]);
    GPIOE->BSRR = (uint32_t)SCK << 16;
}
static void cmd(uint8_t b){pin(CS,0);pin(DC,0);byte(b);pin(CS,1);}
static void data(uint8_t b){pin(CS,0);pin(DC,1);byte(b);pin(CS,1);}
static void window(unsigned x0,unsigned y0,unsigned x1,unsigned y1){
 cmd(0x2a);data(x0>>8);data(x0);data(x1>>8);data(x1);
 cmd(0x2b);data(y0>>8);data(y0);data(y1>>8);data(y1);cmd(0x2c);
}
/* RGB565 draw buffer for ten rows. The panel receives the high byte first. */
static __ALIGNED(4) uint16_t lcd_buffer[240U * 10U];

static void lcd_flush(lv_display_t *display, const lv_area_t *area, uint8_t *pixels)
{
    /* Ten-row transfers bound LCD blocking. Service audio between chunks. */
    MP3_Process();
    const uint16_t *rgb565 = (const uint16_t *)pixels;
    uint32_t count = (uint32_t)lv_area_get_width(area) * lv_area_get_height(area);
    window(area->x1, area->y1, area->x2, area->y2);
    pin(CS, 0);
    pin(DC, 1);
    for (uint32_t i = 0; i < count; ++i) {
        byte((uint8_t)(rgb565[i] >> 8));
        byte((uint8_t)rgb565[i]);
    }
    pin(CS, 1);
    MP3_Process();
    lv_display_flush_ready(display);
}

/* FT6336U: PB6=SCL, PB7=SDA, PE12=RESET. Poll first touch point. */
#define TP_SCL GPIO_PIN_6
#define TP_SDA GPIO_PIN_7
#define TP_RST GPIO_PIN_12
#define TP_ADDRESS 0x38U

static void tp_line(uint16_t line, bool high)
{
    GPIOB->BSRR = high ? line : ((uint32_t)line << 16);
}
static void tp_delay(void)
{
    for (volatile unsigned i = 0; i < 100; ++i) __NOP();
}
static bool tp_clock_high(void)
{
    tp_line(TP_SCL, true);
    for (unsigned i = 0; i < 1000; ++i) {
        if (GPIOB->IDR & TP_SCL) {
            tp_delay();
            return true;
        }
    }
    return false;
}
static bool tp_start(void)
{
    tp_line(TP_SDA, true);
    tp_delay();
    if (!tp_clock_high() || !(GPIOB->IDR & TP_SDA)) return false;
    tp_line(TP_SDA, false);
    tp_delay();
    tp_line(TP_SCL, false);
    return true;
}
static void tp_stop(void)
{
    tp_line(TP_SCL, false);
    tp_line(TP_SDA, false);
    tp_delay();
    (void)tp_clock_high();
    tp_line(TP_SDA, true);
    tp_delay();
}
static bool tp_write(uint8_t value)
{
    for (unsigned i = 0; i < 8; ++i) {
        tp_line(TP_SDA, (value & 0x80U) != 0);
        tp_delay();
        if (!tp_clock_high()) return false;
        tp_line(TP_SCL, false);
        value <<= 1;
    }
    tp_line(TP_SDA, true);
    tp_delay();
    if (!tp_clock_high()) return false;
    bool acknowledged = (GPIOB->IDR & TP_SDA) == 0;
    tp_line(TP_SCL, false);
    return acknowledged;
}
static bool tp_read(uint8_t *value, bool acknowledge)
{
    uint8_t result = 0;
    tp_line(TP_SDA, true);
    for (unsigned i = 0; i < 8; ++i) {
        tp_delay();
        if (!tp_clock_high()) return false;
        result = (uint8_t)((result << 1) | ((GPIOB->IDR & TP_SDA) != 0));
        tp_line(TP_SCL, false);
    }
    tp_line(TP_SDA, !acknowledge);
    tp_delay();
    if (!tp_clock_high()) return false;
    tp_line(TP_SCL, false);
    tp_line(TP_SDA, true);
    *value = result;
    return true;
}
static bool tp_read_point(uint8_t point[5])
{
    bool ok = tp_start() && tp_write(TP_ADDRESS << 1) && tp_write(0x02)
           && tp_start() && tp_write((TP_ADDRESS << 1) | 1U);
    for (unsigned i = 0; ok && i < 5; ++i) {
        ok = tp_read(&point[i], i < 4);
    }
    tp_stop();
    return ok;
}
static void touch_read(lv_indev_t *input, lv_indev_data_t *data)
{
    (void)input;
    static lv_point_t last_point;
    uint8_t point[5];
    data->state = LV_INDEV_STATE_RELEASED;
    data->point = last_point;
    if (!tp_read_point(point) || (point[0] & 0x0FU) == 0 || (point[1] >> 6) == 1) return;

    unsigned x = ((point[1] & 0x0FU) << 8) | point[2];
    unsigned y = ((point[3] & 0x0FU) << 8) | point[4];
    if (x >= 240 || y >= 320) return;
    last_point.x = x;
    last_point.y = y;
    data->point = last_point;
    data->state = LV_INDEV_STATE_PRESSED;
}
static void touch_init(lv_display_t *display)
{
    __HAL_RCC_GPIOB_CLK_ENABLE();
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin = TP_SCL | TP_SDA;
    gpio.Mode = GPIO_MODE_OUTPUT_OD;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    tp_line(TP_SCL | TP_SDA, true);
    HAL_GPIO_Init(GPIOB, &gpio);

    gpio.Pin = TP_RST;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    pin(TP_RST, 0);
    HAL_GPIO_Init(GPIOE, &gpio);
    HAL_Delay(20);
    pin(TP_RST, 1);
    HAL_Delay(200);

    lv_indev_t *input = lv_indev_create();
    if (!input) Error_Handler();
    lv_indev_set_type(input, LV_INDEV_TYPE_POINTER);
    lv_indev_set_display(input, display);
    lv_indev_set_read_cb(input, touch_read);
}

void LCD_LVGL_Init(void){
 __HAL_RCC_GPIOE_CLK_ENABLE();
 GPIO_InitTypeDef g={0};g.Pin=SCK|MOSI|CS|RST|DC;
 g.Mode=GPIO_MODE_OUTPUT_PP;g.Pull=GPIO_NOPULL;g.Speed=GPIO_SPEED_FREQ_HIGH;
 pin(CS,1);pin(RST,1);pin(SCK,0);pin(MOSI,0);pin(DC,1);HAL_GPIO_Init(GPIOE,&g);
 HAL_Delay(100);pin(RST,0);HAL_Delay(20);pin(RST,1);HAL_Delay(150);
 cmd(0x01);HAL_Delay(150);
 cmd(0x11);
 HAL_Delay(120);
 cmd(0x36);
 data(0x00);
 cmd(0x3A);
 data(0x55);
 cmd(0xB2);
 data(0x0C);
 data(0x0C);
 data(0x00);
 data(0x33);
 data(0x33);
 cmd(0xB7);
 data(0x56);
 cmd(0xBB);
 data(0x20);
 cmd(0xC0);
 data(0x2C);
 cmd(0xC2);
 data(0x01);
 cmd(0xC3);
 data(0x0F);
 cmd(0xC4);
 data(0x20);
 cmd(0xC6);
 data(0x0F);
 cmd(0xD0);
 data(0xA4);
 data(0xA1);
 cmd(0xD6);
 data(0xA1);
 cmd(0xE0);
 data(0xF0);
 data(0x00);
 data(0x06);
 data(0x06);
 data(0x07);
 data(0x05);
 data(0x30);
 data(0x44);
 data(0x48);
 data(0x38);
 data(0x11);
 data(0x10);
 data(0x2E);
 data(0x34);
 cmd(0xE1);
 data(0xF0);
 data(0x0A);
 data(0x0E);
 data(0x0D);
 data(0x0B);
 data(0x27);
 data(0x2F);
 data(0x44);
 data(0x47);
 data(0x35);
 data(0x12);
 data(0x12);
 data(0x2C);
 data(0x32);
 cmd(0x35);
 data(0x00);
 cmd(0x21);
 /* Explicit portrait bounds; vendor text's final address window was landscape. */
 cmd(0x36);data(0x00);cmd(0x3a);data(0x55);
 lv_init();
 lv_tick_set_cb(HAL_GetTick);
 lv_display_t *display = lv_display_create(240, 320);
 if (!display) Error_Handler();
 lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
 lv_display_set_buffers(display, lcd_buffer, NULL, sizeof(lcd_buffer), LV_DISPLAY_RENDER_MODE_PARTIAL);
 lv_display_set_flush_cb(display, lcd_flush);
 touch_init(display);

 /* Fill the panel RAM with the SquareLine screen before enabling display. */
 lv_refr_now(display);
 cmd(0x29);

}
