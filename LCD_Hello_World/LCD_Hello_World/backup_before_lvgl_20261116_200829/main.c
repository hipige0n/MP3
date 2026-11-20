/* Standalone ST7789-family SPI hello world, STM32F407 / HSE 8MHz.
 * Software SPI on PE7(SCK), PE8(SDA/MOSI), PE9(CS), PE10(RESET), PE11(RS/DC).
 * No audio/SD/TP/UART/DMA. Bare-panel power/backlight require external wiring.
 */
#include "main.h"
void SystemClock_Config(void);
#define SCK GPIO_PIN_7
#define MOSI GPIO_PIN_8
#define CS GPIO_PIN_9
#define RST GPIO_PIN_10
#define DC GPIO_PIN_11
static void pin(uint16_t p,int high){GPIOE->BSRR=high?p:((uint32_t)p<<16);}
static void pause_spi(void){for(volatile unsigned i=0;i<12;i++)__NOP();}
static void byte(uint8_t b){
 for(unsigned i=0;i<8;i++){
  pin(SCK,0);pin(MOSI,(b&0x80)!=0);pause_spi();pin(SCK,1);pause_spi();b<<=1;
 }
 pin(SCK,0);
}
static void cmd(uint8_t b){pin(CS,0);pin(DC,0);byte(b);pin(CS,1);}
static void data(uint8_t b){pin(CS,0);pin(DC,1);byte(b);pin(CS,1);}
static void window(unsigned x0,unsigned y0,unsigned x1,unsigned y1){
 cmd(0x2a);data(x0>>8);data(x0);data(x1>>8);data(x1);
 cmd(0x2b);data(y0>>8);data(y0);data(y1>>8);data(y1);cmd(0x2c);
}
static void clear(void){
 window(0,0,239,319);pin(CS,0);pin(DC,1);
 for(unsigned i=0;i<240U*320U;i++){byte(0xFF);byte(0xFF);}pin(CS,1);
}
int main(void){
 HAL_Init();SystemClock_Config();__HAL_RCC_GPIOE_CLK_ENABLE();
 GPIO_InitTypeDef g={0};g.Pin=SCK|MOSI|CS|RST|DC;
 g.Mode=GPIO_MODE_OUTPUT_PP;g.Pull=GPIO_NOPULL;g.Speed=GPIO_SPEED_FREQ_LOW;
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
 cmd(0x29);
 /* Explicit portrait bounds; vendor text's final address window was landscape. */
 cmd(0x36);data(0x00);cmd(0x3a);data(0x55);
 clear();for(;;)HAL_Delay(1000);
}
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** 板载 8MHz 晶振 -> PLL: M=8, N=336, P=2 -> SYSCLK 168MHz
      Q=7 -> 48MHz 给 SDIO 提供时钟
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 8;
  RCC_OscInitStruct.PLL.PLLN = 336;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 7;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK)
  {
    Error_Handler();
  }
}


void Error_Handler(void){__disable_irq();for(;;){}}
#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *f,uint32_t l){(void)f;(void)l;Error_Handler();}
#endif
