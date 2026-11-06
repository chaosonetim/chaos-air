#include "main.h"
#include "camera_snapshot.h"
#include "imx335.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#ifdef CAMERA_AUTO_ISP
#include "isp_api.h"
#include "isp_services.h"
#include "imx335_isp_param_conf.h"
#endif

#define FRAME_WIDTH 640U
#define FRAME_HEIGHT 480U
#define FRAME_BYTES (FRAME_WIDTH * FRAME_HEIGHT * 2U)
#define GUARD_BYTES 32U
#define SENSOR_ADDRESS (0x1AU << 1)
#ifdef CAMERA_FULL_FRAME
#define CAPTURE_PIPE DCMIPP_PIPE1
_Static_assert(IMX335_WIDTH * FRAME_HEIGHT == IMX335_HEIGHT * FRAME_WIDTH,
               "Full-field resize must preserve the sensor aspect ratio");
_Static_assert((FRAME_WIDTH * 2U) % 16U == 0U, "RGB565 pitch must be 16-byte aligned");
#else
#define CAPTURE_PIPE DCMIPP_PIPE0
#endif

typedef struct
{
  uint8_t before[GUARD_BYTES];
  uint16_t pixels[FRAME_WIDTH * FRAME_HEIGHT];
  uint8_t after[GUARD_BYTES];
} CameraFrame;

__attribute__((section(".camera_frame"), aligned(32))) CameraFrame camera_frame;
volatile uint32_t camera_frame_count;
volatile uint32_t camera_capture_error;
volatile uint32_t camera_capture_complete;
volatile uint32_t camera_captured_bytes;
volatile uint32_t camera_frame_checksum;

static UART_HandleTypeDef uart;
static I2C_HandleTypeDef i2c;
static DCMIPP_HandleTypeDef dcmipp;
static IMX335_Object_t sensor;
#ifdef CAMERA_AUTO_ISP
static ISP_HandleTypeDef isp;
static int32_t sensor_gain;
static int32_t sensor_exposure;
static volatile uint32_t isp_gathering;
static uint32_t retained_frame_count = 1U;
static uint16_t sharpen_rows[3][FRAME_WIDTH];
#endif

static void Log(const char *format, ...)
{
  char text[224];
  va_list args;
  va_start(args, format);
  int length = vsnprintf(text, sizeof(text), format, args);
  va_end(args);
  if ((length < 0) || ((size_t)length >= sizeof(text)) ||
      (HAL_UART_Transmit(&uart, (uint8_t *)text, (uint16_t)length, 1000U) != HAL_OK))
  {
    Error_Handler();
  }
}

static void Require(HAL_StatusTypeDef status, const char *operation)
{
  if (status != HAL_OK)
  {
    Log("ERROR: %s status=%u I2C=%08lX DCMIPP=%08lX\r\n", operation,
        (unsigned int)status, (unsigned long)i2c.ErrorCode, (unsigned long)dcmipp.ErrorCode);
    HAL_GPIO_WritePin(GPIOO, GPIO_PIN_5, GPIO_PIN_RESET);
    __HAL_RCC_DCMIPP_FORCE_RESET();
    camera_capture_error = 1U;
    Error_Handler();
  }
}

#ifdef CAMERA_AUTO_ISP
int __io_putchar(int character)
{
  uint8_t value = (uint8_t)character;
  if (HAL_UART_Transmit(&uart, &value, 1U, 1000U) != HAL_OK)
  {
    Error_Handler();
  }
  return character;
}

static void ISP_Require(ISP_StatusTypeDef status, const char *operation)
{
  if (status != ISP_OK)
  {
    Log("ERROR: ISP %s status=%u\r\n", operation, (unsigned int)status);
    Require(HAL_ERROR, operation);
  }
}

