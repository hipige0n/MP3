/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : 例20 SD卡实验 HAL库版 (STM32F407VGT6 核心板, LQFP100)
  *
  * 功能: 检测 SD 卡 -> 挂载 FAT -> 串口验证文件读取 -> PA0 重复验证
  *
  * 引脚(核心板板载, 无需接线):
  *   LED2  -> PA1  板载蓝色 LED0, 低电平点亮
  *   WKUP  -> PA0  板载 KEY_UP 按键
  *   TF卡座 -> SDIO 4线(PC8~PC12, PD2), 板载且带 47K 上拉, 直接插卡
  *   串口   -> PA9(TX)/PA10(RX), 用 USB-TTL 模块, 115200 8N1
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "gpio.h"
#include "usart.h"
#include "sdio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include <string.h>
#include "fat_verify.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/


/* USER CODE BEGIN PV */
static HAL_SD_CardInfoTypeDef SDCardInfo;
static uint8_t sd_buf[512];
static uint32_t led_tick;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */
int fputc(int ch, FILE *f);
void show_sdcard_info(void);
void read_sector0(void);
static HAL_StatusTypeDef sd_card_init(void);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
static HAL_StatusTypeDef sd_card_init(void)
{
  /* Reset both the peripheral and handle before retrying card identification. */
  if (HAL_SD_DeInit(&hsd) != HAL_OK) return HAL_ERROR;
  hsd.Init.BusWide = SDIO_BUS_WIDE_1B;
  if (HAL_SD_Init(&hsd) != HAL_OK) return HAL_ERROR;
  return HAL_SD_ConfigWideBusOperation(&hsd, SDIO_BUS_WIDE_4B);
}


/**
 * printf 重定向到 USART1(115200), 与标准库版例程一致
 */
int fputc(int ch, FILE *f)
{
  (void)f;
  HAL_UART_Transmit(&huart1, (uint8_t *)&ch, 1, HAL_MAX_DELAY);
  return ch;
}

/**
 * 打印 SD 卡信息(类型/容量/块大小/厂商ID)
 */
void show_sdcard_info(void)
{
  if (HAL_SD_GetCardInfo(&hsd, &SDCardInfo) != HAL_OK)
  {
    printf("GetCardInfo Failed!\r\n");
    return;
  }

  switch (SDCardInfo.CardType)
  {
    case CARD_SDSC:      printf("Card Type:SDSC\r\n");        break;
    case CARD_SDHC_SDXC: printf("Card Type:SDHC/SDXC\r\n");   break;
    case CARD_SECURED:   printf("Card Type:Secured\r\n");     break;
    default:             printf("Card Type:Unknown\r\n");     break;
  }

  /* V1.28.3 的 CardInfo 没有 CardCapacity 字段:
     容量 = 块数 x 块大小, 必须用 64 位计算, 大容量卡会超 32 位 */
  printf("Card Capacity:%llu MB\r\n",
         (unsigned long long)(((uint64_t)SDCardInfo.LogBlockNbr * SDCardInfo.LogBlockSize) >> 20));
  printf("Card BlockSize:%lu\r\n", (unsigned long)SDCardInfo.BlockSize);

  printf("Card RCA:%lu\r\n", (unsigned long)SDCardInfo.RelCardAdd);
  HAL_SD_CardCIDTypeDef cid;
  if (HAL_SD_GetCardCID(&hsd, &cid) == HAL_OK)
  {
    printf("Card ManufacturerID:%d\r\n", cid.ManufacturerID);
  }
}

/**
 * 读取第 0 扇区(512 字节)并从串口十六进制打印, 与原例程行为一致
 */
void read_sector0(void)
{
  printf("SECTOR 0 DATA:\r\n");

  if (HAL_SD_ReadBlocks(&hsd, sd_buf, 0, 1, 2000U) != HAL_OK)
  {
    printf("Read Failed! HAL error:0x%08lX\r\n", (unsigned long)HAL_SD_GetError(&hsd));
    return;
  }

  for (uint32_t i = 0; i < 512; i++)
  {
    printf("%02x ", sd_buf[i]);
    if ((i & 15U) == 15U) printf("\r\n");   /* 每行 16 字节 */
  }
  printf("\r\nDATA ENDED\r\n");
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();      /* 168MHz: HSE 8MHz, PLLM=8 N=336 P=2; PLLQ=7 出 48MHz 给 SDIO */

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_USART1_UART_Init();
  MX_SDIO_SD_Init();         /* 只配置 SDIO 参数, 卡检测由下面的循环完成 */
  /* USER CODE BEGIN 2 */



  /* 等待检测到 SD 卡: 失败则打印错误并闪灯, 与原例程 while(SD_Init()) 行为一致 */
  while (sd_card_init() != HAL_OK)
  {
    printf("SD Card Error:0x%08lX Please Check!\r\n", (unsigned long)HAL_SD_GetError(&hsd));
    HAL_GPIO_TogglePin(LED2_GPIO_Port, LED2_Pin);
    HAL_Delay(500);
  }

  show_sdcard_info();
  printf("SD Card OK (4-bit SDIO)\r\n");
  FAT_Verify();
  led_tick = HAL_GetTick();
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    /* WK_UP: repeat the read-only FAT verification. */
    if (HAL_GPIO_ReadPin(WKUP_GPIO_Port, WKUP_Pin) == GPIO_PIN_SET)
    {
      HAL_Delay(20);   /* 消抖 */
      if (HAL_GPIO_ReadPin(WKUP_GPIO_Port, WKUP_Pin) == GPIO_PIN_SET)
      {
        FAT_Verify();
        while (HAL_GPIO_ReadPin(WKUP_GPIO_Port, WKUP_Pin) == GPIO_PIN_SET);  /* 等松开 */
      }
    }

    if ((uint32_t)(HAL_GetTick() - led_tick) >= 200U)
    {
      led_tick = HAL_GetTick();
      HAL_GPIO_TogglePin(LED2_GPIO_Port, LED2_Pin);
    }
    HAL_Delay(10);
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
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

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
