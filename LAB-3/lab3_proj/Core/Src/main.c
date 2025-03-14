/**
  ******************************************************************************
  * @file    BSP/Src/main.c
  * @author  MCD Application Team
  * @brief   This example code shows how to use the STM324xG BSP Drivers
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; Copyright (c) 2017 STMicroelectronics.
  * All rights reserved.</center></h2>
  *
  * This software component is licensed by ST under BSD 3-Clause license,
  * the "License"; You may not use this file except in compliance with the
  * License. You may obtain a copy of the License at:
  *                        opensource.org/licenses/BSD-3-Clause
  *
  ******************************************************************************
  ******************************************************************************
  ******************************************************************************
  * This code was modified for use in ENCM 515 in 2022 and 2023
  * B. Tan
  * Note: DO NOT REGENERATE CODE/MODIFY THE IOC
  */

/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private typedef -----------------------------------------------------------*/

/* Private define ------------------------------------------------------------*/
#define BUFFER_SIZE 100
#define NUMBER_OF_TAPS 1000
#define AUDIO_SIZE (0x2BEEC - 44)/4
#define DELAY_NUMBER 1
//#define FUNCTIONAL_TEST
/* Private macro -------------------------------------------------------------*/
/* Private variables ---------------------------------------------------------*/

__IO uint8_t UserPressButton = 0;

/* Wave Player Pause/Resume Status. Defined as external in waveplayer.c file */
__IO uint32_t PauseResumeStatus = IDLE_STATUS;

/* Counter for User button presses */
__IO uint32_t PressCount = 0;

TIM_HandleTypeDef    TimHandle;
TIM_OC_InitTypeDef   sConfig;
uint32_t uwPrescalerValue = 0;
uint32_t uwCapturedValue = 0;

volatile int32_t *raw_audio = 0x802002C; // ignore first 44 bytes of header
volatile int new_sample_flag = 0;
static int sample_count = 0;
int16_t newSampleL = 0;
int16_t newSampleR = 0;
int16_t filteredSampleL;
int16_t filteredSampleR;
int16_t delayBuffer[DELAY_NUMBER];

// Probably don't need to modify these, they are for audio output
static volatile int32_t filteredOutBufferA[BUFFER_SIZE];
static volatile int32_t filteredOutBufferB[BUFFER_SIZE];
static volatile int bufchoice = 0;
volatile int bufArdy = 0;
volatile int bufBrdy = 0;
volatile int ready = 0;

extern I2S_HandleTypeDef       hAudioOutI2s;

/* Private function prototypes -----------------------------------------------*/
static void SystemClock_Config(void);
static void GPIOA_Init(void);
static int16_t ProcessSample(int16_t newsample);
static int16_t echoEffect(int16_t newsample);
static int16_t reverbEffect(int16_t newsample);
/* Private functions ---------------------------------------------------------*/

/**
  * @brief  Main program
  * @param  None
  * @retval None
  */