static ISP_StatusTypeDef ISP_SensorInfo(uint32_t instance, ISP_SensorInfoTypeDef *info)
{
  (void)instance;
  strcpy(info->name, IMX335_NAME);
  info->bayer_pattern = IMX335_BAYER_PATTERN;
  info->color_depth = IMX335_COLOR_DEPTH;
  info->width = IMX335_WIDTH;
  info->height = IMX335_HEIGHT;
  info->gain_min = IMX335_GAIN_MIN;
  info->gain_max = IMX335_GAIN_MAX;
  info->again_max = 30000U;
  info->exposure_min = IMX335_EXPOSURE_MIN;
  info->exposure_max = IMX335_EXPOSURE_MAX;
  return ISP_OK;
}

static ISP_StatusTypeDef ISP_SetGain(uint32_t instance, int32_t gain)
{
  (void)instance;
  if (IMX335_SetGain(&sensor, gain) != IMX335_OK)
  {
    Log("ERROR: ISP sensor gain=%ld\r\n", (long)gain);
    return ISP_ERR_SENSORGAIN;
  }
  sensor_gain = gain;
  return ISP_OK;
}

static ISP_StatusTypeDef ISP_GetGain(uint32_t instance, int32_t *gain)
{
  (void)instance;
  *gain = sensor_gain;
  return ISP_OK;
}

static ISP_StatusTypeDef ISP_SetExposure(uint32_t instance, int32_t exposure)
{
  (void)instance;
  if (IMX335_SetExposure(&sensor, exposure) != IMX335_OK)
  {
    Log("ERROR: ISP sensor exposure=%ld\r\n", (long)exposure);
    return ISP_ERR_SENSOREXPOSURE;
  }
  sensor_exposure = exposure;
  return ISP_OK;
}

static ISP_StatusTypeDef ISP_GetExposure(uint32_t instance, int32_t *exposure)
{
  (void)instance;
  *exposure = sensor_exposure;
  return ISP_OK;
}

void HAL_DCMIPP_PIPE_VsyncEventCallback(DCMIPP_HandleTypeDef *handle, uint32_t pipe)
{
  if ((handle == &dcmipp) && (pipe == CAPTURE_PIPE) && isp_gathering)
  {
    ISP_IncMainFrameId(&isp);
    ISP_GatherStatistics(&isp);
  }
}
#endif

static void Control_Init(void)
{
  GPIO_InitTypeDef gpio = {0};
  __HAL_RCC_GPIOE_CLK_ENABLE();
  __HAL_RCC_USART1_CLK_ENABLE();
  gpio.Pin = GPIO_PIN_5 | GPIO_PIN_6;
  gpio.Mode = GPIO_MODE_AF_PP;
  gpio.Alternate = GPIO_AF7_USART1;
  gpio.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOE, &gpio);
  uart.Instance = USART1;
  uart.Init.BaudRate = 115200U;
  uart.Init.WordLength = UART_WORDLENGTH_8B;
  uart.Init.StopBits = UART_STOPBITS_1;
  uart.Init.Mode = UART_MODE_TX_RX;
  uart.Init.OverSampling = UART_OVERSAMPLING_16;
  uart.Init.ClockPrescaler = UART_PRESCALER_DIV1;
  if (HAL_UART_Init(&uart) != HAL_OK)
  {
    Error_Handler();
  }
#ifdef CAMERA_FULL_FRAME
  Log("\r\nAppNS: one IMX335 full-field RGB565 snapshot\r\n");
#else
  Log("\r\nAppNS: one IMX335 RAW10 snapshot\r\n");
