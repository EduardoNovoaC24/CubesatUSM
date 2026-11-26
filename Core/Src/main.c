/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Telemetria IMU/magnetometro con FreeRTOS y SX1262
  ******************************************************************************
  * Task_SensorFusion produce quaternion/eCompass y Task_RadioTest transmite
  * el ultimo payload P2P por SX1262. UART2 (USB, 115200) envia
 * cuaterniones incluso si el radio falla o queda en modo diagnostico.
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include "radio_test.h"
#include "p2p_control.h"
#include "mpu9250.h"
#include "FusionAhrs.h"
#include "FusionBias.h"
#include "lorawan_crypto.h"
#include "adcs_control.h"
#include "adsb_receiver.h"
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
I2C_HandleTypeDef hi2c1;

SPI_HandleTypeDef hspi1;

TIM_HandleTypeDef htim1;

UART_HandleTypeDef huart1;
UART_HandleTypeDef huart2;

/* Definitions for defaultTask */
osThreadId_t defaultTaskHandle;
const osThreadAttr_t defaultTask_attributes = {
  .name = "COMMAND",
  .stack_size = 512 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* USER CODE BEGIN PV */

#define UART_TX_TIMEOUT_MS   100U
#define GY91_TEST_MODE       0U     /* 1: diagnostico GY-91 y TX de prueba. */
#define GY91_TEST_PERIOD_MS  500U
#define CUBESAT_TX_PERIOD_MS 10000U
#define LORAWAN_FPORT        44U
#define LORAWAN_MAX_APP_LEN  P2P_RF_MAX
#define UART_SENSOR_STREAM_ENABLED 0U /* UART reservado para diagnostico. */
#define QUAT_UART_PERIOD_MS  20U     /* Telemetria UART a ~50 Hz, independiente de LoRa */
#define FORCE_UART_ONLY       0U     /* 1: omitir SX1262. */
#define LORAWAN_TX_PERIOD_MS 10000U
#define MAG_DIAG_PERIOD_MS   500U
#define FUSION_USE_MAGNETOMETER 1U
#define I2C_SCAN_ONLY           0U  /* 1: diagnóstico aislado, no inicia FreeRTOS/LoRa */
#define FUSION_GAIN             0.50f
#define FUSION_SAMPLE_RATE_HZ   50.0f
#define FUSION_BIAS_STATIONARY_THRESHOLD_DPS 3.0f
#define FUSION_BIAS_STATIONARY_PERIOD_S      3.0f
#define LOG_QUEUE_LENGTH        16U
#define LOG_MESSAGE_LENGTH      192U
#define ADCS_COMMAND_QUEUE_LENGTH 8U
#define IMU_SAFETY_TIMEOUT_MS   250U

TaskHandle_t radioTestTaskHandle;
TaskHandle_t sensorFusionTaskHandle;
TaskHandle_t loggerTaskHandle;
TaskHandle_t safetyTaskHandle;
static SemaphoreHandle_t telemetryMutexHandle;
static SemaphoreHandle_t gy91UartMutex;
static QueueHandle_t logQueueHandle;
static QueueHandle_t adcsCommandQueueHandle;
static QueueHandle_t p2pCommandQueueHandle, p2pResponseQueueHandle;
typedef struct {
    uint32_t received_ms;
    char line[ADSB_LINE_MAX + 1U];
} ADSB_RadioFrame;
static QueueHandle_t adsbRadioQueue;
static P2P_Control p2p_control;
static P2P_Cache p2p_cache;
static volatile uint32_t p2p_rate_hz = 2U;

typedef enum {
    ADCS_CMD_ARM,
    ADCS_CMD_DISARM,
    ADCS_CMD_MANUAL,
    ADCS_CMD_AUTO,
    ADCS_CMD_AXIS,
    ADCS_CMD_GAINS,
    ADCS_CMD_TARGET,
    ADCS_CMD_STATUS,
} AdcsCommandType_t;

typedef struct {
    AdcsCommandType_t type;
    float value[4];
    ADCS_Axis_t axis;
} AdcsCommand_t;

typedef struct {
    char text[LOG_MESSAGE_LENGTH];
} LogMessage_t;

typedef struct {
    int16_t quaternion[4];
    int16_t axes[9];
    uint16_t sequence;
    uint32_t updated_ms;
    bool valid;
    bool full_sample;
    bool mag_detected, mag_valid, mag_calibrated;
} TelemetrySnapshot_t;

static TelemetrySnapshot_t telemetry_snapshot;

/* Actuador de banco en PA8/TIM1_CH1. Arranca siempre desarmado. */
static ADCS_Control_t adcs_control;
static QueueHandle_t wheel_commands;
typedef struct {
    P2P_WheelProfile profile;
    P2P_WheelState state;
    uint16_t pulse;
    int8_t level;
    uint8_t axis;
    bool invert;
    int16_t body_crpm;
    uint16_t elapsed_cs;
    uint16_t ready_elapsed_cs;
    P2P_DetumbleState detumble;
    P2P_WheelManeuver maneuver;
    int16_t angles_ddeg[4]; /* Rotation, target, reference, light azimuth. */
} WheelTelemetry;
static WheelTelemetry wheel_telemetry = { .axis = 3U, .body_crpm = INT16_MIN };
static float wheel_body_rate[3];
static uint32_t wheel_rate_ms;
static bool wheel_rate_calibrated;
static uint8_t wheel_sensor_flags;
static uint16_t photo_samples[P2P_LIGHT_CHANNELS];
static uint8_t photo_valid_mask;
static uint32_t photo_sample_ms;
#define EPS_ADC_CHANNELS 4U
static uint16_t eps_adc[EPS_ADC_CHANNELS];
static uint8_t eps_valid_mask;
#if EPS_UART_TELEMETRY
typedef struct {
    unsigned sequence, flags, solar_mv, battery_mv;
    long solar_ua;
    uint32_t received_ms;
} EpsUartSample;
static EpsUartSample eps_uart_sample;
#define EPS_UART_RING_SIZE 512U
static volatile uint16_t eps_uart_head, eps_uart_tail;
static volatile bool eps_uart_overrun;
static volatile uint8_t eps_uart_ring[EPS_UART_RING_SIZE];
static volatile uint32_t eps_uart_bytes, eps_uart_errors;
#endif
static volatile uint32_t wheel_link_ms;
static volatile bool wheel_link_seen;
static volatile uint32_t wheel_tach_ms, wheel_tach_period_ms;
static volatile bool wheel_tach_seen;
static volatile uint32_t imu_last_valid_ms;
static volatile bool sensor_task_started;

/* TTN ABP session for end-device stm32-obc-usm.
 * ABP uplinks use DevAddr + session keys; DevEUI is only registry identity. */
#ifdef CUBESAT_LORAWAN_LOCAL_CONFIG
#include "lorawan_session.local.h"
#else
#include "lorawan_session.example.h"
#endif
static uint32_t lorawan_fcnt_up = 0;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_I2C1_Init(void);
static void MX_USART1_UART_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_SPI1_Init(void);
static void MX_TIM1_Init(void);
#if EPS_UART_TELEMETRY
static void eps_uart_init(void);
#endif
void StartDefaultTask(void *argument);

/* USER CODE BEGIN PFP */
void Task_RadioTest(void *argument);
void Task_SensorFusion(void *argument);
void Task_Logger(void *argument);
void Task_Safety(void *argument);
static void Task_GY91(void *argument);
static void Task_CubesatTelemetry(void *argument);
static void GY91_Write(const char *line);
static void process_adcs_commands(void);
static void uart2_write(const char *msg);
static bool uart2_enqueue(const char *msg);
static bool adsb_forward(const char *line);
static bool parse_adcs_command(const char *line, AdcsCommand_t *command);
#if I2C_SCAN_ONLY
static void I2C_ScanAndIdentify(void);
#endif
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    char msg[64];
    snprintf(msg, sizeof(msg), "STACK OVERFLOW: %s\r\n", pcTaskName);
    HAL_UART_Transmit(&huart2, (uint8_t*)msg, strlen(msg), HAL_MAX_DELAY);
    __disable_irq();
    while (1) {}
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
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_I2C1_Init();
  MX_USART2_UART_Init();
#if EPS_UART_TELEMETRY
  eps_uart_init();
#endif
  /* USER CODE BEGIN 2 */
  if (GY91_TEST_MODE) {
    bool radio_ready = false;
    if (!FORCE_UART_ONLY) {
      MX_SPI1_Init();
      radio_set_log_sink(GY91_Write);
      radio_ready = radio_diagnostic_run(&hspi1);
      if (!radio_ready) {
        GY91_Write("[P2P] SX1262 no disponible; continua la lectura del GY-91\r\n");
      }
    }
    if (osKernelInitialize() != osOK) {
      Error_Handler();
    }
    gy91UartMutex = xSemaphoreCreateMutex();
    if (gy91UartMutex == NULL ||
        xTaskCreate(Task_GY91, "GY91", 512U, NULL, tskIDLE_PRIORITY + 1U, NULL) != pdPASS) {
      Error_Handler();
    }
    if (radio_ready &&
        xTaskCreate(Task_CubesatTelemetry, "CUBESAT_TX", 1024U, NULL,
                    tskIDLE_PRIORITY + 1U, NULL) != pdPASS) {
      Error_Handler();
    }
    osKernelStart();
    Error_Handler();
  }

  MX_USART1_UART_Init();
  MX_SPI1_Init();
  MX_TIM1_Init();
  if (ADCS_ControlInit(&adcs_control, &htim1, TIM_CHANNEL_1) != HAL_OK) {
    Error_Handler();
  }
  uint32_t pclk1 = HAL_RCC_GetPCLK1Freq();
  char dbg[64];
  snprintf(dbg, sizeof(dbg), "PCLK1=%lu Hz\r\n", pclk1);
  HAL_UART_Transmit(&huart2, (uint8_t*)dbg, strlen(dbg), 100);
  HAL_UART_Transmit(&huart2,
                    (uint8_t*)"[ADCS] PA8/TIM1_CH1 listo; motor DESARMADO\r\n",
                    sizeof("[ADCS] PA8/TIM1_CH1 listo; motor DESARMADO\r\n") - 1U,
                    100);
  snprintf(dbg, sizeof(dbg), "[BUILD] SX1262 diag=%u %s %s\r\n",
           (unsigned)SX1262_DIAGNOSTIC_MODE, __DATE__, __TIME__);
  HAL_UART_Transmit(&huart2, (uint8_t*)dbg, strlen(dbg), 100);
#if FORCE_UART_ONLY
  /* Iniciar ADS-B, sensores y consola sin diagnostico ni tarea del SX1262. */
  HAL_UART_Transmit(&huart2,
                    (uint8_t*)"[UART] ADS-B/IMU activos; SX1262 omitido por configuracion\r\n",
                    sizeof("[UART] ADS-B/IMU activos; SX1262 omitido por configuracion\r\n") - 1U,
                    100U);
#elif SX1262_DIAGNOSTIC_MODE && RADIO_QUATERNION_MODE
  if (!radio_diagnostic_run(&hspi1)) {
#if EPS_UART_TELEMETRY
    const char message[] = "[SXDIAG] Radio no lista; EPS activo, LoRa reintentara\r\n";
    HAL_UART_Transmit(&huart2, (const uint8_t *)message, sizeof(message) - 1U, 100);
#else
    HAL_UART_Transmit(&huart2, (uint8_t*)"[SXDIAG] Radio no lista; reset para repetir\r\n",
                      sizeof("[SXDIAG] Radio no lista; reset para repetir\r\n") - 1U, 100U);
    while (1) { HAL_Delay(1000U); }
#endif
  }
#elif SX1262_DIAGNOSTIC_MODE
  /* Prueba P2P aislada: solo diagnostico UART y TX fijo, sin tareas IMU. */
  if (radio_diagnostic_run(&hspi1)) {
    HAL_UART_Transmit(&huart2,
                      (uint8_t*)"[SXDIAG] P2P: TEST_LORA cada 10s; reset para reiniciar\r\n",
                      sizeof("[SXDIAG] P2P: TEST_LORA cada 10s; reset para reiniciar\r\n") - 1U,
                      100U);
    while (1) {
      HAL_Delay(10000U);
      if (!radio_p2p_send_test()) {
        break; /* Detener ante error; no emitir un flujo de fallos indefinido. */
      }
    }
  }
  HAL_UART_Transmit(&huart2,
                    (uint8_t*)"[SXDIAG] Fin de diagnostico; reset para repetir\r\n",
                    sizeof("[SXDIAG] Fin de diagnostico; reset para repetir\r\n") - 1U,
                    100U);
  while (1) {
    HAL_Delay(1000U);
  }
#endif
#if I2C_SCAN_ONLY
  I2C_ScanAndIdentify();
  HAL_UART_Transmit(&huart2, (uint8_t*)"[I2C] SCAN_ONLY: reset para repetir\r\n",
                    sizeof("[I2C] SCAN_ONLY: reset para repetir\r\n") - 1U, 100);
  while (1) {
    HAL_Delay(1000U);
  }
#endif
  /* USER CODE END 2 */

  /* Init scheduler */
  osKernelInitialize();

  /* USER CODE BEGIN RTOS_MUTEX */
  telemetryMutexHandle = xSemaphoreCreateMutex();
  if (telemetryMutexHandle == NULL) {
    Error_Handler();
  }
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  logQueueHandle = xQueueCreate(LOG_QUEUE_LENGTH, sizeof(LogMessage_t));
  adcsCommandQueueHandle = xQueueCreate(ADCS_COMMAND_QUEUE_LENGTH, sizeof(AdcsCommand_t));
  p2pCommandQueueHandle = xQueueCreate(8U, sizeof(P2P_Command));
  p2pResponseQueueHandle = xQueueCreate(16U, P2P_RF_MAX + 1U);
  adsbRadioQueue = xQueueCreate(8U, sizeof(ADSB_RadioFrame));
#if WHEEL_BENCH_MODE
  wheel_commands = xQueueCreate(4U, sizeof(P2P_Command));
  if (wheel_commands == NULL) { Error_Handler(); }
#endif
  if ((logQueueHandle == NULL) || (adcsCommandQueueHandle == NULL) ||
      (p2pCommandQueueHandle == NULL) || (p2pResponseQueueHandle == NULL) ||
      (adsbRadioQueue == NULL)) {
    Error_Handler();
  }
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of defaultTask */
  defaultTaskHandle = osThreadNew(StartDefaultTask, NULL, &defaultTask_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  if (!ADSB_ReceiverInit(&huart1, uart2_write, adsb_forward)) {
    Error_Handler();
  }
#if (!SX1262_DIAGNOSTIC_MODE || RADIO_QUATERNION_MODE) && !FORCE_UART_ONLY
  /* La tarea de radio NO es requisito para leer IMU o emitir datos por UART. */
  if (xTaskCreate(Task_RadioTest, "RADIOTEST", 1024, NULL, osPriorityNormal, &radioTestTaskHandle) != pdPASS) {
    Error_Handler();
  }
#endif
  if (xTaskCreate(Task_SensorFusion, "SENSOR", 1024, NULL, osPriorityAboveNormal, &sensorFusionTaskHandle) != pdPASS) {
    Error_Handler();
  }
  /* El logger debe poder ejecutarse aunque RADIOTEST se quede ocupando la CPU. */
  if (xTaskCreate(Task_Logger, "LOGGER", 512, NULL, osPriorityAboveNormal, &loggerTaskHandle) != pdPASS) {
    Error_Handler();
  }
  if (xTaskCreate(Task_Safety, "SAFETY", 512, NULL, osPriorityHigh, &safetyTaskHandle) != pdPASS) {
    Error_Handler();
  }
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

  /* Start scheduler */
  osKernelStart();

  /* We should never get here as control is now taken by the scheduler */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
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
  if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_MSI;
  RCC_OscInitStruct.MSIState = RCC_MSI_ON;
  RCC_OscInitStruct.MSICalibrationValue = 0;
  RCC_OscInitStruct.MSIClockRange = RCC_MSIRANGE_6;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_MSI;
  RCC_OscInitStruct.PLL.PLLM = 1;
  RCC_OscInitStruct.PLL.PLLN = 40;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV7;
  RCC_OscInitStruct.PLL.PLLQ = RCC_PLLQ_DIV2;
  RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2;
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
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief I2C1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_I2C1_Init(void)
{

  /* USER CODE BEGIN I2C1_Init 0 */

  /* USER CODE END I2C1_Init 0 */

  /* USER CODE BEGIN I2C1_Init 1 */

  /* USER CODE END I2C1_Init 1 */
  hi2c1.Instance = I2C1;
  hi2c1.Init.Timing = 0x10D19CE4;
  hi2c1.Init.OwnAddress1 = 0;
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2 = 0;
  hi2c1.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Analogue filter
  */
  if (HAL_I2CEx_ConfigAnalogFilter(&hi2c1, I2C_ANALOGFILTER_ENABLE) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Digital filter
  */
  if (HAL_I2CEx_ConfigDigitalFilter(&hi2c1, 0) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN I2C1_Init 2 */

  /* USER CODE END I2C1_Init 2 */

}

/**
  * @brief SPI1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_SPI1_Init(void)
{

  /* USER CODE BEGIN SPI1_Init 0 */

  /* USER CODE END SPI1_Init 0 */

  /* USER CODE BEGIN SPI1_Init 1 */

  /* USER CODE END SPI1_Init 1 */
  /* SPI1 parameter configuration*/
  hspi1.Instance = SPI1;
  hspi1.Init.Mode = SPI_MODE_MASTER;
  hspi1.Init.Direction = SPI_DIRECTION_2LINES;
  hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi1.Init.NSS = SPI_NSS_SOFT;
  hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_256;
  hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi1.Init.CRCPolynomial = 7;
  hspi1.Init.CRCLength = SPI_CRC_LENGTH_DATASIZE;
  hspi1.Init.NSSPMode = SPI_NSS_PULSE_DISABLE;
  if (HAL_SPI_Init(&hspi1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN SPI1_Init 2 */

  /* USER CODE END SPI1_Init 2 */

}

/**
  * @brief TIM1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM1_Init(void)
{

  /* USER CODE BEGIN TIM1_Init 0 */

  /* USER CODE END TIM1_Init 0 */

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};
  TIM_BreakDeadTimeConfigTypeDef sBreakDeadTimeConfig = {0};

  /* USER CODE BEGIN TIM1_Init 1 */

  /* USER CODE END TIM1_Init 1 */
  htim1.Instance = TIM1;
  htim1.Init.Prescaler = 79;
  htim1.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim1.Init.Period = 19999;
  htim1.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim1.Init.RepetitionCounter = 0;
  htim1.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim1) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim1, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_Init(&htim1) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterOutputTrigger2 = TIM_TRGO2_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim1, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 0;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCNPolarity = TIM_OCNPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  sConfigOC.OCIdleState = TIM_OCIDLESTATE_RESET;
  sConfigOC.OCNIdleState = TIM_OCNIDLESTATE_RESET;
  if (HAL_TIM_PWM_ConfigChannel(&htim1, &sConfigOC, TIM_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }
  sBreakDeadTimeConfig.OffStateRunMode = TIM_OSSR_DISABLE;
  sBreakDeadTimeConfig.OffStateIDLEMode = TIM_OSSI_DISABLE;
  sBreakDeadTimeConfig.LockLevel = TIM_LOCKLEVEL_OFF;
  sBreakDeadTimeConfig.DeadTime = 0;
  sBreakDeadTimeConfig.BreakState = TIM_BREAK_DISABLE;
  sBreakDeadTimeConfig.BreakPolarity = TIM_BREAKPOLARITY_HIGH;
  sBreakDeadTimeConfig.BreakFilter = 0;
  sBreakDeadTimeConfig.Break2State = TIM_BREAK2_DISABLE;
  sBreakDeadTimeConfig.Break2Polarity = TIM_BREAK2POLARITY_HIGH;
  sBreakDeadTimeConfig.Break2Filter = 0;
  sBreakDeadTimeConfig.AutomaticOutput = TIM_AUTOMATICOUTPUT_DISABLE;
  if (HAL_TIMEx_ConfigBreakDeadTime(&htim1, &sBreakDeadTimeConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM1_Init 2 */

  /* USER CODE END TIM1_Init 2 */
  HAL_TIM_MspPostInit(&htim1);

}

/**
  * @brief USART1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART1_UART_Init(void)
{

  /* USER CODE BEGIN USART1_Init 0 */

  /* USER CODE END USART1_Init 0 */

  /* USER CODE BEGIN USART1_Init 1 */

  /* USER CODE END USART1_Init 1 */
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 115200;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  huart1.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart1.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART1_Init 2 */

  /* USER CODE END USART1_Init 2 */

}

/**
  * @brief USART2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART2_UART_Init(void)
{

  /* USER CODE BEGIN USART2_Init 0 */

  /* USER CODE END USART2_Init 0 */

  /* USER CODE BEGIN USART2_Init 1 */

  /* USER CODE END USART2_Init 1 */
  huart2.Instance = USART2;
  huart2.Init.BaudRate = 115200;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  huart2.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart2.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&huart2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART2_Init 2 */

  /* USER CODE END USART2_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(SX1262_NRESET_GPIO_Port, SX1262_NRESET_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(SX1262_NSS_GPIO_Port, SX1262_NSS_Pin, GPIO_PIN_SET);

  /*Configure GPIO pin : SX1262_NRESET_Pin */
  GPIO_InitStruct.Pin = SX1262_NRESET_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(SX1262_NRESET_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : SX1262_NSS_Pin */
  GPIO_InitStruct.Pin = SX1262_NSS_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(SX1262_NSS_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : SX1262_BUSY_Pin */
  GPIO_InitStruct.Pin = SX1262_BUSY_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(SX1262_BUSY_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : SX1262_DIO1_Pin */
  GPIO_InitStruct.Pin = SX1262_DIO1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(SX1262_DIO1_GPIO_Port, &GPIO_InitStruct);

  /* EXTI interrupt init*/
  HAL_NVIC_SetPriority(EXTI1_IRQn, 5, 0);
  HAL_NVIC_EnableIRQ(EXTI1_IRQn);

  /* USER CODE BEGIN MX_GPIO_Init_2 */
  HAL_GPIO_WritePin(RELAY_GPIO_Port, RELAY_Pin, RELAY_INACTIVE_LEVEL);
  GPIO_InitStruct.Pin = RELAY_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(RELAY_GPIO_Port, &GPIO_InitStruct);
#if WHEEL_BENCH_MODE
  GPIO_InitStruct.Pin = WHEEL_TACH_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(WHEEL_TACH_GPIO_Port, &GPIO_InitStruct);
  HAL_NVIC_SetPriority(EXTI9_5_IRQn, 5, 0);
  HAL_NVIC_EnableIRQ(EXTI9_5_IRQn);
  GPIO_InitStruct.Mode = GPIO_MODE_ANALOG_ADC_CONTROL;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Pin = PHOTO_GPIOA_Pins;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
  GPIO_InitStruct.Pin = PHOTO_GPIOC_Pins;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);
#endif
#if SOLAR_TELEMETRY
  GPIO_InitStruct.Mode = GPIO_MODE_ANALOG_ADC_CONTROL;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Pin = EPS_ADC_GPIOC_Pins;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);
  GPIO_InitStruct.Pin = EPS_ADC_GPIOA_Pins;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
#elif EPS_UART_TELEMETRY
  GPIO_InitStruct.Mode = GPIO_MODE_ANALOG_ADC_CONTROL;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Pin = GPIO_PIN_4;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);
#endif

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

static HAL_StatusTypeDef I2C_ReadId(uint8_t address_7bit, uint8_t id_register,
                                    uint8_t *value)
{
    return HAL_I2C_Mem_Read(&hi2c1, (uint16_t)address_7bit << 1U,
                            id_register, I2C_MEMADD_SIZE_8BIT,
                            value, 1U, 100U);
}

static void GY91_Write(const char *line)
{
    if (gy91UartMutex != NULL) {
        xSemaphoreTake(gy91UartMutex, portMAX_DELAY);
    }
    HAL_UART_Transmit(&huart2, (const uint8_t *)line, (uint16_t)strlen(line),
                      UART_TX_TIMEOUT_MS);
    if (gy91UartMutex != NULL) {
        xSemaphoreGive(gy91UartMutex);
    }
}

static void Task_CubesatTelemetry(void *argument)
{
    (void)argument;
    static const uint8_t payload[] = "cubesat";

    for (;;) {
        bool sent = radio_send_payload(payload, sizeof(payload) - 1U);
        GY91_Write(sent ? "[P2P] TX OK cubesat\r\n" : "[P2P] TX FAIL cubesat\r\n");
        vTaskDelay(pdMS_TO_TICKS(CUBESAT_TX_PERIOD_MS));
    }
}

static uint8_t GY91_InitBMP280(void (*log)(const char *))
{
    char line[96];
    for (uint8_t address = 0x76U; address <= 0x77U; ++address) {
        uint8_t id = 0U;
        HAL_StatusTypeDef status = I2C_ReadId(address, 0xD0U, &id);
        if (status != HAL_OK || id != 0x58U) {
            snprintf(line, sizeof(line), "[GY91] I2C=0x%02X CHIP_ID=0x%02X HAL=%d I2C_ERR=0x%lX\r\n",
                     (unsigned)address, (unsigned)id, (int)status,
                     (unsigned long)HAL_I2C_GetError(&hi2c1));
            log(line);
            continue;
        }
        /* Modo normal con sobremuestreo x1 de temperatura y presion. */
        uint8_t ctrl_meas = 0x27U;
        if (HAL_I2C_Mem_Write(&hi2c1, (uint16_t)address << 1U, 0xF4U,
                              I2C_MEMADD_SIZE_8BIT, &ctrl_meas, 1U, 100U) == HAL_OK) {
            return address;
        }
    }
    return 0U;
}

static void Task_GY91(void *argument)
{
    (void)argument;
    char line[128];
    bool imu_ready = false;
    uint8_t bmp_address = 0U;
    GY91_Write("\r\n[GY91] Prueba I2C1, muestras crudas por USART2\r\n");
    vTaskDelay(pdMS_TO_TICKS(20U));

    for (;;) {
        if (!imu_ready) {
            uint8_t imu_id = 0U;
            uint8_t mag_id = 0U;
            imu_ready = MPU9250_Init(&hi2c1) == HAL_OK;
            (void)MPU9250_ReadWhoAmI(&imu_id);
            if (imu_ready && MPU9250_HasMagnetometer()) {
                (void)MPU9250_ReadMagWhoAmI(&mag_id);
            }
            snprintf(line, sizeof(line), "[GY91] MPU=%s ID=0x%02X AK8963_ID=0x%02X\r\n",
                     imu_ready ? "OK" : "ERROR", (unsigned)imu_id, (unsigned)mag_id);
            GY91_Write(line);
        }
        if (bmp_address == 0U) {
            bmp_address = GY91_InitBMP280(GY91_Write);
            if (bmp_address != 0U) {
                snprintf(line, sizeof(line), "[GY91] BMP280 OK I2C=0x%02X\r\n",
                         (unsigned)bmp_address);
                GY91_Write(line);
            }
        }

        MPU9250_RawData_t sample;
        if (imu_ready && MPU9250_ReadRaw(&sample) == HAL_OK) {
            snprintf(line, sizeof(line), "[GY91] ACC_RAW=%d,%d,%d GYR_RAW=%d,%d,%d\r\n",
                     sample.ax, sample.ay, sample.az, sample.gx, sample.gy, sample.gz);
            GY91_Write(line);
            if (sample.magnetometer_valid) {
                snprintf(line, sizeof(line), "[GY91] MAG_RAW=%d,%d,%d\r\n",
                         sample.mx, sample.my, sample.mz);
                GY91_Write(line);
            } else {
                GY91_Write("[GY91] AK8963 sin muestra valida\r\n");
            }
        } else if (imu_ready) {
            GY91_Write("[GY91] MPU error de lectura\r\n");
            imu_ready = false;
        }

        uint8_t bmp[6];
        if (bmp_address != 0U) {
            if (HAL_I2C_Mem_Read(&hi2c1, (uint16_t)bmp_address << 1U, 0xF7U,
                                 I2C_MEMADD_SIZE_8BIT, bmp, sizeof(bmp), 100U) == HAL_OK) {
                unsigned long pressure = ((uint32_t)bmp[0] << 12U) |
                                         ((uint32_t)bmp[1] << 4U) | (bmp[2] >> 4U);
                unsigned long temperature = ((uint32_t)bmp[3] << 12U) |
                                            ((uint32_t)bmp[4] << 4U) | (bmp[5] >> 4U);
                if (pressure != 0x80000UL && temperature != 0x80000UL) {
                    snprintf(line, sizeof(line), "[GY91] BMP280 P_RAW=%lu T_RAW=%lu\r\n",
                             pressure, temperature);
                    GY91_Write(line);
                }
            } else {
                GY91_Write("[GY91] BMP280 error de lectura\r\n");
                bmp_address = 0U;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(GY91_TEST_PERIOD_MS));
    }
}

#if I2C_SCAN_ONLY
static void I2C_ScanAndIdentify(void)
{
    char line[96];
    uint8_t found = 0U;
    const char *header = "\r\n[I2C] scan 0x01..0x7E\r\n";
    HAL_UART_Transmit(&huart2, (uint8_t *)header, strlen(header), 100U);

    for (uint8_t address = 1U; address < 0x7FU; address++) {
        if (HAL_I2C_IsDeviceReady(&hi2c1, (uint16_t)address << 1U,
                                  2U, 20U) != HAL_OK) {
            continue;
        }

        found++;
        int length;
        uint8_t id = 0U;
        if ((address == 0x68U) || (address == 0x69U)) {
            HAL_StatusTypeDef status = I2C_ReadId(address, 0x75U, &id);
            length = snprintf(line, sizeof(line),
                              "[I2C] 0x%02X MPU WHO_AM_I=0x%02X ret=%d\r\n",
                              address, id, (int)status);
        } else if ((address == 0x76U) || (address == 0x77U)) {
            HAL_StatusTypeDef status = I2C_ReadId(address, 0xD0U, &id);
            length = snprintf(line, sizeof(line),
                              "[I2C] 0x%02X BMP CHIP_ID=0x%02X ret=%d\r\n",
                              address, id, (int)status);
        } else if (address == 0x0CU) {
            HAL_StatusTypeDef status = I2C_ReadId(address, 0x00U, &id);
            length = snprintf(line, sizeof(line),
                              "[I2C] 0x0C AK8963 WIA=0x%02X ret=%d\r\n",
                              id, (int)status);
        } else {
            length = snprintf(line, sizeof(line), "[I2C] dispositivo en 0x%02X\r\n",
                              address);
        }
        if (length > 0) {
            HAL_UART_Transmit(&huart2, (uint8_t *)line, (uint16_t)length, 100U);
        }
    }

    int length = snprintf(line, sizeof(line), "[I2C] fin, encontrados=%u\r\n", found);
    if (length > 0) {
        HAL_UART_Transmit(&huart2, (uint8_t *)line, (uint16_t)length, 100U);
    }
}
#endif

/*
 * Task_RadioTest — build de prueba mínima del driver SX1262 (Semtech
 * sx126x_driver vendorizado + HAL STM32 propio, ver Drivers/SX126x/).
 *
 * Secuencia: REGTEST -> XOSC -> telemetría de quaternion por LoRa cada 10 s.
 * RESET/BUSY/GetStatus, REGTEST y XOSC deben pasar antes de TX.
 */
static int16_t quat_to_q10000(float value)
{
    if (value > 1.0f) {
        value = 1.0f;
    } else if (value < -1.0f) {
        value = -1.0f;
    }

    const float scaled = value * 10000.0f;
    return (int16_t)((scaled >= 0.0f) ? (scaled + 0.5f) : (scaled - 0.5f));
}

static int16_t clamp_float_to_i16(float value)
{
    if (value > 32767.0f) {
        return 32767;
    }
    if (value < -32768.0f) {
        return -32768;
    }
    return (int16_t)((value >= 0.0f) ? (value + 0.5f) : (value - 0.5f));
}

static uint8_t build_lorawan_abp_uplink(const uint8_t *app_payload,
                                        uint8_t app_len,
                                        uint8_t fport,
                                        uint8_t *frame,
                                        uint8_t frame_max)
{
    if ((app_payload == NULL) || (frame == NULL) ||
        (app_len > LORAWAN_MAX_APP_LEN) || (frame_max < (uint8_t)(13U + app_len))) {
        return 0;
    }

    uint8_t idx = 0;
    frame[idx++] = 0x40; /* MHDR: Unconfirmed Data Up, LoRaWAN 1.0 */

    frame[idx++] = (uint8_t)(LORAWAN_DEV_ADDR & 0xFF);
    frame[idx++] = (uint8_t)((LORAWAN_DEV_ADDR >> 8) & 0xFF);
    frame[idx++] = (uint8_t)((LORAWAN_DEV_ADDR >> 16) & 0xFF);
    frame[idx++] = (uint8_t)((LORAWAN_DEV_ADDR >> 24) & 0xFF);

    frame[idx++] = 0x00; /* FCtrl: no ADR, no ACK, FOptsLen=0 */
    frame[idx++] = (uint8_t)(lorawan_fcnt_up & 0xFF);
    frame[idx++] = (uint8_t)((lorawan_fcnt_up >> 8) & 0xFF);
    frame[idx++] = fport;

    lorawan_encrypt_payload(app_payload, app_len, LORAWAN_DEV_ADDR,
                            lorawan_fcnt_up, LORAWAN_APP_SKEY, &frame[idx]);
    idx += app_len;

    uint8_t mic[4];
    lorawan_compute_mic(frame, idx, LORAWAN_DEV_ADDR, lorawan_fcnt_up,
                        LORAWAN_NWK_SKEY, mic);
    memcpy(&frame[idx], mic, sizeof(mic));
    idx += sizeof(mic);

    lorawan_fcnt_up++;
    return idx;
}

static bool uart2_enqueue(const char *msg)
{
    if ((msg == NULL) || (logQueueHandle == NULL)) {
        return false;
    }

    LogMessage_t entry;
    int length = snprintf(entry.text, sizeof(entry.text), "%s", msg);
    if (length < 0 || (size_t)length >= sizeof(entry.text)) return false;
    return xQueueSend(logQueueHandle, &entry, 0U) == pdPASS;
}

static void uart2_write(const char *msg)
{
    (void)uart2_enqueue(msg);
}

static bool adsb_forward(const char *line)
{
    ADSB_RadioFrame frame = { .received_ms = HAL_GetTick() };
    size_t length = strcspn(line, "\r\n");
    if (length >= sizeof(frame.line)) return false;
    memcpy(frame.line, line, length);
    taskENTER_CRITICAL();
    if (uxQueueSpacesAvailable(adsbRadioQueue) == 0U) {
        ADSB_RadioFrame discarded;
        (void)xQueueReceive(adsbRadioQueue, &discarded, 0U);
    }
    (void)xQueueSend(adsbRadioQueue, &frame, 0U);
    taskEXIT_CRITICAL();
    return uart2_enqueue(line);
}

static bool p2p_emit(const char *line)
{
    char response[P2P_RF_MAX + 1U];
    snprintf(response, sizeof(response), "%s", line);
    return xQueueSend(p2pResponseQueueHandle, response, 0U) == pdPASS;
}

static void p2p_local_reply(uint16_t id, const char *code)
{
    char response[P2P_RF_MAX + 1U];
    snprintf(response, sizeof(response), "ACK,%u,ERR,%s", id, code);
    P2P_CacheReply(&p2p_cache, id, response);
    if (!p2p_emit(response)) { uart2_write("[P2P] Response queue full; ACK not sent\r\n"); }
}

static void p2p_relay_command(const P2P_Command *command)
{
    const P2P_RelayAction action = (P2P_RelayAction)command->value;
    if (action != P2P_RELAY_STATUS) {
        HAL_GPIO_WritePin(RELAY_GPIO_Port, RELAY_Pin,
                          action == P2P_RELAY_ON ? RELAY_ACTIVE_LEVEL : RELAY_INACTIVE_LEVEL);
    }
    const bool on = HAL_GPIO_ReadPin(RELAY_GPIO_Port, RELAY_Pin) == RELAY_ACTIVE_LEVEL;
    char response[32];
    snprintf(response, sizeof(response), "ACK,%u,OK,RELAY,%s", command->id, on ? "ON" : "OFF");
    P2P_CacheReply(&p2p_cache, command->id, response);
    if (!p2p_emit(response)) { uart2_write("[P2P] Relay ACK queue full\r\n"); }
}

/* Independent watchdog ~32 s at nominal 32 kHz LSI. Feed only while radio
 * task progresses and the sensor task has not stalled for more than 5 s. */
static bool p2p_watchdog_started;
static void p2p_watchdog_init(void)
{
    __HAL_RCC_LSI_ENABLE();
    uint32_t start = HAL_GetTick();
    while (!__HAL_RCC_GET_FLAG(RCC_FLAG_LSIRDY)) {
        if (HAL_GetTick() - start > 100U) { return; }
    }
    IWDG->KR = 0xCCCCU;
    IWDG->KR = 0x5555U;
    IWDG->PR = 6U;
    IWDG->RLR = 4095U;
    start = HAL_GetTick();
    while (IWDG->SR != 0U && HAL_GetTick()-start < 100U) { }
    IWDG->KR = 0xAAAAU;
    p2p_watchdog_started = true;
}
static void p2p_watchdog_feed(void)
{
    if (p2p_watchdog_started && (!sensor_task_started || HAL_GetTick()-imu_last_valid_ms < 5000U)) {
        IWDG->KR = 0xAAAAU;
    }
}

static bool parse_float_values(const char *text, float *values, const size_t count)
{
    char *end;
    for (size_t i = 0U; i < count; i++) {
        while (*text == ' ') text++;
        if (*text == '\0') return false;
        values[i] = strtof(text, &end);
        if ((end == text) || !isfinite(values[i])) return false;
        text = end;
    }
    while (*text == ' ') text++;
    return *text == '\0';
}

static bool parse_adcs_command(const char *line, AdcsCommand_t *command)
{
    if ((line == NULL) || (command == NULL)) return false;

    memset(command, 0, sizeof(*command));
    if (strcmp(line, "ADCS ARM") == 0) {
        command->type = ADCS_CMD_ARM;
    } else if (strcmp(line, "ADCS DISARM") == 0) {
        command->type = ADCS_CMD_DISARM;
    } else if (strcmp(line, "ADCS AUTO") == 0) {
        command->type = ADCS_CMD_AUTO;
    } else if (strcmp(line, "ADCS STATUS") == 0) {
        command->type = ADCS_CMD_STATUS;
    } else if (strncmp(line, "ADCS MANUAL ", 12U) == 0) {
        command->type = ADCS_CMD_MANUAL;
        return parse_float_values(&line[12], command->value, 1U);
    } else if (strncmp(line, "ADCS GAINS ", 11U) == 0) {
        command->type = ADCS_CMD_GAINS;
        return parse_float_values(&line[11], command->value, 2U);
    } else if (strncmp(line, "ADCS TARGET ", 12U) == 0) {
        command->type = ADCS_CMD_TARGET;
        return parse_float_values(&line[12], command->value, 4U);
    } else if (strncmp(line, "ADCS AXIS ", 10U) == 0) {
        command->type = ADCS_CMD_AXIS;
        if (strcmp(&line[10], "X") == 0) command->axis = ADCS_AXIS_X;
        else if (strcmp(&line[10], "Y") == 0) command->axis = ADCS_AXIS_Y;
        else if (strcmp(&line[10], "Z") == 0) command->axis = ADCS_AXIS_Z;
        else return false;
    } else {
        return false;
    }
    return true;
}

static void process_adcs_commands(void)
{
    AdcsCommand_t command;
    char response[LOG_MESSAGE_LENGTH];

    while (xQueueReceive(adcsCommandQueueHandle, &command, 0U) == pdPASS) {
        switch (command.type) {
        case ADCS_CMD_ARM:
#if WHEEL_BENCH_MODE || SX1262_DIAGNOSTIC_MODE || FORCE_UART_ONLY
            /* Mantener la prueba de rescate sin movimientos del actuador. */
            uart2_write("[ADCS] ARM rechazado en modo banco/diagnostico\r\n");
#else
            if ((HAL_GetTick() - imu_last_valid_ms) <= IMU_SAFETY_TIMEOUT_MS) {
                ADCS_ControlArm(&adcs_control);
                uart2_write("[ADCS] ARMADO en neutro\r\n");
            } else {
                uart2_write("[ADCS] ARM rechazado: IMU sin datos recientes\r\n");
            }
#endif
            break;
        case ADCS_CMD_DISARM:
            ADCS_ControlDisarm(&adcs_control);
            uart2_write("[ADCS] DESARMADO\r\n");
            break;
        case ADCS_CMD_MANUAL:
            ADCS_ControlEnableClosedLoop(&adcs_control, false);
            ADCS_ControlSetManual(&adcs_control, command.value[0]);
            uart2_write(adcs_control.armed
                        ? "[ADCS] modo MANUAL\r\n"
                        : "[ADCS] MANUAL rechazado: primero ARM\r\n");
            break;
        case ADCS_CMD_AUTO:
            ADCS_ControlEnableClosedLoop(&adcs_control, true);
            uart2_write(adcs_control.closed_loop_enabled
                        ? "[ADCS] modo AUTO\r\n"
                        : "[ADCS] AUTO rechazado: requiere ARM y TARGET\r\n");
            break;
        case ADCS_CMD_AXIS:
            ADCS_ControlSetAxis(&adcs_control, command.axis);
            uart2_write("[ADCS] eje actualizado\r\n");
            break;
        case ADCS_CMD_GAINS:
            ADCS_ControlSetGains(&adcs_control, command.value[0], command.value[1]);
            uart2_write("[ADCS] ganancias actualizadas\r\n");
            break;
        case ADCS_CMD_TARGET: {
            const FusionQuaternion target = {
                .element = { .w = command.value[0], .x = command.value[1],
                             .y = command.value[2], .z = command.value[3] }
            };
            ADCS_ControlSetTarget(&adcs_control, target);
            uart2_write("[ADCS] objetivo actualizado\r\n");
            break;
        }
        case ADCS_CMD_STATUS:
            snprintf(response, sizeof(response),
                     "[ADCS] armed=%u auto=%u target=%u axis=%c pulse=%u kp_milli=%d kd_milli=%d\r\n",
                     adcs_control.armed ? 1U : 0U,
                     adcs_control.closed_loop_enabled ? 1U : 0U,
                     adcs_control.target_configured ? 1U : 0U,
                     "XYZ"[adcs_control.axis], adcs_control.pulse_us,
                     (int)(adcs_control.kp * 1000.0f),
                     (int)(adcs_control.kd * 1000.0f));
            uart2_write(response);
            break;
        }
    }
}

static void telemetry_publish(FusionQuaternion q, bool valid, const MPU9250_Data_t *data,
                              bool mag_detected, bool mag_calibrated)
{
    xSemaphoreTake(telemetryMutexHandle, portMAX_DELAY);
    telemetry_snapshot.quaternion[0] = quat_to_q10000(q.element.w);
    telemetry_snapshot.quaternion[1] = quat_to_q10000(q.element.x);
    telemetry_snapshot.quaternion[2] = quat_to_q10000(q.element.y);
    telemetry_snapshot.quaternion[3] = quat_to_q10000(q.element.z);
    telemetry_snapshot.updated_ms = HAL_GetTick();
    telemetry_snapshot.valid = valid;
    telemetry_snapshot.full_sample = data != NULL;
    telemetry_snapshot.mag_detected = mag_detected;
    telemetry_snapshot.mag_valid = data && data->magnetometer_valid;
    telemetry_snapshot.mag_calibrated = mag_calibrated;
    telemetry_snapshot.sequence++;
    if (data) {
        int16_t *axes = telemetry_snapshot.axes;
        axes[0] = clamp_float_to_i16(data->ax * 1000.0f);
        axes[1] = clamp_float_to_i16(data->ay * 1000.0f);
        axes[2] = clamp_float_to_i16(data->az * 1000.0f);
        axes[3] = clamp_float_to_i16(data->gx * 100.0f);
        axes[4] = clamp_float_to_i16(data->gy * 100.0f);
        axes[5] = clamp_float_to_i16(data->gz * 100.0f);
        axes[6] = clamp_float_to_i16(data->mx * 10.0f);
        axes[7] = clamp_float_to_i16(data->my * 10.0f);
        axes[8] = clamp_float_to_i16(data->mz * 10.0f);
    }
    xSemaphoreGive(telemetryMutexHandle);
}

#define FULL_TELEMETRY_PERIOD_MS 10000U
#define WHEEL_READY_WINDOW_MS 20000U
_Static_assert(SOLAR_R_TOP_OHM > 0U && SOLAR_R_BOTTOM_OHM > 0U && SOLAR_ADC_REF_MV > 0U,
               "The solar divider and ADC reference must be nonzero");

static size_t telemetry_solar(uint8_t *payload, size_t capacity, const TelemetrySnapshot_t *snapshot)
{
    size_t prefix = P2P_QUAT_V1_LEN;
    if (snapshot->valid && HAL_GetTick() - snapshot->updated_ms <= IMU_SAFETY_TIMEOUT_MS) {
        if (capacity < prefix) { return 0U; }
        payload[0] = P2P_QUAT_V1;
        for (unsigned i = 0; i < 4U; ++i) {
            const uint16_t q = (uint16_t)snapshot->quaternion[i];
            payload[1U + 2U * i] = (uint8_t)q;
            payload[2U + 2U * i] = (uint8_t)(q >> 8U);
        }
    } else {
        static const char no_attitude[] = "QERR,NO_DATA";
        prefix = sizeof(no_attitude) - 1U;
        if (capacity < prefix) { return 0U; }
        memcpy(payload, no_attitude, prefix);
    }
#if EPS_UART_TELEMETRY
    taskENTER_CRITICAL();
    EpsUartSample sample = eps_uart_sample;
    const uint16_t test_raw = eps_adc[0];
    const bool test_valid = (eps_valid_mask & 1U) && HAL_GetTick() - photo_sample_ms <= 250U;
    taskEXIT_CRITICAL();
    if (HAL_GetTick() - sample.received_ms > 3500U) { sample = (EpsUartSample){0}; }
    const uint64_t test_denominator = (uint64_t)P2P_LIGHT_MAX * SOLAR_R_BOTTOM_OHM;
    const uint64_t test_numerator = (uint64_t)test_raw * SOLAR_ADC_REF_MV *
                                    ((uint64_t)SOLAR_R_TOP_OHM + SOLAR_R_BOTTOM_OHM);
    const unsigned long test_mv = (unsigned long)((test_numerator + test_denominator / 2U) / test_denominator);
    const int length = snprintf((char *)payload + prefix, capacity - prefix,
                                "|EPSI,%u,%u,%u,%ld,%u|PTEST,%u,%lu", sample.sequence, sample.flags,
                                sample.solar_mv, sample.solar_ua, sample.battery_mv,
                                test_valid ? (test_raw >= 4090U ? 2U : 1U) : 0U, test_mv);
#else
    uint16_t raw[EPS_ADC_CHANNELS];
    taskENTER_CRITICAL();
    memcpy(raw, eps_adc, sizeof(raw));
    const unsigned valid = HAL_GetTick() - photo_sample_ms <= 250U ? eps_valid_mask : 0U;
    taskEXIT_CRITICAL();
    unsigned saturated = 0U;
    unsigned long mv[EPS_ADC_CHANNELS] = {0};
    const uint64_t denominator = (uint64_t)P2P_LIGHT_MAX * SOLAR_R_BOTTOM_OHM;
    for (unsigned i = 0; i < EPS_ADC_CHANNELS; ++i) {
        if (!(valid & (1U << i))) { continue; }
        const uint64_t numerator = (uint64_t)raw[i] * SOLAR_ADC_REF_MV *
                                   ((uint64_t)SOLAR_R_TOP_OHM + SOLAR_R_BOTTOM_OHM);
        mv[i] = (unsigned long)((numerator + denominator / 2U) / denominator);
        if (raw[i] >= 4090U) { saturated |= 1U << i; }
    }
    const int length = snprintf((char *)payload + prefix, capacity - prefix,
                                "|EPS,%u,%u,%lu,%lu,%lu,%lu", valid, saturated,
                                mv[0], mv[1], mv[2], mv[3]);
#endif
    return length > 0 && (size_t)length < capacity - prefix ? prefix + (size_t)length : 0U;
}

static size_t telemetry_copy(uint8_t *payload, size_t capacity, bool full)
{
    xSemaphoreTake(telemetryMutexHandle, portMAX_DELAY);
    TelemetrySnapshot_t snapshot = telemetry_snapshot;
    xSemaphoreGive(telemetryMutexHandle);

    if ((SOLAR_TELEMETRY || EPS_UART_TELEMETRY) && full) { return telemetry_solar(payload, capacity, &snapshot); }
    if (WHEEL_BENCH_MODE) {
        if (capacity < P2P_WHEEL_MANEUVER_LEN) { return 0U; }
        WheelTelemetry wheel;
        uint32_t tach_ms, period_ms, light_ms;
        uint8_t sensor_flags, light_mask;
        uint16_t light[P2P_LIGHT_CHANNELS];
        taskENTER_CRITICAL();
        wheel = wheel_telemetry;
        tach_ms = wheel_tach_ms;
        period_ms = wheel_tach_period_ms;
        sensor_flags = wheel_sensor_flags;
        memcpy(light, photo_samples, sizeof(light));
        light_mask = photo_valid_mask;
        light_ms = photo_sample_ms;
        taskEXIT_CRITICAL();
        const bool valid = snapshot.valid && HAL_GetTick() - snapshot.updated_ms <= IMU_SAFETY_TIMEOUT_MS;
        const uint16_t rpm = period_ms && HAL_GetTick() - tach_ms < 2000U
                             ? (uint16_t)(60000U / period_ms) : P2P_WHEEL_RPM_UNKNOWN;
        payload[0] = P2P_WHEEL_V1;
        for (unsigned i = 0; i < 4U; ++i) {
            uint16_t q = valid ? (uint16_t)snapshot.quaternion[i] : 0U;
            payload[1U + 2U * i] = (uint8_t)q;
            payload[2U + 2U * i] = (uint8_t)(q >> 8U);
        }
        payload[9] = (uint8_t)wheel.pulse;
        payload[10] = (uint8_t)(wheel.pulse >> 8U);
        payload[11] = (uint8_t)wheel.level;
        payload[12] = (uint8_t)((wheel.profile << 5U) | wheel.state |
                                (valid ? P2P_WHEEL_QUAT_VALID : 0U));
        payload[13] = (uint8_t)rpm;
        payload[14] = (uint8_t)(rpm >> 8U);
        const uint16_t body = (uint16_t)wheel.body_crpm;
        payload[15] = (uint8_t)(wheel.axis | (wheel.invert ? 4U : 0U));
        payload[16] = (uint8_t)wheel.detumble;
        payload[17] = (uint8_t)body;
        payload[18] = (uint8_t)(body >> 8U);
        const uint16_t elapsed_cs = wheel.state == WHEEL_READY ?
                                    wheel.ready_elapsed_cs : wheel.elapsed_cs;
        payload[19] = (uint8_t)elapsed_cs;
        payload[20] = (uint8_t)(elapsed_cs >> 8U);
        payload[21] = (uint8_t)(sensor_flags | (HAL_GetTick() - light_ms <= 250U ? light_mask : 0U));
        P2P_PackLight(&payload[22], light);
        payload[28] = wheel.maneuver;
        for (unsigned i = 0; i < 4; ++i) {
            const uint16_t value = (uint16_t)wheel.angles_ddeg[i];
            payload[29 + 2 * i] = (uint8_t)value;
            payload[30 + 2 * i] = (uint8_t)(value >> 8);
        }
        return P2P_WHEEL_MANEUVER_LEN;
    }
    if (!snapshot.valid || HAL_GetTick() - snapshot.updated_ms > IMU_SAFETY_TIMEOUT_MS) {
        return (size_t)snprintf((char *)payload, capacity, "QERR,%s",
                                snapshot.valid ? "STALE" : "NO_DATA");
    }

    if (full && snapshot.full_sample) {
        const int16_t *a = snapshot.axes;
        const int16_t *q = snapshot.quaternion;
        int length = snprintf((char *)payload, capacity,
            "TEL,%u,%lu,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%u|MAGSTAT,%u,%u,%u",
            (unsigned)snapshot.sequence, (unsigned long)snapshot.updated_ms,
            a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8],
            q[0], q[1], q[2], q[3], (unsigned)(snapshot.mag_valid && snapshot.mag_calibrated),
            (unsigned)snapshot.mag_detected, (unsigned)snapshot.mag_valid,
            (unsigned)snapshot.mag_calibrated);
        if (length > 0 && (size_t)length < capacity) { return (size_t)length; }
    }
    payload[0] = P2P_QUAT_V1;
    for (size_t i = 0; i < 4U; ++i) {
        uint16_t value = (uint16_t)snapshot.quaternion[i];
        payload[1U + 2U * i] = (uint8_t)value;
        payload[2U + 2U * i] = (uint8_t)(value >> 8U);
    }
    return P2P_QUAT_V1_LEN;
}

void Task_Logger(void *argument)
{
    (void)argument;
    LogMessage_t entry;
    for (;;) {
        if (xQueueReceive(logQueueHandle, &entry, portMAX_DELAY) == pdPASS) {
            HAL_UART_Transmit(&huart2, (uint8_t *)entry.text,
                              (uint16_t)strlen(entry.text), UART_TX_TIMEOUT_MS);
        }
    }
}

static uint16_t wheel_neutral(P2P_WheelProfile profile)
{
    return profile == WHEEL_PROFILE_AIR ? 1000U : 1500U;
}

static void wheel_stop(WheelTelemetry *wheel, uint32_t *phase_at, uint32_t now)
{
    wheel->level = 0;
    if (wheel->state == WHEEL_IDLE || wheel->state == WHEEL_STOPPING) { return; }
    wheel->state = WHEEL_STOPPING;
    wheel->pulse = wheel_neutral(wheel->profile);
    *phase_at = now;
}

static void wheel_reply(uint16_t id, bool ok, const char *detail)
{
    char line[64];
    snprintf(line, sizeof(line), "ACK,%u,%s,%s", id, ok ? "OK" : "ERR", detail);
    (void)p2p_emit(line);
}

enum { BRAKE_IDLE, BRAKE_SPIN, BRAKE_CONTROL, BRAKE_RECOVER, BRAKE_FINISHED, BRAKE_ABORTED };
enum { BRAKE_ERROR_NONE, BRAKE_ERROR_STOP, BRAKE_ERROR_IMU, BRAKE_ERROR_LINK,
       BRAKE_ERROR_NO_MOTION, BRAKE_ERROR_CONTROL, BRAKE_ERROR_RECOVERY };

typedef struct {
    bool armed;
    unsigned stage, error, count, pulse;
    uint32_t phase_at, steady_at;
    uint16_t times[P2P_BRAKE_TRIALS];
} WheelBrakeBatch;

static bool wheel_batch_active(const WheelBrakeBatch *batch)
{
    return batch->stage >= BRAKE_SPIN && batch->stage <= BRAKE_RECOVER;
}

static void wheel_batch_stop(WheelBrakeBatch *batch, unsigned error)
{
    batch->armed = false;
    if (wheel_batch_active(batch)) {
        batch->stage = BRAKE_ABORTED;
        batch->error = error;
    }
}

static void wheel_batch_report(uint16_t id, const WheelBrakeBatch *batch)
{
    char response[P2P_RF_MAX + 1U] = {0};
    size_t used = (size_t)snprintf(response, sizeof(response), "ACK,%u,OK,BRAKE,%u,%u,%u,%u",
                                  id, batch->pulse, batch->stage, batch->error, (unsigned)batch->armed);
    for (unsigned i = 0; i < batch->count; ++i) {
        used += (size_t)snprintf(response + used, sizeof(response) - used, ",%u", (unsigned)batch->times[i]);
    }
    (void)xQueueSend(p2pResponseQueueHandle, response, 0U);
}

static BaseType_t wheel_enqueue(const P2P_Command *command)
{
    if ((int)command->value == WHEEL_CMD_STOP) {
        return xTaskNotify(safetyTaskHandle, command->id, eSetValueWithOverwrite);
    }
    return xQueueSend(wheel_commands, command, 0U);
}

static float wheel_wrap_degrees(float degrees)
{
    degrees = fmodf(degrees, 360.0f);
    return degrees < 0.0f ? degrees + 360.0f : degrees;
}

/* Faces 1, 2, 3, 4 face +X, +Y, -X, -Y, respectively. */
static bool wheel_light_angle(uint32_t now, bool inverted, float *degrees)
{
    uint16_t samples[P2P_LIGHT_CHANNELS];
    taskENTER_CRITICAL();
    const bool valid = photo_valid_mask == 15U && now - photo_sample_ms <= 250U;
    memcpy(samples, photo_samples, sizeof(samples));
    taskEXIT_CRITICAL();
    if (!valid) { return false; }
    const float x = (float)samples[0] - samples[2];
    const float y = (float)samples[1] - samples[3];
    if (x*x + y*y < 80.0f * 80.0f) { return false; }
    const float polarity = inverted ? -1.0f : 1.0f;
    *degrees = atan2f(polarity * y, polarity * x) * 57.2957795f;
    return true;
}

static void wheel_service(WheelTelemetry *wheel, uint32_t *phase_at, bool *enabled,
                          uint32_t *enabled_at, uint32_t now, bool imu_stale)
{
    static uint32_t last_step_at;
    static uint32_t detumble_at, below_at, completed_at, ramp_at, integrated_at;
    static float initial_speed, rotation, target_angle, reference, previous_rate;
    static bool light_inverted;
    static uint32_t cruise_period;
    static int cruise_level;
    static bool below;
    static WheelBrakeBatch batch;
    float rate = 0.0f;
    bool rate_ok;
    taskENTER_CRITICAL();
    if (wheel->axis < 3U) { rate = wheel_body_rate[wheel->axis]; }
    rate_ok = wheel->axis < 3U && wheel_rate_calibrated && now - wheel_rate_ms <= 200U;
    taskEXIT_CRITICAL();
    rate_ok = rate_ok && isfinite(rate);
    const bool link_ok = wheel_link_seen && now - wheel_link_ms < 25000U;
    wheel->body_crpm = rate_ok ? clamp_float_to_i16(rate * (100.0f / 6.0f)) : INT16_MIN;
    if (batch.armed && (!link_ok || !rate_ok || imu_stale || fabsf(rate) >= 475.0f)) {
        wheel_batch_stop(&batch, link_ok ? BRAKE_ERROR_IMU : BRAKE_ERROR_LINK);
        if (wheel->detumble == DETUMBLE_ACTIVE) {
            wheel->detumble = link_ok ? DETUMBLE_IMU : DETUMBLE_LINK_LOST;
        }
        wheel_stop(wheel, phase_at, now);
    }
    if (batch.stage == BRAKE_SPIN) {
        if (wheel->pulse == batch.pulse) {
            if (!batch.steady_at) { batch.steady_at = now; }
            if (now - batch.steady_at >= 2000U) {
                if (fabsf(rate) <= 0.5f) {
                    wheel_batch_stop(&batch, BRAKE_ERROR_NO_MOTION);
                    wheel_stop(wheel, phase_at, now);
                } else {
                    batch.stage = BRAKE_CONTROL;
                    wheel->maneuver = WHEEL_MANEUVER_BRAKE;
                    wheel->detumble = DETUMBLE_ACTIVE;
                    initial_speed = fabsf(rate);
                    detumble_at = integrated_at = now;
                    ramp_at = now - 200U;
                    below = false;
                }
            }
        } else {
            batch.steady_at = 0U;
            if (now - batch.phase_at >= 6000U) {
                wheel_batch_stop(&batch, BRAKE_ERROR_CONTROL);
                wheel_stop(wheel, phase_at, now);
            }
        }
    }
    if (wheel->detumble == DETUMBLE_ACTIVE) {
        const uint32_t elapsed = now - detumble_at;
        const bool braking = wheel->maneuver == WHEEL_MANEUVER_BRAKE;
        const bool tracking = wheel->maneuver == WHEEL_MANEUVER_LIGHT;
        float light_body = 0.0f;
        const bool light_ok = !tracking || wheel_light_angle(now, light_inverted, &light_body);
        wheel->elapsed_cs = (uint16_t)((elapsed > 60000U ? 60000U : elapsed) / 10U);
        if (!rate_ok || imu_stale || fabsf(rate) >= 475.0f || now - integrated_at > 200U) {
            wheel->detumble = DETUMBLE_IMU;
        } else if (!link_ok) {
            wheel->detumble = DETUMBLE_LINK_LOST;
        } else if (!light_ok) {
            wheel->angles_ddeg[3] = INT16_MIN;
            wheel->detumble = DETUMBLE_NO_LIGHT;
        } else if (braking && fabsf(rate) > initial_speed + fmaxf(30.0f, initial_speed * 0.20f)) {
            wheel->detumble = DETUMBLE_DIVERGED;
        } else if (!braking && (fabsf(rate) > 45.0f ||
                   (!tracking && fabsf(target_angle - rotation) > fabsf(target_angle) + 30.0f))) {
            wheel->detumble = DETUMBLE_DIVERGED;
        } else if (elapsed >= 60000U) {
            wheel->detumble = abs(wheel->level) >= P2P_WHEEL_MAX_LEVEL ?
                              DETUMBLE_LIMIT : DETUMBLE_TIMEOUT;
        }
        if (wheel->detumble != DETUMBLE_ACTIVE) {
            if (batch.stage == BRAKE_CONTROL) { wheel_batch_stop(&batch, BRAKE_ERROR_CONTROL); }
            wheel_stop(wheel, phase_at, now);
        } else {
            if (!braking) {
                rotation += (rate + previous_rate) * 0.0005f * (float)(now - integrated_at);
                wheel->angles_ddeg[0] = clamp_float_to_i16(rotation * 10.0f);
            }
            previous_rate = rate;
            integrated_at = now;
            if (tracking) {
                wheel->angles_ddeg[3] = (int16_t)(wheel_wrap_degrees(reference + rotation + light_body) * 10.0f);
            }
            const float error = tracking ? light_body : target_angle - rotation;
            const float tolerance = tracking ? 3.0f : 1.0f;
            const bool settled = fabsf(rate) <= 0.5f && (braking || fabsf(error) <= tolerance);
            if (settled) {
                if (!below) { below_at = now; }
                below = true;
                if (now - below_at >= 2000U && !tracking) {
                    wheel->elapsed_cs = (uint16_t)((below_at - detumble_at) / 10U);
                    wheel->detumble = DETUMBLE_DONE;
                    completed_at = now;
                    if (batch.stage == BRAKE_CONTROL) {
                        batch.times[batch.count++] = wheel->elapsed_cs;
                        if (batch.count == P2P_BRAKE_TRIALS) {
                            batch.stage = BRAKE_FINISHED;
                            batch.armed = false;
                            wheel_stop(wheel, phase_at, now);
                        } else {
                            batch.stage = BRAKE_RECOVER;
                            batch.phase_at = now;
                            batch.steady_at = 0U;
                            wheel->level = 0;
                        }
                    }
                }
            } else {
                below = false;
            }
            if (!settled && now - ramp_at >= 200U) {
                ramp_at = now;
                /* Constant body rate in cruise, reducing it as the target approaches. */
                const float desired = braking || fabsf(error) <= tolerance ? 0.0f :
                                      fminf(12.0f, fabsf(error) * 0.8f) * copysignf(1.0f, error);
                const float rate_error = rate - desired;
                if (fabsf(rate_error) > 0.5f) {
                    cruise_period = 0U;
                    const int step = ((rate_error < 0.0f) != wheel->invert) ? -1 : 1;
                    const int next = wheel->level + step;
                    if (abs(next) <= P2P_WHEEL_MAX_LEVEL) { wheel->level = (int8_t)next; }
                } else if (!braking && fabsf(desired) >= 12.0f && abs(wheel->level) >= 2) {
                    uint32_t period, tach_at;
                    taskENTER_CRITICAL();
                    period = wheel_tach_period_ms;
                    tach_at = wheel_tach_ms;
                    taskEXIT_CRITICAL();
                    if (period && now - tach_at < 1000U) {
                        if (!cruise_period) {
                            cruise_period = period;
                            cruise_level = wheel->level;
                        }
                        /* Trim ESC output around the measured cruise speed. */
                        const int sign = cruise_level > 0 ? 1 : -1;
                        const int correction = period > cruise_period * 1.05f ? sign :
                                               period < cruise_period * 0.95f ? -sign : 0;
                        const int level = cruise_level + correction;
                        if (abs(level) <= P2P_WHEEL_MAX_LEVEL) { wheel->level = (int8_t)level; }
                    } else {
                        cruise_period = 0U;
                    }
                } else {
                    cruise_period = 0U;
                }
            }
        }
    }
    if (batch.stage == BRAKE_RECOVER) {
        uint32_t tach_at, tach_period;
        taskENTER_CRITICAL();
        tach_at = wheel_tach_ms;
        tach_period = wheel_tach_period_ms;
        taskEXIT_CRITICAL();
        const bool spinning = tach_period && tach_period < 500U && now - tach_at < 2000U;
        if (now - batch.phase_at >= 20000U) {
            wheel_batch_stop(&batch, BRAKE_ERROR_RECOVERY);
            wheel_stop(wheel, phase_at, now);
        } else if (wheel->pulse != wheel_neutral(wheel->profile) || fabsf(rate) > 0.5f || spinning) {
            batch.steady_at = 0U;
        } else {
            if (!batch.steady_at) { batch.steady_at = now; }
            if (now - batch.steady_at >= 2000U) {
                batch.stage = BRAKE_SPIN;
                batch.phase_at = now;
                batch.steady_at = 0U;
                wheel->detumble = DETUMBLE_NONE;
                wheel->level = (int8_t)((batch.pulse - 1500U) / 5U);
            }
        }
    }
    const bool result_hold = wheel->detumble == DETUMBLE_DONE && wheel->state == WHEEL_RUNNING;
    if (result_hold && !wheel_batch_active(&batch) &&
        (now - completed_at >= 5000U || !rate_ok)) { wheel_stop(wheel, phase_at, now); }
    if (*enabled && now - *enabled_at >= 30000U) { *enabled = false; }
    if (imu_stale ||
        (wheel->state == WHEEL_READY && !batch.armed && now - *phase_at >= WHEEL_READY_WINDOW_MS) ||
        (wheel->state == WHEEL_RUNNING && wheel->detumble != DETUMBLE_ACTIVE &&
         !result_hold && !wheel_batch_active(&batch) && now - *phase_at >= 8000U)) {
        *enabled = false;
        wheel_stop(wheel, phase_at, now);
    }
    if (wheel->state == WHEEL_ARMING && now - *phase_at >= 3000U) {
        wheel->state = WHEEL_READY;
        *phase_at = now;
    }
    /* The ADCS elapsed field carries time in READY while no test is running. */
    if (wheel->state == WHEEL_READY) {
        wheel->ready_elapsed_cs = batch.armed ? 0U : (uint16_t)((now - *phase_at) / 10U);
    }
    if (wheel->state == WHEEL_STOPPING) { batch.armed = false; }
    if (wheel->state == WHEEL_STOPPING && now - *phase_at >= 5000U) {
        wheel->state = WHEEL_IDLE;
        wheel->pulse = 0U;
    }

    P2P_Command command;
    uint32_t stop_id;
    BaseType_t received = xTaskNotifyWait(0U, 0U, &stop_id, 0U);
    if (received == pdTRUE) {
        xQueueReset(wheel_commands);
        command = (P2P_Command){ .id = (uint16_t)stop_id, .name = P2P_WHEEL,
                                 .value = WHEEL_CMD_STOP };
    } else {
        received = xQueueReceive(wheel_commands, &command, 0U);
    }
    if (received == pdPASS && (int)command.value == WHEEL_CMD_RESULTS) {
        if (command.id) { wheel_batch_report(command.id, &batch); }
        received = pdFALSE;
    }
    if (received == pdPASS) {
        const int action = (int)command.value;
        const char *error = NULL;
        const char *detail = "WHEEL_STATUS_IN_TELEMETRY";
        if (action == WHEEL_CMD_STOP) {
            *enabled = false;
            wheel_batch_stop(&batch, BRAKE_ERROR_STOP);
            if (wheel->detumble == DETUMBLE_ACTIVE) { wheel->detumble = DETUMBLE_STOPPED; }
            wheel_stop(wheel, phase_at, now);
            detail = "WHEEL_STOP_REQUEST_APPLIED";
        } else if (command.id == 0 && action == WHEEL_CMD_PROFILE_OFF) {
            *enabled = false;
            wheel_batch_stop(&batch, BRAKE_ERROR_STOP);
            wheel_stop(wheel, phase_at, now);
            if (wheel->detumble == DETUMBLE_ACTIVE) { wheel->detumble = DETUMBLE_STOPPED; }
            if (wheel->state == WHEEL_IDLE) { wheel->profile = WHEEL_PROFILE_NONE; }
            else { error = "WAIT_FOR_STOP"; }
        } else if (action == WHEEL_CMD_BENCH_ARM) {
            if (batch.armed && wheel->state == WHEEL_READY) {
                detail = "BENCH_READY";
            } else if (wheel->state != WHEEL_IDLE || !link_ok || !rate_ok ||
                       imu_stale || fabsf(rate) > 0.5f) {
                error = "CHECK_IDLE_LINK_AXIS_GYRO";
            } else {
                *enabled = false;
                batch.armed = true;
                wheel->profile = WHEEL_PROFILE_BIDIR;
                wheel->detumble = DETUMBLE_NONE;
                wheel->maneuver = WHEEL_MANEUVER_NONE;
                wheel->state = WHEEL_ARMING;
                wheel->pulse = wheel_neutral(wheel->profile);
                *phase_at = now;
                detail = "BENCH_ARMING";
            }
        } else if (action > WHEEL_RUNS_BASE) {
            if (!batch.armed || wheel->state != WHEEL_READY || !link_ok || !rate_ok || fabsf(rate) > 0.5f) {
                error = "BENCH_ARM_AND_REST_REQUIRED";
            } else {
                batch = (WheelBrakeBatch){ .armed = true, .stage = BRAKE_SPIN,
                    .pulse = (unsigned)(action - WHEEL_RUNS_BASE), .phase_at = now };
                wheel->detumble = DETUMBLE_NONE;
                wheel->maneuver = WHEEL_MANEUVER_BRAKE;
                wheel->state = WHEEL_RUNNING;
                wheel->level = (int8_t)((batch.pulse - 1500U) / 5U);
                last_step_at = now;
                detail = "BRAKE_BATCH_STARTED";
            }
        } else if (wheel_batch_active(&batch)) {
            if (action != WHEEL_CMD_STATUS) { error = "BRAKE_BATCH_ACTIVE"; }
        } else if (action == WHEEL_CMD_ENABLE ||
                   action == WHEEL_CMD_PROFILE_AIR || action == WHEEL_CMD_PROFILE_BIDIR) {
            if (wheel->state != WHEEL_IDLE) {
                error = "WAIT_FOR_STOP";
            } else if (action == WHEEL_CMD_ENABLE) {
                if (wheel->profile == WHEEL_PROFILE_NONE || imu_stale || !sensor_task_started) {
                    error = "PROFILE_OR_IMU_NOT_READY";
                } else {
                    *enabled = true;
                    *enabled_at = now;
                    detail = "WHEEL_ENABLED_30S";
                }
            } else {
                *enabled = false;
                wheel->profile = action == WHEEL_CMD_PROFILE_AIR ? WHEEL_PROFILE_AIR : WHEEL_PROFILE_BIDIR;
                detail = action == WHEEL_CMD_PROFILE_AIR ? "WHEEL_PROFILE_AIR" : "WHEEL_PROFILE_BIDIR";
            }
        } else if (action >= WHEEL_CMD_AXIS_X_POS &&
                   action <= WHEEL_CMD_AXIS_Z_NEG) {
            if (wheel->state != WHEEL_IDLE) { error = "WAIT_FOR_STOP"; }
            else {
                const unsigned selection = (unsigned)(action - WHEEL_CMD_AXIS_X_POS);
                wheel->axis = (uint8_t)(selection / 2U);
                wheel->invert = (selection & 1U) != 0U;
                *enabled = false;
            }
        } else if (action == WHEEL_CMD_ARM) {
            if (!*enabled || wheel->state != WHEEL_IDLE || imu_stale || !sensor_task_started) {
                error = "WHEEL_ENABLE_REQUIRED";
            } else {
                *enabled = false;
                wheel->detumble = DETUMBLE_NONE;
                wheel->maneuver = WHEEL_MANEUVER_NONE;
                for (unsigned i = 0; i < 4; ++i) { wheel->angles_ddeg[i] = INT16_MIN; }
                wheel->elapsed_cs = 0U;
                wheel->state = WHEEL_ARMING;
                wheel->pulse = wheel_neutral(wheel->profile);
                *phase_at = now;
                detail = "WHEEL_ARMING";
            }
        } else if (action == WHEEL_CMD_DETUMBLE || action >= WHEEL_TURN_BASE) {
            const bool braking = action == WHEEL_CMD_DETUMBLE;
            const bool tracking = action >= WHEEL_LIGHT_BASE;
            const int light_config = action - WHEEL_LIGHT_BASE;
            float light_body = 0.0f;
            if (wheel->state != WHEEL_READY || wheel->profile != WHEEL_PROFILE_BIDIR) {
                error = "BIDIR_ARM_REQUIRED";
            } else if (!link_ok) {
                error = "NO_RECENT_LINK_FEEDBACK";
            } else if (!rate_ok || fabsf(rate) >= 475.0f || (!braking && fabsf(rate) > 2.0f)) {
                error = "CHECK_AXIS_GYRO_CAL_AND_RATE";
            } else if (tracking && wheel->axis != ADCS_AXIS_Z) {
                error = "LIGHT_REQUIRES_Z_AXIS";
            } else if (tracking && !wheel_light_angle(now, light_config >= 360, &light_body)) {
                error = "NO_DIRECTIONAL_LIGHT";
            } else {
                wheel->maneuver = braking ? WHEEL_MANEUVER_BRAKE :
                                  tracking ? WHEEL_MANEUVER_LIGHT : WHEEL_MANEUVER_TURN;
                initial_speed = fabsf(rate);
                rotation = 0.0f;
                previous_rate = rate;
                target_angle = (float)(action - WHEEL_TURN_BASE - 180);
                reference = tracking ? (float)(light_config % 360) : 0.0f;
                light_inverted = tracking && light_config >= 360;
                wheel->angles_ddeg[0] = braking ? INT16_MIN : 0;
                wheel->angles_ddeg[1] = braking || tracking ? INT16_MIN : (int16_t)(target_angle * 10.0f);
                wheel->angles_ddeg[2] = tracking ? (int16_t)(reference * 10.0f) : INT16_MIN;
                wheel->angles_ddeg[3] = tracking ?
                    (int16_t)(wheel_wrap_degrees(reference + light_body) * 10.0f) : INT16_MIN;
                wheel->level = 0;
                wheel->state = WHEEL_RUNNING;
                wheel->detumble = DETUMBLE_ACTIVE;
                detumble_at = integrated_at = *phase_at = now;
                ramp_at = now - 200U;
                below = false;
                cruise_period = 0U;
                last_step_at = now - 50U;
                detail = "MANEUVER_STARTED";
            }
        } else if (action != WHEEL_CMD_STATUS) {
            if (batch.armed) {
                error = "BENCH_USE_AUTOMATIC_COMMAND";
            } else if (wheel->state != WHEEL_READY && wheel->state != WHEEL_RUNNING) {
                error = "WHEEL_NOT_READY";
            } else if (wheel->detumble == DETUMBLE_ACTIVE || result_hold) {
                error = "STOP_AUTOMATIC_FIRST";
            } else if (action < 0 && wheel->profile != WHEEL_PROFILE_BIDIR) {
                error = "REVERSE_UNSUPPORTED";
            } else if (wheel->level && ((wheel->level < 0) != (action < 0))) {
                error = "STOP_BEFORE_REVERSE";
            } else {
                if (wheel->state == WHEEL_READY) {
                    *phase_at = now;
                    last_step_at = now - 50U;
                }
                wheel->state = WHEEL_RUNNING;
                wheel->level = (int8_t)action;
                detail = "WHEEL_SETPOINT_ACCEPTED";
            }
        }
        if (command.id) {
            wheel_reply(command.id, error == NULL, error ? error : detail);
        } else {
            uart2_write(error ? "[WHEEL] Orden local rechazada; revisa perfil, IMU y estado\r\n"
                              : "[WHEEL] Orden local aplicada\r\n");
        }
    }
    if (wheel->state == WHEEL_RUNNING && now - last_step_at >= 50U) {
        last_step_at = now;
        const int target = (int)wheel_neutral(wheel->profile) + 5 * wheel->level;
        const int delta = target - wheel->pulse;
        wheel->pulse = (uint16_t)(wheel->pulse + (delta > 5 ? 5 : delta < -5 ? -5 : delta));
    }
    ADCS_ControlSetBenchPulse(&adcs_control, wheel->pulse);
    taskENTER_CRITICAL();
    wheel_telemetry = *wheel;
    taskEXIT_CRITICAL();
}

void HAL_GPIO_EXTI_Callback(uint16_t pin)
{
    if (!WHEEL_BENCH_MODE || pin != WHEEL_TACH_Pin) { return; }
    const uint32_t now = HAL_GetTick();
    const uint32_t elapsed = now - wheel_tach_ms;
    if (wheel_tach_seen && elapsed < 2U) { return; }
    wheel_tach_period_ms = wheel_tach_seen && elapsed < 2000U ? elapsed : 0U;
    wheel_tach_ms = now;
    wheel_tach_seen = true;
}

static bool photo_adc_init(void)
{
    __HAL_RCC_ADC_CLK_ENABLE();
    MODIFY_REG(ADC123_COMMON->CCR, ADC_CCR_CKMODE, ADC_CCR_CKMODE);
    CLEAR_BIT(ADC1->CR, ADC_CR_DEEPPWD);
    SET_BIT(ADC1->CR, ADC_CR_ADVREGEN);
    HAL_Delay(1U);
    CLEAR_BIT(ADC1->CR, ADC_CR_ADCALDIF);
    SET_BIT(ADC1->CR, ADC_CR_ADCAL);
    uint32_t start = HAL_GetTick();
    while ((ADC1->CR & ADC_CR_ADCAL) != 0U) {
        if (HAL_GetTick() - start >= 5U) { return false; }
    }
    HAL_Delay(1U);
    /* HCLK/4, single-ended 12-bit samples, 640.5 cycles per channel. */
    ADC1->SMPR1 = ADC_SMPR1_SMP3_Msk | ADC_SMPR1_SMP4_Msk |
                  ADC_SMPR1_SMP5_Msk | ADC_SMPR1_SMP6_Msk;
    if (SOLAR_TELEMETRY) { ADC1->SMPR2 = ADC_SMPR2_SMP13_Msk | ADC_SMPR2_SMP14_Msk; }
    else if (EPS_UART_TELEMETRY) { ADC1->SMPR2 = ADC_SMPR2_SMP13_Msk; }
    ADC1->ISR = ADC_ISR_ADRDY;
    SET_BIT(ADC1->CR, ADC_CR_ADEN);
    start = HAL_GetTick();
    while ((ADC1->ISR & ADC_ISR_ADRDY) == 0U) {
        if (HAL_GetTick() - start >= 5U) { return false; }
    }
    return true;
}

static bool analog_read(uint8_t channel, uint16_t *sample)
{
    if ((ADC1->CR & (ADC_CR_ADSTART | ADC_CR_ADSTP)) != 0U) { return false; }
    ADC1->SQR1 = (uint32_t)channel << ADC_SQR1_SQ1_Pos;
    /* Discard the first result after switching channels. */
    for (unsigned pass = 0; pass < 2U; ++pass) {
        ADC1->ISR = ADC_ISR_EOC | ADC_ISR_EOS | ADC_ISR_OVR;
        SET_BIT(ADC1->CR, ADC_CR_ADSTART);
        const uint32_t start = HAL_GetTick();
        while ((ADC1->ISR & ADC_ISR_EOS) == 0U && HAL_GetTick() - start < 2U) { }
        if ((ADC1->ISR & (ADC_ISR_EOS | ADC_ISR_OVR)) != ADC_ISR_EOS) {
            if (ADC1->CR & ADC_CR_ADSTART) { SET_BIT(ADC1->CR, ADC_CR_ADSTP); }
            return false;
        }
        *sample = (uint16_t)(ADC1->DR & P2P_LIGHT_MAX);
    }
    return true;
}

static void photo_adc_sample(void)
{
    /* Face order follows CN7 pins 28, 30, 35, 37. */
    static const uint8_t channels[P2P_LIGHT_CHANNELS] = {5U, 6U, 3U, 4U};
    uint16_t samples[P2P_LIGHT_CHANNELS] = {0};
    uint8_t valid = 0U;
    /* In the EPS build PA0 and PA1 are reserved for two voltage inputs. */
    for (unsigned face = SOLAR_TELEMETRY ? 2U : 0U;
         WHEEL_BENCH_MODE && face < P2P_LIGHT_CHANNELS; ++face) {
        if (analog_read(channels[face], &samples[face])) { valid |= (uint8_t)(1U << face); }
    }
    static const uint8_t eps_channels[EPS_ADC_CHANNELS] = {13U, 14U, 5U, 6U};
    uint16_t readings[EPS_ADC_CHANNELS] = {0};
    uint8_t eps_valid = 0U;
    const unsigned eps_count = SOLAR_TELEMETRY ? EPS_ADC_CHANNELS : EPS_UART_TELEMETRY ? 1U : 0U;
    for (unsigned i = 0; i < eps_count; ++i) {
        if (analog_read(eps_channels[i], &readings[i])) { eps_valid |= (uint8_t)(1U << i); }
    }
    taskENTER_CRITICAL();
    memcpy(photo_samples, samples, sizeof(photo_samples));
    photo_valid_mask = valid;
    memcpy(eps_adc, readings, sizeof(readings));
    eps_valid_mask = eps_valid;
    photo_sample_ms = HAL_GetTick();
    taskEXIT_CRITICAL();
}

#if EPS_UART_TELEMETRY
static void eps_uart_init(void)
{
    __HAL_RCC_USART3_CLK_ENABLE();
    __HAL_RCC_USART3_FORCE_RESET();
    __HAL_RCC_USART3_RELEASE_RESET();
    CLEAR_BIT(RCC->CCIPR, RCC_CCIPR_USART3SEL);
    GPIO_InitTypeDef rx = {
        .Pin = GPIO_PIN_5, .Mode = GPIO_MODE_AF_PP, .Pull = GPIO_PULLUP,
        .Speed = GPIO_SPEED_FREQ_LOW, .Alternate = GPIO_AF7_USART3
    };
    HAL_GPIO_Init(GPIOC, &rx);
    USART3->BRR = (HAL_RCC_GetPCLK1Freq() + 57600U) / 115200U;
    USART3->CR3 = USART_CR3_EIE;
    USART3->CR1 = USART_CR1_RE | USART_CR1_RXNEIE | USART_CR1_UE;
    HAL_NVIC_SetPriority(USART3_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(USART3_IRQn);
}

void USART3_IRQHandler(void)
{
    const uint32_t status = USART3->ISR;
    if (status & (USART_ISR_ORE | USART_ISR_FE | USART_ISR_NE)) {
        if (status & USART_ISR_RXNE) { (void)USART3->RDR; }
        USART3->ICR = USART_ICR_ORECF | USART_ICR_FECF | USART_ICR_NECF;
        eps_uart_overrun = true;
        ++eps_uart_errors;
    } else if (status & USART_ISR_RXNE) {
        const uint8_t byte = (uint8_t)USART3->RDR;
        ++eps_uart_bytes;
        /* Do not queue post-loss bytes until the task resynchronizes. */
        if (eps_uart_overrun) { return; }
        const uint16_t next = (uint16_t)((eps_uart_head + 1U) % EPS_UART_RING_SIZE);
        if (next == eps_uart_tail) {
            eps_uart_overrun = true;
            ++eps_uart_errors;
        } else {
            eps_uart_ring[eps_uart_head] = byte;
            eps_uart_head = next;
        }
    }
}

static void eps_uart_poll(void)
{
    static char line[64];
    static size_t used;
    static bool discard;
    static bool link_active;
    static uint32_t last_report_ms;
    taskENTER_CRITICAL();
    if (eps_uart_overrun) {
        eps_uart_tail = eps_uart_head;
        eps_uart_overrun = false;
        used = 0U;
        discard = true;
    }
    taskEXIT_CRITICAL();
    for (unsigned limit = 0; limit < EPS_UART_RING_SIZE && eps_uart_tail != eps_uart_head; ++limit) {
        const uint8_t byte = eps_uart_ring[eps_uart_tail];
        eps_uart_tail = (uint16_t)((eps_uart_tail + 1U) % EPS_UART_RING_SIZE);
        if (byte == '\n') {
            line[used] = '\0';
            EpsUartSample next = {0};
            int consumed = 0;
            if (!discard && sscanf(line, "EPSI,%5u,%1u,%5u,%9ld,%5u%n",
                    &next.sequence, &next.flags, &next.solar_mv, &next.solar_ua,
                    &next.battery_mv, &consumed) == 5 && (size_t)consumed == used &&
                    next.sequence <= 65535U && next.flags <= 3U &&
                    next.solar_mv <= 32000U && next.battery_mv <= 33000U &&
                    next.solar_ua >= -10000000L && next.solar_ua <= 10000000L) {
                next.received_ms = HAL_GetTick();
                taskENTER_CRITICAL();
                eps_uart_sample = next;
                taskEXIT_CRITICAL();
                if (!link_active) {
                    char message[80];
                    snprintf(message, sizeof(message), "[EPS] PC5 RX OK: EPSI,%u,%u,%u,%ld,%u\r\n",
                             next.sequence, next.flags, next.solar_mv, next.solar_ua, next.battery_mv);
                    uart2_write(message);
                    link_active = true;
                }
            }
            used = 0U;
            discard = false;
        } else if (byte != '\r' && !discard) {
            if (used < sizeof(line) - 1U && byte >= 32U && byte <= 126U) {
                line[used++] = (char)byte;
            } else {
                discard = true;
            }
        }
    }
    const uint32_t now_ms = HAL_GetTick();
    if (now_ms - eps_uart_sample.received_ms > 3500U) {
        link_active = false;
        if (now_ms - last_report_ms >= 5000U) {
            char message[100];
            snprintf(message, sizeof(message),
                     "[EPS] PC5 sin EPSI valido (115200 8N1): bytes=%lu errores=%lu\r\n",
                     (unsigned long)eps_uart_bytes, (unsigned long)eps_uart_errors);
            uart2_write(message);
            last_report_ms = now_ms;
        }
    }
}
#endif

void Task_Safety(void *argument)
{
    (void)argument;
    bool imu_fault_latched = false;
    AdcsCommand_t command = { .type = ADCS_CMD_DISARM };
    TickType_t last_wake = xTaskGetTickCount();
    WheelTelemetry wheel = { .axis = 3U, .body_crpm = INT16_MIN };
    uint32_t phase_at = 0U, enabled_at = 0U;
    bool enabled = false;
    const bool photo_enabled = WHEEL_BENCH_MODE || SOLAR_TELEMETRY || EPS_UART_TELEMETRY;
    const bool photo_ready = photo_enabled && photo_adc_init();
    if (photo_enabled && !photo_ready) { uart2_write("[PHOTO] ADC1 no disponible\r\n"); }

    for (;;) {
        const uint32_t now_ms = HAL_GetTick();
        const bool imu_stale = sensor_task_started &&
                               ((now_ms - imu_last_valid_ms) > IMU_SAFETY_TIMEOUT_MS);
        if (WHEEL_BENCH_MODE) {
            wheel_service(&wheel, &phase_at, &enabled, &enabled_at, now_ms, imu_stale);
        } else if (imu_stale && !imu_fault_latched) {
            if (xQueueSend(adcsCommandQueueHandle, &command, 0U) == pdPASS) {
                imu_fault_latched = true;
                uart2_write("[SAFETY] IMU timeout; orden DISARM\r\n");
            }
        } else if (!imu_stale) {
            imu_fault_latched = false;
        }
        if (photo_ready) { photo_adc_sample(); }
#if EPS_UART_TELEMETRY
        eps_uart_poll();
#endif
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(50U));
    }
}

static uint32_t telemetry_interval_ms(size_t length, int signal)
{
    if (signal > -45) signal = -45;
    if (signal < -95) signal = -95;
    /* From 1% airtime at -45 dBm to about 1/3% at -95 dBm. */
    uint32_t interval = P2P_TimeOnAirUs(length) * (uint32_t)(50 + 2 * (-45 - signal)) / 500U;
    uint32_t rate_limit = 1000U / p2p_rate_hz;
    return interval > rate_limit ? interval : rate_limit;
}

void Task_RadioTest(void *argument)
{
    char msg[192];

    vTaskDelay(pdMS_TO_TICKS(500));

    snprintf(msg, sizeof(msg), "[BUILD] radio_test %s %s\r\n", __DATE__, __TIME__);
    uart2_write(msg);

    radio_set_log_sink(uart2_write);
    p2p_watchdog_init();
    P2P_AirtimeBudget budget;
    P2P_BudgetInit(&budget, HAL_GetTick());
    uint32_t next_telemetry = HAL_GetTick() + 500U;
    uint32_t next_full_telemetry = next_telemetry;
    uint32_t last_tx = HAL_GetTick(), last_budget_log = 0, last_health = HAL_GetTick();
    unsigned failures = 0, recoveries = 0;
    bool listening = radio_rx_start(), pending = false;
    char response[P2P_RF_MAX + 1U];
    ADSB_RadioFrame aircraft = {0};
    bool last_telemetry_had_aircraft = false;
    bool last_telemetry_was_full = false;
    int rssi = -95;
    uint32_t last_link = HAL_GetTick() - 120000U;
    size_t last_telemetry_len = 0U;
    uint32_t last_telemetry_tx = 0U;
    P2P_WheelState last_wheel_state = WHEEL_IDLE;
    bool wheel_ready_pending = false;
    P2P_DetumbleState last_detumble_state = DETUMBLE_NONE;
    uart2_write(WHEEL_BENCH_MODE ? "[P2P] Binary Q + wheel ready\r\n"
                                : "[P2P] Binary Q ready; full TEL target every 10 s\r\n");
    for (;;) {
        p2p_watchdog_feed();
        if (!listening || failures >= 3U) {
            if (++recoveries > 3U) {
                uart2_write("[P2P] Recovery exhausted; watchdog will restart firmware\r\n");
                vTaskDelete(NULL); return;
            }
            uart2_write("[P2P] Controlled radio reset/reinitialization\r\n");
            vTaskDelay(pdMS_TO_TICKS(1000U));
            listening = radio_diagnostic_run(&hspi1) && radio_rx_start();
            failures = 0;
            continue;
        }
        if (HAL_GetTick()-last_health >= 1000U) {
            last_health = HAL_GetTick();
            if (!radio_rx_health()) { listening = false; continue; }
        }
        uint8_t received[P2P_RF_MAX + 1U];
        int n = radio_rx_poll(received, sizeof(received));
        if (n < 0) { failures++; listening = false; continue; }
        if (n > 5 && memcmp(received, "LINK,", 5U) == 0) {
            char *end;
            long sample = strtol((char *)received + 5, &end, 10);
            if (received[5] == '-' && end == (char *)received + n && sample >= -130 && sample <= 0) {
                uint32_t now = HAL_GetTick();
                rssi = (now - last_link >= 120000U) ? (int)sample : (2 * rssi + (int)sample) / 3;
                last_link = now;
                wheel_link_ms = now;
                wheel_link_seen = true;
                if (last_telemetry_len != 0U) {
                    uint32_t interval = telemetry_interval_ms(last_telemetry_len, rssi);
                    next_telemetry = last_telemetry_tx + interval;
                    snprintf(msg, sizeof(msg), "[P2P] LINK RSSI=%d dBm interval=%lu ms\r\n",
                             rssi, (unsigned long)interval);
                    uart2_write(msg);
                }
            }
            n = 0;
        }
        if (n > 0) {
            P2P_Command command;
            const char *error = P2P_ParseCommand((char *)received, (size_t)n, &command);
            if (command.id) {
                const char *cached;
                taskENTER_CRITICAL();
                const bool wheel_armed = wheel_telemetry.state != WHEEL_IDLE;
                taskEXIT_CRITICAL();
                int duplicate = P2P_CacheClaim(&p2p_cache, command.id,
                                              P2P_Hash(received, (size_t)n), &cached);
                if (duplicate < 0) {
                    char conflict[64];
                    snprintf(conflict, sizeof(conflict), "ACK,%u,ERR,ID_CONFLICT", command.id);
                    (void)p2p_emit(conflict);
                } else if (duplicate > 0) {
                    if (cached && cached[0]) { (void)p2p_emit(cached); }
                } else if (error) {
                    p2p_local_reply(command.id, error);
                } else if (command.name == P2P_RELAY) {
                    p2p_relay_command(&command);
                } else if (command.name == P2P_WHEEL) {
                    if (!WHEEL_BENCH_MODE) {
                        p2p_local_reply(command.id, "WHEEL_BENCH_DISABLED");
                    } else {
                        if (wheel_enqueue(&command) != pdPASS) {
                            p2p_local_reply(command.id, "BUSY");
                        }
                    }
                } else if (WHEEL_BENCH_MODE && wheel_armed &&
                           (command.name == P2P_CAL_GYRO || command.name == P2P_CAL_ACCEL ||
                            command.name == P2P_MAG_START || command.name == P2P_MAG_STOP ||
                            command.name == P2P_LOAD_CONFIG || command.name == P2P_RESET_FILTER)) {
                    p2p_local_reply(command.id, "DISARM_WHEEL_FIRST");
                } else if (xQueueSend(p2pCommandQueueHandle, &command, 0U) != pdPASS) {
                    p2p_local_reply(command.id, "BUSY");
                }
            }
        }
        if (!pending && xQueueReceive(p2pResponseQueueHandle, response, 0U) == pdPASS) {
            pending = true;
            unsigned id = 0;
            if (!strstr(response, ",ERR,ID_CONFLICT") &&
                (sscanf(response, "ACK,%u,", &id) == 1 || sscanf(response, "EVT,%u,", &id) == 1)) {
                P2P_CacheReply(&p2p_cache, (uint16_t)id, response);
            }
        }
        uint32_t now = HAL_GetTick();
        if (WHEEL_BENCH_MODE) {
            taskENTER_CRITICAL();
            P2P_WheelState wheel_state = wheel_telemetry.state;
            P2P_DetumbleState detumble_state = wheel_telemetry.detumble;
            taskEXIT_CRITICAL();
            if (wheel_state != last_wheel_state || detumble_state != last_detumble_state) {
                if (wheel_state != last_wheel_state) {
                    wheel_ready_pending = wheel_state == WHEEL_READY;
                }
                last_wheel_state = wheel_state;
                last_detumble_state = detumble_state;
                next_telemetry = now;
            }
        }
        /* Every completed TX is followed by at least 150 ms of RX. Responses
         * have priority; telemetry is reduced by the shared airtime budget. */
        if (now - last_tx >= 150U) {
            uint8_t payload[P2P_RF_MAX + 1U];
            size_t length = 0U;
            bool send = false;
            bool includes_aircraft = false;
            const bool full_due = !wheel_ready_pending &&
                                  (SOLAR_TELEMETRY || EPS_UART_TELEMETRY || !WHEEL_BENCH_MODE) && !last_telemetry_was_full &&
                                  (int32_t)(now - next_full_telemetry) >= 0;
            if (pending) {
                length = strlen(response);
                memcpy(payload, response, length + 1U);
                send = P2P_BudgetTake(&budget, now, length, true);
            } else if (full_due || (int32_t)(now-next_telemetry) >= 0) {
                length = telemetry_copy(payload, sizeof(payload), full_due);
                while (aircraft.line[0] == '\0' ||
                       (int32_t)(now - aircraft.received_ms) >= 30000) {
                    aircraft.line[0] = '\0';
                    if (xQueueReceive(adsbRadioQueue, &aircraft, 0U) != pdPASS) break;
                }
                /* With continuous ADS-B traffic, leave every alternate packet short. */
                if (!WHEEL_BENCH_MODE && !full_due && !last_telemetry_was_full &&
                    !last_telemetry_had_aircraft && aircraft.line[0] != '\0') {
                    size_t frame_len = strlen(aircraft.line);
                    if (length + 1U + frame_len <= P2P_RF_MAX) {
                        payload[length++] = '|';
                        memcpy(payload + length, aircraft.line, frame_len + 1U);
                        length += frame_len;
                        includes_aircraft = true;
                    }
                }
                send = length && P2P_BudgetTake(&budget, now, length, wheel_ready_pending);
                if (!send && now-last_budget_log >= 15000U) {
                    uart2_write("[P2P] TEL throttled by airtime budget; RX active\r\n");
                    last_budget_log = now;
                }
            }
            if (send) {
                if (!pending && full_due && (SOLAR_TELEMETRY || EPS_UART_TELEMETRY)) {
                    uart2_write("[P2P] TX EPS telemetry\r\n");
                } else if (payload[0] == P2P_WHEEL_V1) {
                    uart2_write("[P2P] TX Q + wheel + light binary\r\n");
                } else if (payload[0] == P2P_QUAT_V1) {
                    snprintf(msg, sizeof(msg), "[P2P] TX Q binary %u bytes%s\r\n",
                             (unsigned)length, includes_aircraft ? " + ADSB" : "");
                } else {
                    snprintf(msg, sizeof(msg), "[P2P] TX %.160s\r\n", (const char *)payload);
                }
                uart2_write(msg);
                bool tx_ok = radio_send_payload(payload, (uint8_t)length);
                listening = radio_rx_start(); /* Always rearm, including failed TX. */
                last_tx = HAL_GetTick();
                if (tx_ok) {
                    failures = 0; recoveries = 0;
                    if (pending) { pending = false; }
                    else {
                        wheel_ready_pending = false;
                        if (includes_aircraft) aircraft.line[0] = '\0';
                        last_telemetry_had_aircraft = includes_aircraft;
                        int signal = last_tx - last_link < 120000U ? rssi : -95;
                        if (full_due) { next_full_telemetry = last_tx + FULL_TELEMETRY_PERIOD_MS; }
                        last_telemetry_was_full = full_due && (SOLAR_TELEMETRY || EPS_UART_TELEMETRY ||
                                                               memcmp(payload, "TEL,", 4U) == 0);
                        last_telemetry_len = last_telemetry_was_full ?
                            (WHEEL_BENCH_MODE ? P2P_WHEEL_MANEUVER_LEN : P2P_QUAT_V1_LEN) : length;
                        last_telemetry_tx = last_tx;
                        next_telemetry = last_tx + telemetry_interval_ms(last_telemetry_len, signal);
                    }
                } else { failures++; }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(5U));
    }
}

void Task_SensorFusion(void *argument)
{
    (void)argument;
    char msg[192];
    FusionAhrs ahrs;
    FusionAhrsInitialise(&ahrs);
    FusionAhrsSettings settings = fusionAhrsDefaultSettings;
    settings.gain = FUSION_GAIN;
    settings.gyroscopeRange = MPU9250_GYRO_RANGE_DPS;
    settings.accelerationRejection = 90.0f;
    settings.magneticRejection = 20.0f;
    settings.recoveryTriggerPeriod = 250U;
    FusionAhrsSetSettings(&ahrs, &settings);

    FusionBias gyro_bias;
    FusionBiasInitialise(&gyro_bias);
    FusionBiasSettings bias_settings = fusionBiasDefaultSettings;
    bias_settings.sampleRate = FUSION_SAMPLE_RATE_HZ;
    bias_settings.stationaryThreshold = FUSION_BIAS_STATIONARY_THRESHOLD_DPS;
    bias_settings.stationaryPeriod = FUSION_BIAS_STATIONARY_PERIOD_S;
    FusionBiasSetSettings(&gyro_bias, &bias_settings);

    P2P_ControlInit(&p2p_control, p2p_emit, P2P_FlashSave, P2P_FlashLoad);
    uint32_t applied_revision = UINT32_MAX;
    bool imu_ok = (MPU9250_Init(&hi2c1) == HAL_OK);
    uint8_t who_am_i = 0U;
    uint8_t mag_who_am_i = 0U;
    (void)MPU9250_ReadWhoAmI(&who_am_i);
    bool has_magnetometer = MPU9250_HasMagnetometer();
    if (has_magnetometer) (void)MPU9250_ReadMagWhoAmI(&mag_who_am_i);
    snprintf(msg, sizeof(msg), "[IMU] I2C MPU=%s ID=0x%02X MAG=%s ID=0x%02X\r\n",
             imu_ok ? "OK" : "FAIL", who_am_i,
             has_magnetometer ? "OK" : "NO", mag_who_am_i);
    uart2_write(msg);
    uart2_write(has_magnetometer
                ? "[ORI] Fusion 9-DOF con magnetometro\r\n"
                : "[ORI] Fusion 6-DOF; yaw relativo sin magnetometro\r\n");
    MPU9250_Data_t last_data = {0};
    bool sample_ok = false;
    bool imu_failure_reported = false;
    uint32_t last_update_ms = HAL_GetTick();
    uint32_t last_uart_ms = last_update_ms;
    uint32_t last_mag_diag_ms = last_update_ms;
    uint8_t failure_count = 0U;
    TickType_t last_wake = xTaskGetTickCount();
    imu_last_valid_ms = HAL_GetTick();
    sensor_task_started = true;

    for (;;) {
        process_adcs_commands();
        const uint32_t now_ms = HAL_GetTick();
        float delta_time = (float)(now_ms - last_update_ms) * 0.001f;
        last_update_ms = now_ms;
        if ((delta_time <= 0.0f) || (delta_time > 0.1f)) delta_time = 0.02f;

        MPU9250_RawData_t raw;
        MPU9250_Data_t data;
        sample_ok = imu_ok && (MPU9250_ReadRaw(&raw) == HAL_OK);
        if (sample_ok) {
            if (imu_failure_reported) {
                uart2_write("[IMU] Lecturas recuperadas; quat valido\r\n");
                imu_failure_reported = false;
            }
            failure_count = 0U;
            imu_last_valid_ms = now_ms;
            MPU9250_Convert(&raw, &data);
            P2P_Sample sample = {
                .accel = { data.ax, data.ay, data.az },
                .gyro = { data.gx, data.gy, data.gz },
                .mag = { data.mx, data.my, data.mz },
                .imu_ok = true, .mag_ok = data.magnetometer_valid
            };
            P2P_ControlSample(&p2p_control, &sample, now_ms);
            P2P_Command command;
            if (uxQueueSpacesAvailable(p2pResponseQueueHandle) >= 3U &&
                xQueueReceive(p2pCommandQueueHandle, &command, 0U) == pdPASS) {
                P2P_ControlCommand(&p2p_control, &command, now_ms);
            }
            if (applied_revision != p2p_control.revision) {
                settings.gain = p2p_control.config.gain;
                if (p2p_control.reset_filter) {
                    FusionAhrsRestart(&ahrs);
                    FusionBiasSetOffset(&gyro_bias, FUSION_VECTOR_ZERO);
                    p2p_control.reset_filter = false;
                }
                FusionAhrsSetSettings(&ahrs, &settings);
                p2p_rate_hz = p2p_control.config.rate_hz;
                applied_revision = p2p_control.revision;
            }
            P2P_ApplyCalibration(&p2p_control.config, &sample);
            const bool fusion_9dof = FUSION_USE_MAGNETOMETER != 0U &&
                                     p2p_control.config.mode == 9U &&
                                     p2p_control.config.mag_calibrated &&
                                     has_magnetometer && data.magnetometer_valid;
            taskENTER_CRITICAL();
            memcpy(wheel_body_rate, sample.gyro, sizeof(wheel_body_rate));
            wheel_rate_ms = now_ms;
            wheel_rate_calibrated = p2p_control.config.gyro_calibrated && !p2p_control.calibration;
            wheel_sensor_flags = (p2p_control.config.gyro_calibrated ? P2P_ADCS_GYRO_CAL : 0U) |
                                 (p2p_control.config.mag_calibrated ? P2P_ADCS_MAG_CAL : 0U) |
                                 (has_magnetometer && data.magnetometer_valid ? P2P_ADCS_MAG_VALID : 0U) |
                                 (fusion_9dof ? P2P_ADCS_FUSION_9DOF : 0U);
            taskEXIT_CRITICAL();
            data.ax=sample.accel[0]; data.ay=sample.accel[1]; data.az=sample.accel[2];
            data.gx=sample.gyro[0]; data.gy=sample.gyro[1]; data.gz=sample.gyro[2];
            data.mx=sample.mag[0]; data.my=sample.mag[1]; data.mz=sample.mag[2];
            last_data = data;
            const FusionVector gyroscope = {
                .axis = { .x = data.gx, .y = data.gy, .z = data.gz }
            };
            const FusionVector corrected_gyroscope = FusionBiasUpdate(&gyro_bias, gyroscope);
            const FusionVector accelerometer = {
                .axis = { .x = data.ax, .y = data.ay, .z = data.az }
            };
            if (fusion_9dof) {
                const FusionVector magnetometer = {
                    .axis = { .x = data.mx, .y = data.my, .z = data.mz }
                };
                FusionAhrsUpdate(&ahrs, corrected_gyroscope, accelerometer,
                                 magnetometer, delta_time);
            } else {
                FusionAhrsUpdateNoMagnetometer(&ahrs, corrected_gyroscope,
                                               accelerometer, delta_time);
            }
            ADCS_ControlUpdate(&adcs_control, FusionAhrsGetQuaternion(&ahrs),
                               corrected_gyroscope, true);
        } else {
            P2P_Sample failed = {0};
            taskENTER_CRITICAL();
            wheel_rate_calibrated = false;
            wheel_sensor_flags &= (P2P_ADCS_GYRO_CAL | P2P_ADCS_MAG_CAL);
            taskEXIT_CRITICAL();
            P2P_ControlSample(&p2p_control, &failed, now_ms);
            P2P_Command command;
            if (uxQueueSpacesAvailable(p2pResponseQueueHandle) >= 3U &&
                xQueueReceive(p2pCommandQueueHandle, &command, 0U) == pdPASS) {
                P2P_ControlCommand(&p2p_control, &command, now_ms);
            }
            if (!imu_failure_reported) {
                uart2_write("[IMU] Lectura fallida: payload QERR,MPU9250\r\n");
                imu_failure_reported = true;
            }
            if (failure_count < 255U) failure_count++;
            if (failure_count >= 5U) {
                ADCS_ControlUpdate(&adcs_control, FUSION_QUATERNION_IDENTITY,
                                   FUSION_VECTOR_ZERO, false);
                imu_ok = (MPU9250_Init(&hi2c1) == HAL_OK);
                has_magnetometer = MPU9250_HasMagnetometer();
                failure_count = 0U;
                uart2_write(imu_ok ? "[IMU] MPU9250 reinit OK\r\n"
                                   : "[IMU] MPU9250 reinit FAIL\r\n");
            }
        }

        telemetry_publish(FusionAhrsGetQuaternion(&ahrs), sample_ok,
                          sample_ok ? &last_data : NULL, has_magnetometer,
                          p2p_control.config.mag_calibrated != 0U);

        if (UART_SENSOR_STREAM_ENABLED && (now_ms - last_uart_ms) >= QUAT_UART_PERIOD_MS) {
            last_uart_ms = now_ms;
            const FusionQuaternion q = sample_ok ? FusionAhrsGetQuaternion(&ahrs)
                                                 : FUSION_QUATERNION_IDENTITY;
            snprintf(msg, sizeof(msg),
                     "T=%lu|A=%d,%d,%d|G=%d,%d,%d|M=%d,%d,%d|Q=%d,%d,%d,%d|MAG=%s\r\n",
                     (unsigned long)now_ms,
                     clamp_float_to_i16(last_data.ax * 1000.0f),
                     clamp_float_to_i16(last_data.ay * 1000.0f),
                     clamp_float_to_i16(last_data.az * 1000.0f),
                     clamp_float_to_i16(last_data.gx * 100.0f),
                     clamp_float_to_i16(last_data.gy * 100.0f),
                     clamp_float_to_i16(last_data.gz * 100.0f),
                     clamp_float_to_i16(last_data.mx * 10.0f),
                     clamp_float_to_i16(last_data.my * 10.0f),
                     clamp_float_to_i16(last_data.mz * 10.0f),
                     quat_to_q10000(q.element.w), quat_to_q10000(q.element.x),
                     quat_to_q10000(q.element.y), quat_to_q10000(q.element.z),
                     has_magnetometer
                         ? (last_data.magnetometer_valid ? "OK" : "ERR")
                         : "N/A");
            uart2_write(msg);
        }

        if (UART_SENSOR_STREAM_ENABLED && (now_ms - last_mag_diag_ms) >= MAG_DIAG_PERIOD_MS) {
            last_mag_diag_ms = now_ms;
            snprintf(msg, sizeof(msg), "[MAGD] uT10=%d,%d,%d valid=%u\r\n",
                     clamp_float_to_i16(last_data.mx * 10.0f),
                     clamp_float_to_i16(last_data.my * 10.0f),
                     clamp_float_to_i16(last_data.mz * 10.0f),
                     last_data.magnetometer_valid ? 1U : 0U);
            uart2_write(msg);
        }
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(20U));
    }
}

#if 0 /* Implementación MPU6050 + HMC5883L conservada solo como referencia histórica. */
void Task_SensorFusion_MPU6050_HMC5883L(void *argument)
{
    char msg[192];

    FusionAhrs ahrs;
    FusionAhrsInitialise(&ahrs);
    FusionAhrsSettings ahrs_settings = fusionAhrsDefaultSettings;
    ahrs_settings.gain = FUSION_GAIN;
    ahrs_settings.gyroscopeRange = 250.0f;
    /*
     * La fusión no usa magnetómetro para corregir orientación, por lo que el
     * acelerómetro es la única referencia absoluta de roll/pitch. 20° era
     * demasiado estricto: si el equipo arrancaba o se movía y quedaba con más
     * error, Fusion ignoraba el acelerómetro y el quaternion no podía volver.
     */
    ahrs_settings.accelerationRejection = 90.0f;
    ahrs_settings.magneticRejection = 20.0f;
    ahrs_settings.recoveryTriggerPeriod = 250U;
    FusionAhrsSetSettings(&ahrs, &ahrs_settings);

    FusionBias gyro_bias;
    FusionBiasInitialise(&gyro_bias);
    FusionBiasSettings bias_settings = fusionBiasDefaultSettings;
    bias_settings.sampleRate = FUSION_SAMPLE_RATE_HZ;
    bias_settings.stationaryThreshold = FUSION_BIAS_STATIONARY_THRESHOLD_DPS;
    bias_settings.stationaryPeriod = FUSION_BIAS_STATIONARY_PERIOD_S;
    FusionBiasSetSettings(&gyro_bias, &bias_settings);

    bool imu_ok = (MPU6050_Init(&hi2c1) == HAL_OK);
    snprintf(msg, sizeof(msg), "[IMU] MPU6050=%s\r\n", imu_ok ? "OK" : "FAIL");
    uart2_write(msg);

    uint8_t mag_id[3] = {0};
    HAL_StatusTypeDef mag_id_status = HMC5883L_ReadId(&hi2c1, mag_id);
    snprintf(msg, sizeof(msg), "[MAG] ID ret=%ld bytes=%02X %02X %02X\r\n",
             (long)mag_id_status, mag_id[0], mag_id[1], mag_id[2]);
    uart2_write(msg);

    bool mag_ok = (HMC5883L_Init(&hi2c1) == HAL_OK);
    snprintf(msg, sizeof(msg), "[MAG] HMC5883L=%s\r\n", mag_ok ? "OK" : "FAIL");
    uart2_write(msg);
    const char *ori_msg = "[ORI] chip-down body: X=chipX Y=-chipY Z=chipZ heading_sign=+1 offset=0\r\n";
    uart2_write(ori_msg);
    const char *mag_map_msg = "[MAGMAP] bodyX=+chipX bodyY=-chipY bodyZ=+chipZ\r\n";
    uart2_write(mag_map_msg);

    uint32_t last_update_ms = HAL_GetTick();
    uint32_t last_uart_ms = HAL_GetTick();
    uint32_t last_mag_ms = 0U;
    uint32_t last_mag_diag_ms = 0U;

    MPU6050_CalibratedData_t last_cal = {0};
    HMC5883L_RawData_t last_mag_raw = {0};
    HMC5883L_CalData_t last_mag_chip = {0};
    HMC5883L_CalData_t last_mag_cal = {0};
    HMC5883L_CalData_t last_mag_compass = {0};
    float last_heading_deg = 0.0f;
    bool imu_data_ok = imu_ok;
    bool mag_data_ok = false;
    bool last_ecompass_ok = false;
    uint8_t mag_fail_count = 0U;
    TickType_t last_wake = xTaskGetTickCount();

    for (;;)
    {
        const uint32_t now_ms = HAL_GetTick();
        float delta_time = (float)(now_ms - last_update_ms) * 0.001f;
        last_update_ms = now_ms;
        if (delta_time <= 0.0f || delta_time > 0.1f) {
            delta_time = 0.02f;
        }

        imu_data_ok = false;
        mag_data_ok = false;

        if (imu_ok) {
            MPU6050_RawData_t raw;
            MPU6050_CalibratedData_t cal;
            if (MPU6050_ReadRaw(&hi2c1, &raw) == HAL_OK) {
                imu_data_ok = true;
                MPU6050_ApplyCalibration(&raw, &cal);
                last_cal = cal;

                const FusionVector gyroscope = {
                    .axis = { .x = cal.gx_w, .y = cal.gy_w, .z = cal.gz_w }
                };
                const FusionVector corrected_gyroscope = FusionBiasUpdate(&gyro_bias, gyroscope);
                const FusionVector accelerometer = {
                    .axis = { .x = cal.ax_w, .y = cal.ay_w, .z = cal.az_w }
                };

                if (mag_ok && ((now_ms - last_mag_ms) >= MAG_READ_PERIOD_MS)) {
                    last_mag_ms = now_ms;
                    HMC5883L_RawData_t mag_raw;
                    if (HMC5883L_ReadRaw(&hi2c1, &mag_raw) == HAL_OK) {
                        mag_data_ok = true;
                        mag_fail_count = 0U;
                        HMC5883L_CalData_t mag_chip = {0};
                        HMC5883L_CalData_t mag_world = {0};
                        HMC5883L_ApplyCalibration(&mag_raw, &mag_chip);
                        HMC5883L_RotateToBody(&mag_chip, &mag_world);
                        last_mag_raw = mag_raw;
                        last_mag_chip = mag_chip;
                        last_mag_cal = mag_world;
                        const FusionVector magnetometer = {
                            .axis = { .x = mag_world.x, .y = mag_world.y, .z = mag_world.z }
                        };
                        float heading = 0.0f;
                        if (calculate_ecompass_heading_an4248(accelerometer,
                                                              magnetometer,
                                                              &heading)) {
                            last_heading_deg = heading;
                            compass_vector_from_heading(heading, &last_mag_compass);
                            last_ecompass_ok = true;
                        } else {
                            last_mag_compass = (HMC5883L_CalData_t){0};
                            last_ecompass_ok = false;
                        }
                    } else {
                        if (mag_fail_count < 255U) {
                            mag_fail_count++;
                        }
                        if (mag_fail_count >= 5U) {
                            mag_ok = (HMC5883L_Init(&hi2c1) == HAL_OK);
                            mag_fail_count = 0U;
                            uart2_write("[MAG] reinit\r\n");
                        }
                    }
                }
                mag_data_ok = mag_ok &&
                              last_ecompass_ok &&
                              ((last_mag_raw.x != 0) || (last_mag_raw.y != 0) || (last_mag_raw.z != 0));

                if ((FUSION_USE_MAGNETOMETER != 0U) && mag_data_ok) {
                    const FusionVector magnetometer = {
                        .axis = { .x = last_mag_cal.x, .y = last_mag_cal.y, .z = last_mag_cal.z }
                    };
                    FusionAhrsUpdate(&ahrs, corrected_gyroscope, accelerometer, magnetometer, delta_time);
                } else {
                    FusionAhrsUpdateNoMagnetometer(&ahrs, corrected_gyroscope, accelerometer, delta_time);
                }
            } else {
                imu_ok = false;
                last_cal = (MPU6050_CalibratedData_t){0};
                last_mag_raw = (HMC5883L_RawData_t){0};
                last_mag_chip = (HMC5883L_CalData_t){0};
                last_mag_cal = (HMC5883L_CalData_t){0};
                last_mag_compass = (HMC5883L_CalData_t){0};
                last_ecompass_ok = false;
                FusionAhrsSetQuaternion(&ahrs, FUSION_QUATERNION_IDENTITY);
                uart2_write("[IMU] read FAIL\r\n");
            }
        }

        telemetry_publish(FusionAhrsGetQuaternion(&ahrs), imu_data_ok, NULL, false, false);

        if ((HAL_GetTick() - last_uart_ms) >= QUAT_UART_PERIOD_MS) {
            last_uart_ms = HAL_GetTick();
            FusionQuaternion q = imu_data_ok ? FusionAhrsGetQuaternion(&ahrs) : FUSION_QUATERNION_IDENTITY;
            snprintf(msg, sizeof(msg),
                     "T=%lu|AX=%d AY=%d AZ=%d|GX=%d GY=%d GZ=%d|MX=%d MY=%d MZ=%d|Q=%d,%d,%d,%d|MAG=%s\r\n",
                     (unsigned long)HAL_GetTick(),
                     clamp_float_to_i16(last_cal.ax_w * 1000.0f),
                     clamp_float_to_i16(last_cal.ay_w * 1000.0f),
                     clamp_float_to_i16(last_cal.az_w * 1000.0f),
                     clamp_float_to_i16(last_cal.gx_w * 100.0f),
                     clamp_float_to_i16(last_cal.gy_w * 100.0f),
                     clamp_float_to_i16(last_cal.gz_w * 100.0f),
                     clamp_float_to_i16(last_mag_compass.x),
                     clamp_float_to_i16(last_mag_compass.y),
                     clamp_float_to_i16(last_mag_compass.z),
                     quat_to_q10000(q.element.w),
                     quat_to_q10000(q.element.x),
                     quat_to_q10000(q.element.y),
                     quat_to_q10000(q.element.z),
                     mag_data_ok ? "OK" : "ERR");
            uart2_write(msg);
        }

        if ((HAL_GetTick() - last_mag_diag_ms) >= MAG_DIAG_PERIOD_MS) {
            last_mag_diag_ms = HAL_GetTick();
            snprintf(msg, sizeof(msg),
                     "[MAGD] R=%d,%d,%d C=%d,%d,%d B=%d,%d,%d H=%d OK=%u\r\n",
                     last_mag_raw.x,
                     last_mag_raw.y,
                     last_mag_raw.z,
                     clamp_float_to_i16(last_mag_chip.x),
                     clamp_float_to_i16(last_mag_chip.y),
                     clamp_float_to_i16(last_mag_chip.z),
                     clamp_float_to_i16(last_mag_cal.x),
                     clamp_float_to_i16(last_mag_cal.y),
                     clamp_float_to_i16(last_mag_cal.z),
                     clamp_float_to_i16(last_heading_deg * 10.0f),
                     mag_data_ok ? 1U : 0U);
            uart2_write(msg);
        }

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(20));
    }
}
#endif

/* USER CODE END 4 */

/* USER CODE BEGIN Header_StartDefaultTask */
/**
  * @brief  Function implementing the defaultTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartDefaultTask */
void StartDefaultTask(void *argument)
{
  /* USER CODE BEGIN 5 */
  (void)argument;
  char line[96];
  size_t length = 0U;
  uint8_t byte;

  uart2_write("[COMMAND] listo. Escriba ADCS HELP\r\n");
  for (;;) {
    if (HAL_UART_Receive(&huart2, &byte, 1U, 50U) != HAL_OK) continue;

    if ((byte == '\r') || (byte == '\n')) {
      if (length == 0U) continue;
      line[length] = '\0';
      length = 0U;

      if (strcmp(line, "ADCS HELP") == 0) {
        uart2_write(WHEEL_BENCH_MODE
                    ? "ADCS STATUS | WHEEL PROFILE BIDIR/AIR/OFF | WHEEL ENABLE | WHEEL STOP\r\n"
                    : "ARM|DISARM|AUTO|STATUS|MANUAL n|AXIS X/Y/Z|GAINS kp kd|TARGET w x y z\r\n");
        continue;
      }

      if (WHEEL_BENCH_MODE) {
        const int wheel_action =
          strcmp(line, "WHEEL PROFILE BIDIR") == 0 ? WHEEL_CMD_PROFILE_BIDIR :
          strcmp(line, "WHEEL PROFILE AIR") == 0 ? WHEEL_CMD_PROFILE_AIR :
          strcmp(line, "WHEEL PROFILE OFF") == 0 ? WHEEL_CMD_PROFILE_OFF :
          strcmp(line, "WHEEL AXIS X+") == 0 ? WHEEL_CMD_AXIS_X_POS :
          strcmp(line, "WHEEL AXIS X-") == 0 ? WHEEL_CMD_AXIS_X_NEG :
          strcmp(line, "WHEEL AXIS Y+") == 0 ? WHEEL_CMD_AXIS_Y_POS :
          strcmp(line, "WHEEL AXIS Y-") == 0 ? WHEEL_CMD_AXIS_Y_NEG :
          strcmp(line, "WHEEL AXIS Z+") == 0 ? WHEEL_CMD_AXIS_Z_POS :
          strcmp(line, "WHEEL AXIS Z-") == 0 ? WHEEL_CMD_AXIS_Z_NEG :
          strcmp(line, "WHEEL ENABLE") == 0 ? WHEEL_CMD_ENABLE :
          strcmp(line, "WHEEL STOP") == 0 || strcmp(line, "ADCS DISARM") == 0 ? WHEEL_CMD_STOP : 0;
        if (wheel_action) {
          const P2P_Command wheel_command = { .name = P2P_WHEEL, .value = (float)wheel_action };
          if (wheel_enqueue(&wheel_command) != pdPASS) { uart2_write("[WHEEL] Cola llena\r\n"); }
          continue;
        }
      }

      AdcsCommand_t command;
      if (!parse_adcs_command(line, &command)) {
        uart2_write("[COMMAND] comando invalido; use ADCS HELP\r\n");
      } else if (xQueueSend(adcsCommandQueueHandle, &command, pdMS_TO_TICKS(20U)) != pdPASS) {
        uart2_write("[COMMAND] cola ADCS llena\r\n");
      }
    } else if ((byte == '\b') || (byte == 0x7FU)) {
      if (length > 0U) length--;
    } else if ((byte >= 32U) && (byte <= 126U)) {
      if (length < (sizeof(line) - 1U)) {
        line[length++] = (char)byte;
      } else {
        length = 0U;
        uart2_write("[COMMAND] linea demasiado larga\r\n");
      }
    }
  }
  /* USER CODE END 5 */
}

/**
  * @brief  Period elapsed callback in non blocking mode
  * @note   This function is called  when TIM6 interrupt took place, inside
  * HAL_TIM_IRQHandler(). It makes a direct call to HAL_IncTick() to increment
  * a global variable "uwTick" used as application time base.
  * @param  htim : TIM handle
  * @retval None
  */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  /* USER CODE BEGIN Callback 0 */

  /* USER CODE END Callback 0 */
  if (htim->Instance == TIM6)
  {
    HAL_IncTick();
  }
  /* USER CODE BEGIN Callback 1 */

  /* USER CODE END Callback 1 */
}

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
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
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