int main(void)
{
 /* STM32F4xx HAL library initialization:
       - Configure the Flash prefetch, instruction and Data caches
       - Configure the Systick to generate an interrupt each 1 msec
       - Set NVIC Group Priority to 4
       - Global MSP (MCU Support Package) initialization
     */
  HAL_Init();

  /* Configure LED3, LED4, LED5 and LED6 */
  BSP_LED_Init(LED3);
  BSP_LED_Init(LED4);
  BSP_LED_Init(LED5);
  BSP_LED_Init(LED6);

  /* Configure the system clock to 100 MHz */
  SystemClock_Config();

  /* Configure GPIO so that we can probe PB2 with an Oscilloscope */
  GPIOA_Init();

  /* Configure the User Button in GPIO Mode */
  BSP_PB_Init(BUTTON_KEY, BUTTON_MODE_EXTI);

  /* Set TIMx instance */
  TimHandle.Instance = TIMx;


  /* Initialize the Audio driver */
  if(BSP_AUDIO_OUT_Init(OUTPUT_DEVICE_AUTO, 60, 8000) != 0) {
	  Error_Handler();
  }


  /* Initialize TIM3 peripheral to toggle with a frequency of ~ 8 kHz
   * System clock is 100 MHz and TIM3 is counting at the rate of the system clock
   * so 100 M / 8 k is 12500
   */
  TimHandle.Init.Period = 12499;
  TimHandle.Init.Prescaler = 0;
  TimHandle.Init.ClockDivision = 0;
  TimHandle.Init.CounterMode = TIM_COUNTERMODE_UP;
  TimHandle.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if(HAL_TIM_Base_Init(&TimHandle) != HAL_OK)
  {
	  /* Initialization Error */
	  Error_Handler();
  }

  ITM_Port32(30) = 0;
#ifndef FUNCTIONAL_TEST
  if(HAL_TIM_Base_Start_IT(&TimHandle) != HAL_OK)
  {
	  /* Starting Error */
	  Error_Handler();
  }
#endif

  /******************************************************************************
   ******************************************************************************
   ******************************************************************************
   * Init Complete
   * BEGIN LAB 2 CODE HERE
   ******************************************************************************
   ******************************************************************************
   ******************************************************************************
   */



  static int i = 0;
  static int k = 0;
  static int start = 0;

  while (1) {

#ifdef FUNCTIONAL_TEST
	  new_sample_flag = 1;
		if (sample_count < AUDIO_SIZE) {
			  newSampleL = (int16_t)raw_audio[sample_count];
			  newSampleR = (int16_t)(raw_audio[sample_count] >> 16);
			  sample_count++;
		  } else {
			  sample_count = 0;
		  }
#endif

	if (new_sample_flag == 1) {
		filteredSampleL = ProcessSample(newSampleL); // "L"
		new_sample_flag = 0;

		/* Attempt at double buffering here: note that we are duplicating the sample for L and R, but this could be changed*/
		if (bufchoice == 0) {
			filteredOutBufferA[k] = ((int32_t)filteredSampleL << 16) + (int32_t)newSampleR; // copy the filtered output to both channels
		} else {
			filteredOutBufferB[k] = ((int32_t)filteredSampleL << 16) + (int32_t)newSampleR;
		}

		k++;
	}

	// once a buffer is full, we can swap to fill up the other buffer
	if (k == BUFFER_SIZE) {
		k = 0;
		if (bufchoice == 0) {
			bufchoice = 1;
			bufArdy = 1;
		} else {
			bufchoice = 0;
			bufBrdy = 1;
		}
	}
#ifndef FUNCTIONAL_TEST

	/* We'll use double buffering here, so that once one buffer is ready to go, we use
	 * BSP_AUDIO_OUT_ChangeBuffer to tell the DMA to send the audio to the DAC*/
	if(bufBrdy == 1 && ready == 1 && start == 1) {
		bufBrdy = 0;
		BSP_AUDIO_OUT_ChangeBuffer((uint16_t*)(filteredOutBufferB), BUFFER_SIZE*2);
		ready = 0;
	}

	else if(bufArdy == 1 && ready == 1 && start == 1) {
		bufArdy = 0;
		BSP_AUDIO_OUT_ChangeBuffer((uint16_t*)(filteredOutBufferA), BUFFER_SIZE*2);
		ready = 0;
	}

	/* AUDIO_OUT_PLAY is the BSP function essentially tells the audio chip to start working
	 * so every time the audio DAC receives some new data via DMA /I2S, it will play sound*/
	if (bufArdy == 1 && bufBrdy == 1 && start == 0) {
		BSP_AUDIO_OUT_Play((uint16_t*)(filteredOutBufferA), BUFFER_SIZE*2);
		start = 1;
		bufArdy = 0;
	}
#endif

  }
}

/**
  * @brief  System Clock Configuration
  *         The system Clock is configured as follow :
  *            System Clock source            = PLL (HSI)
  *            SYSCLK(Hz)                     = 100000000
  *            HCLK(Hz)                       = 100000000
  *            AHB Prescaler                  = 1
  *            APB1 Prescaler                 = 2
  *            APB2 Prescaler                 = 1
  *            HSI Frequency(Hz)              = 16000000
  *            PLL_M                          = 16
  *            PLL_N                          = 400
  *            PLL_P                          = 4
  *            PLL_Q                          = 7
  *            VDD(V)                         = 3.3
  *            Main regulator output voltage  = Scale1 mode
  *            Flash Latency(WS)              = 3
  * @param  None
  * @retval None
  */