#endif

  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOO_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_I2C2_CLK_ENABLE();
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_0, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(GPIOO, GPIO_PIN_5, GPIO_PIN_RESET);
  gpio.Mode = GPIO_MODE_OUTPUT_PP;
  gpio.Pin = GPIO_PIN_0;
  HAL_GPIO_Init(GPIOA, &gpio);
  gpio.Pin = GPIO_PIN_5;
  HAL_GPIO_Init(GPIOO, &gpio);
  gpio.Mode = GPIO_MODE_AF_OD;
  gpio.Alternate = GPIO_AF4_I2C2;
  gpio.Pin = GPIO_PIN_10 | GPIO_PIN_11;
  HAL_GPIO_Init(GPIOB, &gpio);
  i2c.Instance = I2C2;
  /* 64 MHz HSI kernel clock, standard mode, analog filter enabled. */
  i2c.Init.Timing = 0x7092232BU;
  i2c.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  Require(HAL_I2C_Init(&i2c), "I2C2 init");
  Require(HAL_I2CEx_ConfigAnalogFilter(&i2c, I2C_ANALOGFILTER_ENABLE), "I2C analog filter");
  Require(HAL_I2CEx_ConfigDigitalFilter(&i2c, 0), "I2C digital filter");
  HAL_Delay(100U);
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_0, GPIO_PIN_SET);
  HAL_Delay(100U);
  HAL_GPIO_WritePin(GPIOO, GPIO_PIN_5, GPIO_PIN_SET);
  HAL_Delay(100U);
}

static int32_t Bus_Init(void)
{
  return IMX335_OK;
}

static int32_t Bus_Read(uint16_t address, uint16_t reg, uint8_t *data, uint16_t size)
{
  HAL_StatusTypeDef status = HAL_I2C_Mem_Read(&i2c, address, reg, I2C_MEMADD_SIZE_16BIT, data, size, 100U);
  if (status != HAL_OK)
  {
    Log("ERROR: sensor read reg=%04X status=%u error=%08lX\r\n",
        reg, (unsigned int)status, (unsigned long)i2c.ErrorCode);
  }
  return status == HAL_OK ? IMX335_OK : IMX335_ERROR;
}

static int32_t Bus_Write(uint16_t address, uint16_t reg, uint8_t *data, uint16_t size)
{
  HAL_StatusTypeDef status = HAL_I2C_Mem_Write(&i2c, address, reg, I2C_MEMADD_SIZE_16BIT, data, size, 100U);
  if (status != HAL_OK)
  {
    Log("ERROR: sensor write reg=%04X status=%u error=%08lX\r\n",
        reg, (unsigned int)status, (unsigned long)i2c.ErrorCode);
  }
  return status == HAL_OK ? IMX335_OK : IMX335_ERROR;
}

static int32_t Bus_Tick(void)
{
  return (int32_t)HAL_GetTick();
}

static void Sensor_Require(int32_t status, const char *operation)
{
  Require(status == IMX335_OK ? HAL_OK : HAL_ERROR, operation);
}

static void Receiver_Init(void)
{
  DCMIPP_CSI_ConfTypeDef csi = {
    DCMIPP_CSI_TWO_DATA_LANES, DCMIPP_CSI_PHYSICAL_DATA_LANES, DCMIPP_CSI_PHY_BT_1600
  };
  DCMIPP_CSI_PIPE_ConfTypeDef input = {DCMIPP_DTMODE_DTIDA, DCMIPP_DT_RAW10, DCMIPP_DT_RAW10};
  DCMIPP_PipeConfTypeDef pipe = {0};
#ifndef CAMERA_FULL_FRAME
  DCMIPP_CropConfTypeDef crop = {0};
#else
  DCMIPP_RawBayer2RGBConfTypeDef bayer = {
    .VLineStrength = DCMIPP_RAWBAYER_ALGO_STRENGTH_4,
    .HLineStrength = DCMIPP_RAWBAYER_ALGO_STRENGTH_4,
    .RawBayerType = DCMIPP_RAWBAYER_RGGB,
    .PeakStrength = DCMIPP_RAWBAYER_ALGO_STRENGTH_4,
    .EdgeStrength = DCMIPP_RAWBAYER_ALGO_STRENGTH_6
  };
  /* ST's Q13 fractional ratio and reciprocal normalization, with no crop. */
  DCMIPP_DownsizeTypeDef downsize = {
    .HSize = FRAME_WIDTH, .VSize = FRAME_HEIGHT,
    .HRatio = 8192U * IMX335_WIDTH / FRAME_WIDTH,
    .VRatio = 8192U * IMX335_HEIGHT / FRAME_HEIGHT,
    .HDivFactor = (1024U * 8192U - 1U) / (8192U * IMX335_WIDTH / FRAME_WIDTH),
    .VDivFactor = (1024U * 8192U - 1U) / (8192U * IMX335_HEIGHT / FRAME_HEIGHT)
  };
#endif

  __HAL_RCC_DCMIPP_CLK_ENABLE();
  __HAL_RCC_CSI_CLK_ENABLE();
  __HAL_RCC_DCMIPP_FORCE_RESET();
  __HAL_RCC_DCMIPP_RELEASE_RESET();
  __HAL_RCC_CSI_FORCE_RESET();
  __HAL_RCC_CSI_RELEASE_RESET();
  dcmipp.Instance = DCMIPP;
  Require(HAL_DCMIPP_Init(&dcmipp), "DCMIPP init");
  Require(HAL_DCMIPP_CSI_SetConfig(&dcmipp, &csi), "CSI D-PHY");
  Require(HAL_DCMIPP_CSI_SetVCConfig(&dcmipp, DCMIPP_VIRTUAL_CHANNEL0, DCMIPP_CSI_DT_BPP10), "CSI VC0");
  Require(HAL_DCMIPP_CSI_PIPE_SetConfig(&dcmipp, CAPTURE_PIPE, &input), "pipe RAW10 input");
  pipe.FrameRate = DCMIPP_FRAME_RATE_ALL;
#ifdef CAMERA_FULL_FRAME
  pipe.PixelPipePitch = FRAME_WIDTH * 2U;
  pipe.PixelPackerFormat = DCMIPP_PIXEL_PACKER_FORMAT_RGB565_1;
  Require(HAL_DCMIPP_PIPE_SetConfig(&dcmipp, CAPTURE_PIPE, &pipe), "pipe1 RGB565 config");
  Require(HAL_DCMIPP_PIPE_DisableCrop(&dcmipp, CAPTURE_PIPE), "disable crop");
  Require(HAL_DCMIPP_PIPE_SetISPRawBayer2RGBConfig(&dcmipp, CAPTURE_PIPE, &bayer), "RGGB demosaic");
  Require(HAL_DCMIPP_PIPE_EnableISPRawBayer2RGB(&dcmipp, CAPTURE_PIPE), "enable demosaic");
  Require(HAL_DCMIPP_PIPE_SetDownsizeConfig(&dcmipp, CAPTURE_PIPE, &downsize), "full-field resize");
  Require(HAL_DCMIPP_PIPE_EnableDownsize(&dcmipp, CAPTURE_PIPE), "enable resize");
#else
  Require(HAL_DCMIPP_PIPE_SetConfig(&dcmipp, DCMIPP_PIPE0, &pipe), "pipe0 config");
  /* Pipe0 horizontal crop units are 32-bit words: two unpacked RAW10 pixels. */
  crop.HStart = 0;
  crop.VStart = 0;
  crop.HSize = FRAME_WIDTH / 2U;
  crop.VSize = FRAME_HEIGHT;
  crop.PipeArea = DCMIPP_POSITIVE_AREA;
  Require(HAL_DCMIPP_PIPE_SetCropConfig(&dcmipp, DCMIPP_PIPE0, &crop), "pipe0 crop");
  Require(HAL_DCMIPP_PIPE_EnableCrop(&dcmipp, DCMIPP_PIPE0), "enable crop");
  Require(HAL_DCMIPP_PIPE_EnableLimitEvent(&dcmipp, DCMIPP_PIPE0, FRAME_BYTES / 4U + 1U), "DMA bound");
#endif
  HAL_NVIC_SetPriority(DCMIPP_IRQn, 5U, 0U);
  HAL_NVIC_SetPriority(CSI_IRQn, 5U, 0U);
  HAL_NVIC_EnableIRQ(DCMIPP_IRQn);
  HAL_NVIC_EnableIRQ(CSI_IRQn);
}

void DCMIPP_IRQHandler(void)
{
  HAL_DCMIPP_IRQHandler(&dcmipp);
}