static void SystemClock_Config(void)
{
  RCC_ClkInitTypeDef RCC_ClkInitStruct;
  RCC_OscInitTypeDef RCC_OscInitStruct;

  /* Enable Power Control clock */
  __HAL_RCC_PWR_CLK_ENABLE();

  /* The voltage scaling allows optimizing the power consumption when the device is
     clocked below the maximum system frequency, to update the voltage scaling value
     regarding system frequency refer to product datasheet.  */
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /* Enable HSI Oscillator and activate PLL with HSI as source */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = 0x10;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = 16;
  RCC_OscInitStruct.PLL.PLLN = 400;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV4;
  RCC_OscInitStruct.PLL.PLLQ = 7;
  if(HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /* Select PLL as system clock source and configure the HCLK, PCLK1 and PCLK2
     clocks dividers */
  RCC_ClkInitStruct.ClockType = (RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2);
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
  if(HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_3) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief  EXTI line detection callbacks.
  * @param  GPIO_Pin: Specifies the pins connected EXTI line
  * @retval None
  */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
  if (KEY_BUTTON_PIN == GPIO_Pin)
  {
    while (BSP_PB_GetState(BUTTON_KEY) != RESET);
    UserPressButton = 1;
  }
}

/**
  * @brief  Toggle LEDs
  * @param  None
  * @retval None
  */
void Toggle_Leds(void)
{
  BSP_LED_Toggle(LED3);
  HAL_Delay(100);
//  BSP_LED_Toggle(LED4);
//  HAL_Delay(100);
  BSP_LED_Toggle(LED5);
  HAL_Delay(100);
  BSP_LED_Toggle(LED6);
  HAL_Delay(100);
}

// This timer callback should trigger every 1/8000 Hz, and it emulates
// the idea of receiving a new sample peridiocally
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{

//  BSP_LED_Toggle(LED4);
//  HAL_GPIO_TogglePin(SCOPE_CHECK_GPIO_Port, SCOPE_CHECK_Pin);

	// If we "miss" processing a sample, the new_sample_flag will still be
	// high on the trigger of the interrupt
	if (new_sample_flag == 1) {
		ITM_Port32(30) = 10;
	}

	// Otherwise, go to the raw audio in memory and "retrieve" a new sample every timer period
	// set the new_sample_flag high
	if (sample_count < AUDIO_SIZE) {
		newSampleL = (int16_t)raw_audio[sample_count];
		newSampleR = (int16_t)(raw_audio[sample_count] >> 16);
		sample_count++;

		if (sample_count >= AUDIO_SIZE) sample_count = 0;
		new_sample_flag = 1;
	}
}

int _write(int file, char* ptr, int len) {
	int DataIdx;

	for (DataIdx = 0; DataIdx < len; DataIdx++) {
		ITM_SendChar(*ptr++);
	}
	return len;
}

/**
  * @brief  This function is executed in case of error occurrence.
  * @param  None
  * @retval None
  */
void Error_Handler(void)
{
  /* Turn LED5 on */
  BSP_LED_On(LED5);
  while(1)
  {
  }
}

static void GPIOA_Init(void){
	GPIO_InitTypeDef GPIO_InitStruct = {0};
	__HAL_RCC_GPIOB_CLK_ENABLE();
	/*Configure GPIO pin : SCOPE_CHECK_Pin */
	  GPIO_InitStruct.Pin = SCOPE_CHECK_Pin;
	  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
	  GPIO_InitStruct.Pull = GPIO_NOPULL;
	  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
	  HAL_GPIO_Init(SCOPE_CHECK_GPIO_Port, &GPIO_InitStruct);

}

static int16_t ProcessSample(int16_t newsample) {
	return newsample;
}

static int16_t echoEffect(int16_t newsample) {
	return newsample;
}


static int16_t reverbEffect(int16_t newsample) {
	return newsample;
}

void BSP_AUDIO_OUT_TransferComplete_CallBack() {
	ready = 1;
}

#ifdef USE_FULL_ASSERT

/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t* file, uint32_t line)
{
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */

  /* Infinite loop */
  while (1)
  {
  }
}
#endif

/**
  * @}
  */

/**
  * @}
  */

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