void CSI_IRQHandler(void)
{
  HAL_DCMIPP_CSI_IRQHandler(&dcmipp);
}

void HAL_DCMIPP_PIPE_FrameEventCallback(DCMIPP_HandleTypeDef *handle, uint32_t pipe)
{
  if ((handle == &dcmipp) && (pipe == CAPTURE_PIPE))
  {
    ++camera_frame_count;
  }
}

void HAL_DCMIPP_ErrorCallback(DCMIPP_HandleTypeDef *handle)
{
  camera_capture_error = handle->ErrorCode | 0x80000000U;
}

void HAL_DCMIPP_PIPE_ErrorCallback(DCMIPP_HandleTypeDef *handle, uint32_t pipe)
{
  (void)pipe;
  camera_capture_error = handle->ErrorCode | 0x80000000U;
}

void HAL_DCMIPP_PIPE_LimitEventCallback(DCMIPP_HandleTypeDef *handle, uint32_t pipe)
{
  (void)handle;
  (void)pipe;
  camera_capture_error = HAL_DCMIPP_ERROR_PIPE0_LIMIT;
}

static uint32_t Checksum(void)
{
  const volatile uint8_t *data = (const volatile uint8_t *)camera_frame.pixels;
  uint32_t hash = 2166136261U;
  for (uint32_t i = 0; i < FRAME_BYTES; ++i)
  {
    hash = (hash ^ data[i]) * 16777619U;
  }
  return hash;
}

#ifdef CAMERA_AUTO_ISP
static int32_t Pixel_Luma(uint16_t pixel)
{
  int32_t red = ((pixel >> 11) & 31U) * 255U / 31U;
  int32_t green = ((pixel >> 5) & 63U) * 255U / 63U;
  int32_t blue = (pixel & 31U) * 255U / 31U;
  return (77 * red + 150 * green + 29 * blue) / 256;
}

static uint32_t Sharpen_Channel(uint32_t value, uint32_t maximum, int32_t delta)
{
  int32_t channel = (int32_t)(value * 255U / maximum) + delta;
  if (channel < 0) { channel = 0; }
  if (channel > 255) { channel = 255; }
  return ((uint32_t)channel * maximum + 127U) / 255U;
}

static void Sharpen_Frame(void)
{
  memcpy(sharpen_rows[0], camera_frame.pixels, sizeof(sharpen_rows[0]));
  memcpy(sharpen_rows[1], camera_frame.pixels + FRAME_WIDTH, sizeof(sharpen_rows[1]));
  for (uint32_t y = 1U; y < FRAME_HEIGHT - 1U; ++y)
  {
    uint16_t *above = sharpen_rows[(y - 1U) % 3U];
    uint16_t *center = sharpen_rows[y % 3U];
    uint16_t *below = sharpen_rows[(y + 1U) % 3U];
    /* Preserve original neighbors before replacing this row in place. */
    memcpy(below, camera_frame.pixels + (y + 1U) * FRAME_WIDTH, sizeof(sharpen_rows[0]));
    for (uint32_t x = 1U; x < FRAME_WIDTH - 1U; ++x)
    {
      int32_t luma = Pixel_Luma(center[x]);
      int32_t blur = (Pixel_Luma(above[x - 1U]) + 2 * Pixel_Luma(above[x]) +
                      Pixel_Luma(above[x + 1U]) + 2 * Pixel_Luma(center[x - 1U]) +
                      4 * luma + 2 * Pixel_Luma(center[x + 1U]) +
                      Pixel_Luma(below[x - 1U]) + 2 * Pixel_Luma(below[x]) +
                      Pixel_Luma(below[x + 1U]) + 8) / 16;
      int32_t detail = luma - blur;
      if ((detail > -4) && (detail < 4)) { continue; }
      int32_t delta = detail;
      if (delta < -16) { delta = -16; }
      if (delta > 16) { delta = 16; }
      uint16_t pixel = center[x];
      uint32_t red = Sharpen_Channel((pixel >> 11) & 31U, 31U, delta);
      uint32_t green = Sharpen_Channel((pixel >> 5) & 63U, 63U, delta);
      uint32_t blue = Sharpen_Channel(pixel & 31U, 31U, delta);
      camera_frame.pixels[y * FRAME_WIDTH + x] = (uint16_t)((red << 11) | (green << 5) | blue);
    }
  }
}
#endif

void Camera_Snapshot(void)
{
  IMX335_IO_t io = {Bus_Init, Bus_Init, SENSOR_ADDRESS, Bus_Write, Bus_Read, Bus_Tick};
  uint32_t id = 0xFFFFFFFFU;
  uint8_t standby = IMX335_MODE_STANDBY;
  uint8_t stream = IMX335_MODE_STREAMING;
#ifndef CAMERA_FULL_FRAME
  uint32_t bytes = 0;
#endif

  /* Keep DMA and CPU views coherent without introducing a cacheable alias. */
  if ((SCB->CCR & SCB_CCR_DC_Msk) != 0U)
  {
    SCB_DisableDCache();
  }
  Control_Init();
  Sensor_Require(IMX335_RegisterBusIO(&sensor, &io), "sensor bus");
  Sensor_Require(IMX335_ReadID(&sensor, &id), "sensor ID");
  Log("IMX335 ID=%02lX\r\n", (unsigned long)id);
  Require(id == IMX335_CHIP_ID ? HAL_OK : HAL_ERROR, "sensor ID match");
#ifdef CAMERA_FULL_FRAME
  Sensor_Require(IMX335_InitStandby(&sensor, IMX335_R2592_1944, IMX335_RAW_RGGB10), "sensor mode in standby");
#else
  Sensor_Require(IMX335_Init(&sensor, IMX335_R2592_1944, IMX335_RAW_RGGB10), "sensor mode");
#endif
  Sensor_Require(Bus_Write(SENSOR_ADDRESS, IMX335_REG_MODE_SELECT, &standby, 1), "sensor standby");
  Sensor_Require(IMX335_SetFrequency(&sensor, IMX335_INCK_24MHZ), "sensor 24 MHz");
#ifdef CAMERA_FULL_FRAME
  Sensor_Require(IMX335_SetFramerate(&sensor, 30), "sensor 30 fps");
  Sensor_Require(IMX335_SetGain(&sensor, 0), "sensor fixed 0 dB gain");
  Sensor_Require(IMX335_SetExposure(&sensor, 20000), "sensor fixed 20 ms exposure");
#ifdef CAMERA_AUTO_ISP
  sensor_gain = 0;
  sensor_exposure = 20000;
#else
  Log("Sensor: fixed 30 fps, exposure=20000 us, gain=0 mdB\r\n");
#endif
#endif
  HAL_Delay(30U);
  Receiver_Init();
#ifdef CAMERA_AUTO_ISP
  ISP_AppliHelpersTypeDef helpers = {
    .GetSensorInfo = ISP_SensorInfo,
    .SetSensorGain = ISP_SetGain, .GetSensorGain = ISP_GetGain,
    .SetSensorExposure = ISP_SetExposure, .GetSensorExposure = ISP_GetExposure
  };
  ISP_Require(ISP_Init(&isp, &dcmipp, 0U, &helpers, &ISP_IQParamCacheInit_IMX335), "init");
  ISP_Require(ISP_Start(&isp), "start");
  Require(HAL_DCMIPP_PIPE_IsEnabledGammaConversion(&dcmipp, CAPTURE_PIPE) ?
          HAL_OK : HAL_ERROR, "ISP gamma enabled");
#endif
  memset(&camera_frame, 0xA5, sizeof(camera_frame));
  __DSB();
#ifdef CAMERA_AUTO_ISP
  Log("Starting ISP warm-up: full 2592x1944 -> 640x480 RGB565, address=%08lX bytes=%lu\r\n",
#elif defined(CAMERA_FULL_FRAME)
  Log("Arming one frame: full 2592x1944 -> 640x480 RGB565, address=%08lX bytes=%lu\r\n",
#else
  Log("Arming one frame: 640x480, RAW10 in uint16, address=%08lX bytes=%lu\r\n",
#endif
      (unsigned long)(uintptr_t)camera_frame.pixels, (unsigned long)FRAME_BYTES);
  Require(HAL_DCMIPP_CSI_PIPE_Start(&dcmipp, CAPTURE_PIPE, DCMIPP_VIRTUAL_CHANNEL0,
                                   (uint32_t)(uintptr_t)camera_frame.pixels,
#ifdef CAMERA_AUTO_ISP
                                   DCMIPP_MODE_CONTINUOUS
#else
                                   DCMIPP_MODE_SNAPSHOT
#endif
                                   ), "capture start");
#ifdef CAMERA_AUTO_ISP
  isp_gathering = 1U;
#endif
  Sensor_Require(Bus_Write(SENSOR_ADDRESS, IMX335_REG_MODE_SELECT, &stream, 1), "sensor stream");
  uint32_t start = HAL_GetTick();
#ifdef CAMERA_AUTO_ISP
  Log("ISP: auto-exposure/gain, AWB, gamma; 90 warm-up frames, 10 s timeout\r\n");
  while ((camera_frame_count < 90U) && (camera_capture_error == 0U) &&
         ((HAL_GetTick() - start) < 10000U))
  {
    ISP_Require(ISP_BackgroundProcess(&isp), "background");
  }
  Require((camera_frame_count >= 90U && camera_capture_error == 0U) ?
          HAL_OK : HAL_ERROR, "ISP warm-up");
  isp_gathering = 0U;
  uint32_t frame = camera_frame_count;
  /* Freeze controls and allow their sensor/ISP latency to elapse. */
  start = HAL_GetTick();
  while ((camera_frame_count < frame + 5U) && (camera_capture_error == 0U) &&
         ((HAL_GetTick() - start) < 3000U))
  {
    __NOP();
  }
  Require((camera_frame_count >= frame + 5U && camera_capture_error == 0U) ?
          HAL_OK : HAL_ERROR, "ISP latency frames");
  frame = camera_frame_count;
  Require(HAL_DCMIPP_PIPE_SetCaptureMode(&dcmipp, CAPTURE_PIPE, DCMIPP_MODE_SNAPSHOT), "retain final frame");
  start = HAL_GetTick();
  while (((camera_frame_count == frame) ||
          ((dcmipp.Instance->P1FCTCR & DCMIPP_P1FCTCR_CPTREQ) != 0U)) &&
         (camera_capture_error == 0U) &&
         ((HAL_GetTick() - start) < 3000U))
  {
    __NOP();
  }
  Require((camera_frame_count > frame && camera_capture_error == 0U &&
           (dcmipp.Instance->P1FCTCR & DCMIPP_P1FCTCR_CPTREQ) == 0U) ?
          HAL_OK : HAL_ERROR, "final ISP frame");
  retained_frame_count = camera_frame_count;
  ISP_ISPGainTypeDef white_balance;
  ISP_Require(ISP_SVC_ISP_GetGain(&isp, &white_balance), "read WB gains");
  Log("ISP: WB gains R=%lu G=%lu B=%lu (x1=100000000), gamma enabled\r\n",
      (unsigned long)white_balance.ispGainR, (unsigned long)white_balance.ispGainG,
      (unsigned long)white_balance.ispGainB);
  Log("ISP: exposure=%ld us gain=%ld mdB, warm-up plus final frames=%lu\r\n",
      (long)sensor_exposure, (long)sensor_gain, (unsigned long)retained_frame_count);
#else
  while ((camera_frame_count == 0U) && (camera_capture_error == 0U) &&
         ((HAL_GetTick() - start) < 3000U))
  {
    __NOP();
  }
#endif
  Sensor_Require(Bus_Write(SENSOR_ADDRESS, IMX335_REG_MODE_SELECT, &standby, 1), "stop sensor");
#ifndef CAMERA_FULL_FRAME
  /* The dump counter reports bytes, advancing in 32-bit granularity for RAW10. */
  Require(HAL_DCMIPP_PIPE_GetDataCounter(&dcmipp, DCMIPP_PIPE0, &bytes), "DMA count");
  camera_captured_bytes = bytes;
#endif
  Require(HAL_DCMIPP_CSI_PIPE_Stop(&dcmipp, CAPTURE_PIPE, DCMIPP_VIRTUAL_CHANNEL0), "stop receiver");
  __DSB();
#ifdef CAMERA_FULL_FRAME
  /* Pipe1 has no dump byte counter; do not present configured size as measured. */
  Log("Capture: frames=%lu payload=%lu errors=%08lX (pipe1, no byte counter)\r\n",
      (unsigned long)camera_frame_count, (unsigned long)FRAME_BYTES,
      (unsigned long)camera_capture_error);
  Require((camera_frame_count ==
#ifdef CAMERA_AUTO_ISP
           retained_frame_count
#else
           1U
#endif
           && camera_capture_error == 0U) ?
          HAL_OK : HAL_ERROR, "exactly one RGB frame");
#else
  Log("Capture: frames=%lu bytes=%lu errors=%08lX\r\n", (unsigned long)camera_frame_count,
      (unsigned long)camera_captured_bytes, (unsigned long)camera_capture_error);
  Require((camera_frame_count == 1U && camera_capture_error == 0U &&
           camera_captured_bytes == FRAME_BYTES) ? HAL_OK : HAL_ERROR, "exactly one complete frame");
#endif
#ifdef CAMERA_AUTO_ISP
  uint32_t unsharpened_checksum = Checksum();
#ifdef CAMERA_COMPARE_SHARPENING
  Log("COMPARE: unsharpened frame ready, checksum=%08lX; send S within 60 s\r\n",
      (unsigned long)unsharpened_checksum);
  uint8_t command = 0U;
  Require(HAL_UART_Receive(&uart, &command, 1U, 60000U), "comparison command");
  Require(command == 'S' ? HAL_OK : HAL_ERROR, "comparison command must be S");
  Require(Checksum() == unsharpened_checksum ? HAL_OK : HAL_ERROR, "comparison input stable");
#endif
  Sharpen_Frame();
  Log("Sharpen: software luma unsharp, Gaussian 3x3, amount=100%% threshold=4 limit=16, input checksum=%08lX\r\n",
      (unsigned long)unsharpened_checksum);
#endif
  for (uint32_t i = 0; i < GUARD_BYTES; ++i)
  {
    Require((camera_frame.before[i] == 0xA5U && camera_frame.after[i] == 0xA5U) ?
            HAL_OK : HAL_ERROR, "frame guards");
  }
#ifndef CAMERA_FULL_FRAME
  uint16_t minimum = 1023U;
  uint16_t maximum = 0U;
  for (uint32_t i = 0; i < FRAME_WIDTH * FRAME_HEIGHT; ++i)
  {
    uint16_t pixel = camera_frame.pixels[i];
    Require(pixel <= 1023U ? HAL_OK : HAL_ERROR, "RAW10 sample range");
    if (pixel < minimum) { minimum = pixel; }
    if (pixel > maximum) { maximum = pixel; }
  }
  Log("RAW10 samples: min=%u max=%u, guards intact\r\n", minimum, maximum);
#else
  Log("RGB565: 640x480 pitch=1280, guards intact, no crop\r\n");
#endif
  camera_frame_checksum = Checksum();
  HAL_Delay(250U);
  Require((camera_frame_count ==
#ifdef CAMERA_AUTO_ISP
           retained_frame_count
#else
           1U
#endif
           && Checksum() == camera_frame_checksum) ?
          HAL_OK : HAL_ERROR, "snapshot remains unchanged");
  camera_capture_complete = 1U;
  Log("DONE: one frame retained, checksum=%08lX, sensor standby, no rearm\r\n",
      (unsigned long)camera_frame_checksum);
}
