 /**
 ******************************************************************************
 * @file    main.c
 * @author  GPM Application Team
 *
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2023 STMicroelectronics.
 * All rights reserved.
 *
 * This software is licensed under terms that can be found in the LICENSE file
 * in the root directory of this software component.
 * If no LICENSE file comes with this software, it is provided AS-IS.
 *
 ******************************************************************************
 */
#include <math.h>
#include <string.h>
#include <unistd.h>

#include "cmw_camera.h"
#include "scrl.h"
#include "stm32n6xx_nucleo_bus.h"
#include "stm32n6xx_nucleo_xspi.h"
#include "stm32n6xx_nucleo.h"
#include "stm32_lcd.h"
#include "app_fuseprogramming.h"
#include "stm32_lcd_ex.h"
#include "app_postprocess.h"
#include "stai.h"
#include "stai_ext.h"
#include "stai_network.h"
#include "app_camerapipeline.h"
#include "main.h"
#include <stdio.h>
#include "app_config.h"
#include "crop_img.h"
#include "stlogo.h"

CLASSES_TABLE;

#define LCD_BG_WIDTH  SCREEN_WIDTH
#define LCD_BG_HEIGHT SCREEN_HEIGHT
#define LCD_FG_WIDTH  SCREEN_WIDTH
#define LCD_FG_HEIGHT SCREEN_HEIGHT

#define LCD_FG_FRAMEBUFFER_SIZE  (LCD_FG_WIDTH * LCD_FG_HEIGHT * 2)

#define UTIL_LCD_COLOR_TRANSPARENT 0

#ifndef APP_GIT_SHA1_STRING
#define APP_GIT_SHA1_STRING "dev"
#endif
#ifndef APP_VERSION_STRING
#define APP_VERSION_STRING "unversioned"
#endif


typedef struct
{
  uint32_t X0;
  uint32_t Y0;
  uint32_t XSize;
  uint32_t YSize;
} Rectangle_TypeDef;

/* Lcd Background area */
Rectangle_TypeDef lcd_bg_area = {
#if ASPECT_RATIO_MODE == ASPECT_RATIO_CROP || ASPECT_RATIO_MODE == ASPECT_RATIO_FIT
  .X0 = (LCD_BG_WIDTH - LCD_BG_HEIGHT) / 2,
#else
  .X0 = 0,
#endif
  .Y0 = 0,
  .XSize = 0,
  .YSize = 0,
};

/* Lcd Foreground area */
Rectangle_TypeDef lcd_fg_area = {
#if ASPECT_RATIO_MODE == ASPECT_RATIO_CROP || ASPECT_RATIO_MODE == ASPECT_RATIO_FIT
  .X0 = (LCD_FG_WIDTH - LCD_FG_HEIGHT) / 2,
#else
  .X0 = 0,
#endif
  .Y0 = 0,
  .XSize = 0,
  .YSize = 0,
};

#define NUMBER_COLORS 10
const uint32_t colors[NUMBER_COLORS] = {
    UTIL_LCD_COLOR_GREEN,
    UTIL_LCD_COLOR_RED,
    UTIL_LCD_COLOR_CYAN,
    UTIL_LCD_COLOR_MAGENTA,
    UTIL_LCD_COLOR_YELLOW,
    UTIL_LCD_COLOR_GRAY,
    UTIL_LCD_COLOR_BLACK,
    UTIL_LCD_COLOR_BROWN,
    UTIL_LCD_COLOR_BLUE,
    UTIL_LCD_COLOR_ORANGE
};

#if POSTPROCESS_TYPE == POSTPROCESS_OD_YOLO_V2_UI
  od_yolov2_pp_static_param_t pp_params;
#elif POSTPROCESS_TYPE == POSTPROCESS_OD_YOLO_V5_UU
  od_yolov5_pp_static_param_t pp_params;
#elif POSTPROCESS_TYPE == POSTPROCESS_OD_YOLO_V8_UI
  od_yolov8_pp_static_param_t pp_params;
#elif POSTPROCESS_TYPE == POSTPROCESS_OD_ST_YOLOX_UI
  od_st_yolox_pp_static_param_t pp_params;
#elif POSTPROCESS_TYPE == POSTPROCESS_OD_SSD_UI
  od_ssd_pp_static_param_t pp_params;
#elif POSTPROCESS_TYPE == POSTPROCESS_OD_ST_YOLOD_UI
  od_yolo_d_pp_static_param_t pp_params;
#elif POSTPROCESS_TYPE == POSTPROCESS_OD_BLAZEFACE_UI
  od_blazeface_pp_static_param_t pp_params;
#elif POSTPROCESS_TYPE == POSTPROCESS_CUSTOM
  od_obb_custom_pp_static_param_t pp_params;
#else
  #error "PostProcessing type not supported"
#endif

UART_HandleTypeDef huart1;
volatile int32_t cameraFrameReceived;
volatile uint32_t g_app_trace_stage = 0;
volatile uint32_t g_app_trace_frame_index = 0;
stai_ptr nn_in;
void* pp_input;
od_pp_out_t pp_output;
static float nn_input_scale = 0.0f;
static int32_t nn_input_zero_point = 0;
static uint32_t g_nn_in_len = 0;
static stai_size g_number_output = 0;

enum
{
  APP_TRACE_STAGE_BOOT = 1,
  APP_TRACE_STAGE_NN_INIT,
  APP_TRACE_STAGE_POSTPROCESS_INIT,
  APP_TRACE_STAGE_CAMERA_INIT,
  APP_TRACE_STAGE_DISPLAY_INIT,
  APP_TRACE_STAGE_DISPLAY_PIPE_START,
  APP_TRACE_STAGE_WAITING_FOR_FRAME,
  APP_TRACE_STAGE_PREPROCESS,
  APP_TRACE_STAGE_INFERENCE_START,
  APP_TRACE_STAGE_INFERENCE_POLL,
  APP_TRACE_STAGE_POSTPROCESS,
  APP_TRACE_STAGE_DISPLAY
};

#define ALIGN_TO_16(value) (((value) + 15) & ~15)

/* When NN input dimensions are not a multiple of 16, the DCMIPP output needs cropping */
#if (STAI_NETWORK_IN_1_WIDTH * STAI_NETWORK_IN_1_CHANNEL) != ALIGN_TO_16(STAI_NETWORK_IN_1_WIDTH * STAI_NETWORK_IN_1_CHANNEL)
#define DCMIPP_NN_NEEDS_CROP 1
#else
#define DCMIPP_NN_NEEDS_CROP 0
#endif

#define NN_INPUT_NEEDS_PREPROC DCMIPP_NN_NEEDS_CROP

#if NN_INPUT_NEEDS_PREPROC
#define DCMIPP_OUT_NN_LEN (ALIGN_TO_16(STAI_NETWORK_IN_1_WIDTH * STAI_NETWORK_IN_1_CHANNEL) * STAI_NETWORK_IN_1_HEIGHT)
#define DCMIPP_OUT_NN_BUFF_LEN (DCMIPP_OUT_NN_LEN + 32 - DCMIPP_OUT_NN_LEN%32)

__attribute__ ((aligned (32)))
static uint8_t dcmipp_out_nn[DCMIPP_OUT_NN_BUFF_LEN];
#endif

/* model */
STAI_NETWORK_CONTEXT_DECLARE(network_context, STAI_NETWORK_CONTEXT_SIZE)
/* Lcd Background Buffer */
__attribute__ ((aligned (32)))
static uint8_t lcd_bg_buffer[LCD_BG_WIDTH * LCD_BG_HEIGHT * 2];
/* Lcd Foreground Buffer */
__attribute__ ((aligned (32)))
static uint8_t lcd_fg_buffer[2][LCD_FG_WIDTH * LCD_FG_HEIGHT * 2];
static int lcd_fg_buffer_rd_idx;
/* screen buffer */
__attribute__ ((aligned (32)))
static uint8_t screen_buffer[LCD_FG_WIDTH * LCD_FG_HEIGHT * 2];

static void SystemClock_Config(void);
static void CONSOLE_Config(void);
static void NPURam_enable(void);
static void NPUCache_config(void);
static void Display_NetworkOutput(od_pp_out_t *p_postprocess, uint32_t inference_ms);
static void Display_init(void);
static void LogCameraPipelineState(const char *tag);
static void LogInferenceRuntimeState(const char *tag);
static void Security_Config(void);
static void set_clk_sleep_mode(void);
static void IAC_Config(void);
static void Display_WelcomeScreen(void);
static uint32_t Display_GetBoxColor(uint32_t class_index);
static void Hardware_init(void);
static void NeuralNetwork_init(uint32_t *nn_in_length, stai_ptr *nn_out, stai_size *number_output, int32_t nn_out_len[]);
static stai_return_code RunInferenceWithTracing(uint32_t frame_index);
static uint32_t SampleBytesChecksum32(const uint8_t *data, uint32_t len);
#if NN_INPUT_NEEDS_PREPROC
static void PreprocessCameraFrameToNNInput(const uint8_t *src, uint8_t *dst, uint32_t src_stride);
#endif

static uint32_t SampleBytesChecksum32(const uint8_t *data, uint32_t len)
{
  uint32_t sum = 0;

  for (uint32_t i = 0; i < len; i++)
  {
    sum = (sum * 131U) + data[i];
  }

  return sum;
}

#if NN_INPUT_NEEDS_PREPROC
static void PreprocessCameraFrameToNNInput(const uint8_t *src, uint8_t *dst, uint32_t src_stride)
{
  const uint32_t row_bytes = STAI_NETWORK_IN_1_WIDTH * STAI_NETWORK_IN_1_CHANNEL;

#if APP_MODEL_PROFILE == APP_MODEL_PROFILE_SAFAL_OBB
  if (STAI_NETWORK_IN_1_FORMAT == STAI_FORMAT_U8)
  {
    for (uint32_t y = 0; y < STAI_NETWORK_IN_1_HEIGHT; y++)
    {
      memcpy(dst + (y * row_bytes), src + (y * src_stride), row_bytes);
    }
    return;
  }

  int8_t *dst_i8 = (int8_t *)dst;
  float scaled_full_range = nn_input_scale * 255.0f;
  for (uint32_t y = 0; y < STAI_NETWORK_IN_1_HEIGHT; y++)
  {
    const uint8_t *src_row = src + (y * src_stride);
    int8_t *dst_row = dst_i8 + (y * row_bytes);

    if ((nn_input_zero_point == -128) && (scaled_full_range > 0.999f) && (scaled_full_range < 1.001f))
    {
      for (uint32_t x = 0; x < row_bytes; x++)
      {
        dst_row[x] = (int8_t)((int32_t)src_row[x] - 128);
      }
    }
    else
    {
      for (uint32_t x = 0; x < row_bytes; x++)
      {
        float normalized = (float)src_row[x] / 255.0f;
        int32_t quantized = (int32_t)lroundf(normalized / nn_input_scale) + nn_input_zero_point;

        if (quantized < -128)
        {
          quantized = -128;
        }
        else if (quantized > 127)
        {
          quantized = 127;
        }

        dst_row[x] = (int8_t)quantized;
      }
    }
  }
#else
  for (uint32_t y = 0; y < STAI_NETWORK_IN_1_HEIGHT; y++)
  {
    memcpy(dst + (y * row_bytes), src + (y * src_stride), row_bytes);
  }
#endif
}
#endif

static void LogCameraPipelineState(const char *tag)
{
  DCMIPP_HandleTypeDef *hcamera_dcmipp = CMW_CAMERA_GetDCMIPPHandle();

  printf("TRACE: %s dcmipp_state=%lu pipe1_state=%lu pipe2_state=%lu err=0x%08lX cmsr1=0x%08lX cmsr2=0x%08lX cmier=0x%08lX\n",
         tag,
         (unsigned long) HAL_DCMIPP_GetState(hcamera_dcmipp),
         (unsigned long) HAL_DCMIPP_PIPE_GetState(hcamera_dcmipp, DCMIPP_PIPE1),
         (unsigned long) HAL_DCMIPP_PIPE_GetState(hcamera_dcmipp, DCMIPP_PIPE2),
         (unsigned long) HAL_DCMIPP_GetError(hcamera_dcmipp),
         (unsigned long) hcamera_dcmipp->Instance->CMSR1,
         (unsigned long) hcamera_dcmipp->Instance->CMSR2,
         (unsigned long) hcamera_dcmipp->Instance->CMIER);
}

static void LogInferenceRuntimeState(const char *tag)
{
  stai_return_code first_error = stai_network_get_error(network_context);
  stai_return_code run_status = stai_ext_network_get_nn_run_status(network_context);

  printf("TRACE: %s stage=%lu frame=%lu tick=%lu nn_in=%p nn_in_len=%lu first_error=0x%06lX run_status=0x%06lX\n",
         tag,
         (unsigned long) g_app_trace_stage,
         (unsigned long) g_app_trace_frame_index,
         (unsigned long) HAL_GetTick(),
         nn_in,
         (unsigned long) g_nn_in_len,
         (unsigned long) first_error,
         (unsigned long) run_status);
}

static stai_return_code RunInferenceWithTracing(uint32_t frame_index)
{
  uint32_t inference_start = HAL_GetTick();
  uint32_t last_trace_ms = 0;
  uint32_t iter = 0;
  stai_return_code ret;

  g_app_trace_stage = APP_TRACE_STAGE_INFERENCE_START;
  ret = stai_network_run(network_context, STAI_MODE_ASYNC);
  printf("TRACE: main loop: frame=%lu stai_network_run async ret=0x%06lX first_error=0x%06lX\n",
         (unsigned long) frame_index,
         (unsigned long) ret,
         (unsigned long) stai_network_get_error(network_context));
  if (ret >= STAI_ERROR_GENERIC)
  {
    return ret;
  }

  while (1)
  {
    stai_return_code status = stai_ext_network_get_nn_run_status(network_context);
    uint32_t elapsed_ms = HAL_GetTick() - inference_start;

    if ((iter < 8U) || ((elapsed_ms - last_trace_ms) >= 100U))
    {
      printf("TRACE: main loop: frame=%lu inference poll=%lu elapsed_ms=%lu status=0x%06lX first_error=0x%06lX\n",
             (unsigned long) frame_index,
             (unsigned long) iter,
             (unsigned long) elapsed_ms,
             (unsigned long) status,
             (unsigned long) stai_network_get_error(network_context));
      last_trace_ms = elapsed_ms;
    }

    if (status == STAI_DONE)
    {
      stai_return_code reset_ret = stai_ext_network_new_inference(network_context);
      printf("TRACE: main loop: frame=%lu inference done elapsed_ms=%lu reset_ret=0x%06lX\n",
             (unsigned long) frame_index,
             (unsigned long) elapsed_ms,
             (unsigned long) reset_ret);
      return reset_ret;
    }

    if (status >= STAI_ERROR_GENERIC)
    {
      printf("ERROR: main loop: frame=%lu inference status error=0x%06lX elapsed_ms=%lu\n",
             (unsigned long) frame_index,
             (unsigned long) status,
             (unsigned long) elapsed_ms);
      return status;
    }

    if (elapsed_ms > 3000U)
    {
      printf("ERROR: main loop: frame=%lu inference timeout elapsed_ms=%lu status=0x%06lX first_error=0x%06lX\n",
             (unsigned long) frame_index,
             (unsigned long) elapsed_ms,
             (unsigned long) status,
             (unsigned long) stai_network_get_error(network_context));
      LogCameraPipelineState("main: inference-timeout");
      return STAI_ERROR_NETWORK_INVALID_RUNTIME;
    }

    g_app_trace_stage = APP_TRACE_STAGE_INFERENCE_POLL;
    if (status == STAI_RUNNING_WFE)
    {
      stai_ext_wfe();
    }

    ret = stai_ext_network_run_continue(network_context);
    if (ret >= STAI_ERROR_GENERIC)
    {
      printf("ERROR: main loop: frame=%lu inference continue error=0x%06lX elapsed_ms=%lu\n",
             (unsigned long) frame_index,
             (unsigned long) ret,
             (unsigned long) elapsed_ms);
      return ret;
    }
    iter++;
  }
}


/**
  * @brief  Main program
  * @param  None
  * @retval None
  */
int main(void)
{
  g_app_trace_stage = APP_TRACE_STAGE_BOOT;
  Hardware_init();
  printf("TRACE: main: Hardware_init complete\n");

  /*** NN Init ****************************************************************/
  uint32_t nn_in_len = 0;
  stai_size number_output = 0;
  stai_ptr nn_out[STAI_NETWORK_OUT_NUM] = {0};
  int32_t nn_out_len[STAI_NETWORK_OUT_NUM] = {0};

  g_app_trace_stage = APP_TRACE_STAGE_NN_INIT;
  printf("TRACE: main: NeuralNetwork_init begin\n");
  NeuralNetwork_init(&nn_in_len, nn_out, &number_output, nn_out_len);
  printf("TRACE: main: NeuralNetwork_init OK input_len=%lu outputs=%lu\n",
         (unsigned long) nn_in_len, (unsigned long) number_output);

  /*** Post Processing Init ***************************************************/
  stai_network_info info;
  int ret;

  g_app_trace_stage = APP_TRACE_STAGE_POSTPROCESS_INIT;
  printf("TRACE: main: postprocess init begin\n");
  ret = stai_network_get_info(network_context, &info);
  printf("TRACE: main: stai_network_get_info ret=%d\n", ret);
  assert(ret == STAI_SUCCESS);
  app_postprocess_init(&pp_params, &info);
  printf("TRACE: main: postprocess init OK\n");

  /*** Camera Init ************************************************************/
  uint32_t pitch_nn = 0;
  g_app_trace_stage = APP_TRACE_STAGE_CAMERA_INIT;
  printf("TRACE: main: CameraPipeline_Init begin\n");
  CameraPipeline_Init((uint32_t *[2]) {&lcd_bg_area.XSize, &lcd_fg_area.XSize}, (uint32_t *[2]) {&lcd_bg_area.YSize, &lcd_fg_area.YSize}, &pitch_nn);
  printf("TRACE: main: CameraPipeline_Init OK bg=%lux%lu fg=%lux%lu pitch_nn=%lu\n",
         (unsigned long) lcd_bg_area.XSize, (unsigned long) lcd_bg_area.YSize,
         (unsigned long) lcd_fg_area.XSize, (unsigned long) lcd_fg_area.YSize,
         (unsigned long) pitch_nn);

  g_app_trace_stage = APP_TRACE_STAGE_DISPLAY_INIT;
  printf("TRACE: main: Display_init begin\n");
  Display_init();
  printf("TRACE: main: Display_init OK; USB/UVC should be initialized now\n");

  /* Start LCD Display camera pipe stream */
  g_app_trace_stage = APP_TRACE_STAGE_DISPLAY_PIPE_START;
  printf("TRACE: main: CameraPipeline_DisplayPipe_Start begin\n");
  CameraPipeline_DisplayPipe_Start(lcd_bg_buffer, CMW_MODE_CONTINUOUS);
  printf("TRACE: main: CameraPipeline_DisplayPipe_Start OK\n");

  /*** App header *************************************************************/
  printf("========================================\n");
  printf("STM32N6-GettingStarted-ObjectDetection %s (%s)\n", APP_VERSION_STRING, APP_GIT_SHA1_STRING);
  printf("Build date & time: %s %s\n", __DATE__, __TIME__);
#if defined(__GNUC__)
  printf("Compiler: GCC %d.%d.%d\n", __GNUC__, __GNUC_MINOR__, __GNUC_PATCHLEVEL__);
#elif defined(__ICCARM__)
  printf("Compiler: IAR EWARM %d.%d.%d\n", __VER__ / 1000000, (__VER__ / 1000) % 1000 ,__VER__ % 1000);
#else
  printf("Compiler: Unknown\n");
#endif
  printf("HAL: %lu.%lu.%lu\n", __STM32N6xx_HAL_VERSION_MAIN, __STM32N6xx_HAL_VERSION_SUB1, __STM32N6xx_HAL_VERSION_SUB2);
  printf("STEdgeAI Tools: %d.%d.%d\n", STAI_TOOLS_VERSION_MAJOR, STAI_TOOLS_VERSION_MINOR, STAI_TOOLS_VERSION_MICRO);
  printf("NN model: %s\n", STAI_NETWORK_ORIGIN_MODEL_NAME);
  printf("Model profile: %s\n", APP_MODEL_PROFILE_NAME);
  printf("Calibration: %s\n", APP_MODEL_CALIBRATION_NAME);
  printf("========================================\n");

  /*** App Loop ***************************************************************/
  uint32_t app_loop_trace_count = 0;
  while (1)
  {
    g_app_trace_frame_index = app_loop_trace_count;
    CameraPipeline_IspUpdate();

#if NN_INPUT_NEEDS_PREPROC
    /* Start NN camera single capture Snapshot into intermediate buffer */
    CameraPipeline_NNPipe_Start(dcmipp_out_nn, CMW_MODE_SNAPSHOT);
#else
    /* Start NN camera single capture Snapshot directly into NN input */
    CameraPipeline_NNPipe_Start(nn_in, CMW_MODE_SNAPSHOT);
#endif

    g_app_trace_stage = APP_TRACE_STAGE_WAITING_FOR_FRAME;
    uint32_t frame_wait_start = HAL_GetTick();
    while (cameraFrameReceived == 0)
    {
      if ((HAL_GetTick() - frame_wait_start) > 500U)
      {
        printf("ERROR: main: timeout waiting for NN frame event after pipe2 snapshot start\n");
        LogInferenceRuntimeState("main: nn-frame-timeout");
        LogCameraPipelineState("main: nn-wait-timeout");
        assert(0);
      }
    }
    cameraFrameReceived = 0;
    if (app_loop_trace_count < 10U)
    {
      printf("TRACE: main loop: frame=%lu camera frame received\n",
             (unsigned long) app_loop_trace_count);
    }

    uint32_t ts[2] = { 0 };

#if NN_INPUT_NEEDS_PREPROC
    /*
     * Crop/copy/quantize the camera frame into the NN input buffer.
     * The DCMIPP hardware may pad each row to a 16-byte boundary, and the
     * Safal OBB model also needs uint8 camera bytes remapped into signed int8.
     */
    g_app_trace_stage = APP_TRACE_STAGE_PREPROCESS;
    SCB_InvalidateDCache_by_Addr(dcmipp_out_nn, sizeof(dcmipp_out_nn));
    PreprocessCameraFrameToNNInput(dcmipp_out_nn, nn_in, pitch_nn);
    SCB_CleanInvalidateDCache_by_Addr(nn_in, nn_in_len);
#else
    g_app_trace_stage = APP_TRACE_STAGE_PREPROCESS;
    SCB_InvalidateDCache_by_Addr(nn_in, nn_in_len);
#endif

    ts[0] = HAL_GetTick();
    if (app_loop_trace_count < 10U)
    {
      uint32_t sample_len = (nn_in_len < 64U) ? nn_in_len : 64U;
      printf("TRACE: main loop: frame=%lu stai_network_run begin nn_in=%p align=0x%lX checksum64=0x%08lX output0=%p output0_len=%ld\n",
             (unsigned long) app_loop_trace_count,
             nn_in,
             (unsigned long) (((uintptr_t)nn_in) & 31U),
             (unsigned long) SampleBytesChecksum32((const uint8_t *)nn_in, sample_len),
             nn_out[0],
             (long) nn_out_len[0]);
    }
    LogInferenceRuntimeState("main: before-inference");
    ret = RunInferenceWithTracing(app_loop_trace_count);
    if (app_loop_trace_count < 10U)
    {
      printf("TRACE: main loop: frame=%lu stai_network_run ret=%d\n",
             (unsigned long) app_loop_trace_count,
             ret);
    }
    assert(ret == 0);
    ts[1] = HAL_GetTick();

    g_app_trace_stage = APP_TRACE_STAGE_POSTPROCESS;
    if (app_loop_trace_count < 10U)
    {
      printf("TRACE: main loop: frame=%lu postprocess begin\n",
             (unsigned long) app_loop_trace_count);
    }
    int32_t ret = app_postprocess_run((void **) nn_out, number_output, &pp_output, &pp_params);
    if (app_loop_trace_count < 10U)
    {
      printf("TRACE: main loop: frame=%lu postprocess ret=%ld\n",
             (unsigned long) app_loop_trace_count,
             (long) ret);
    }
    assert(ret == 0);
    if ((app_loop_trace_count < 10U) || ((app_loop_trace_count % 30U) == 0U))
    {
      printf("TRACE: main loop: frame=%lu inference_ms=%lu detections=%lu\n",
             (unsigned long) app_loop_trace_count,
             (unsigned long) (ts[1] - ts[0]),
             (unsigned long) pp_output.nb_detect);
    }

    g_app_trace_stage = APP_TRACE_STAGE_DISPLAY;
    Display_NetworkOutput(&pp_output, ts[1] - ts[0]);
    /* Discard nn_out region (used by pp_input and pp_outputs variables) to avoid Dcache evictions during nn inference */
    for (int i = 0; i < number_output; i++)
    {
      void *tmp = nn_out[i];
      SCB_InvalidateDCache_by_Addr(tmp, nn_out_len[i]);
    }
    app_loop_trace_count++;
  }
}


static void Hardware_init(void)
{
  /* Power on ICACHE */
  MEMSYSCTL->MSCR |= MEMSYSCTL_MSCR_ICACTIVE_Msk;

  /* Set back system and CPU clock source to HSI */
  __HAL_RCC_CPUCLK_CONFIG(RCC_CPUCLKSOURCE_HSI);
  __HAL_RCC_SYSCLK_CONFIG(RCC_SYSCLKSOURCE_HSI);

  HAL_Init();

  SCB_EnableICache();

#if defined(USE_DCACHE)
  /* Power on DCACHE */
  MEMSYSCTL->MSCR |= MEMSYSCTL_MSCR_DCACTIVE_Msk;
  SCB_EnableDCache();
#endif

  SystemClock_Config();

  CONSOLE_Config();
  printf("TRACE: Hardware_init: console ready at 115200 baud\n");

  printf("TRACE: Hardware_init: NPURam_enable begin\n");
  NPURam_enable();
  printf("TRACE: Hardware_init: NPURam_enable OK\n");

  printf("TRACE: Hardware_init: Fuse_Programming begin\n");
  Fuse_Programming();
  printf("TRACE: Hardware_init: Fuse_Programming OK\n");

  printf("TRACE: Hardware_init: NPUCache_config begin\n");
  NPUCache_config();
  printf("TRACE: Hardware_init: NPUCache_config OK\n");

  /*** External NOR Flash *********************************************/
  printf("TRACE: Hardware_init: external NOR init begin\n");
  BSP_XSPI_NOR_Init_t NOR_Init;
  NOR_Init.InterfaceMode = BSP_XSPI_NOR_OPI_MODE;
  NOR_Init.TransferRate = BSP_XSPI_NOR_DTR_TRANSFER;
  BSP_XSPI_NOR_Init(0, &NOR_Init);
  BSP_XSPI_NOR_EnableMemoryMappedMode(0);
  printf("TRACE: Hardware_init: external NOR memory mapped OK\n");

  /* Set all required IPs as secure privileged */
  printf("TRACE: Hardware_init: Security_Config begin\n");
  Security_Config();
  printf("TRACE: Hardware_init: Security_Config OK\n");

  printf("TRACE: Hardware_init: IAC_Config begin\n");
  IAC_Config();
  printf("TRACE: Hardware_init: IAC_Config OK\n");
  set_clk_sleep_mode();
  printf("TRACE: Hardware_init: sleep clock config OK\n");

}

static void NeuralNetwork_init(uint32_t *nn_in_length, stai_ptr *nn_out, stai_size *number_output, int32_t nn_out_len[])
{
  stai_network_info info;
  int ret;

  /* initialize runtime */
  ret = stai_runtime_init();
  printf("TRACE: NeuralNetwork_init: stai_runtime_init ret=%d\n", ret);
  assert(ret == STAI_SUCCESS);
  /* init model instance */
  ret = stai_network_init(network_context);
  printf("TRACE: NeuralNetwork_init: stai_network_init ret=%d\n", ret);
  assert(ret == STAI_SUCCESS);

  ret = stai_network_get_info(network_context, &info);
  printf("TRACE: NeuralNetwork_init: stai_network_get_info ret=%d inputs=%lu outputs=%lu\n",
         ret, (unsigned long) info.n_inputs, (unsigned long) info.n_outputs);
  assert(ret == STAI_SUCCESS);
  assert(info.n_inputs == 1);
  *number_output = STAI_NETWORK_OUT_NUM;
  nn_input_scale = info.inputs[0].scale.data[0];
  nn_input_zero_point = info.inputs[0].zeropoint.data[0];

#if APP_MODEL_PROFILE == APP_MODEL_PROFILE_SAFAL_OBB
  assert((info.inputs[0].format == STAI_FORMAT_U8) || (info.inputs[0].format == STAI_FORMAT_S8));
#endif

  /* Get the input buffer size & address */
  *nn_in_length = info.inputs[0].size_bytes;
  g_nn_in_len = *nn_in_length;
  ret = stai_network_get_inputs(network_context, &nn_in, (stai_size *)&info.n_inputs);
  printf("TRACE: NeuralNetwork_init: stai_network_get_inputs ret=%d input_bytes=%lu input_ptr=%p\n",
         ret, (unsigned long) *nn_in_length, nn_in);
  assert(ret == STAI_SUCCESS);

  /* Get the output buffers size & address */
  ret = stai_network_get_outputs(network_context, nn_out, number_output);
  printf("TRACE: NeuralNetwork_init: stai_network_get_outputs ret=%d\n", ret);
  assert(ret == STAI_SUCCESS);
  g_number_output = *number_output;
  for (int i = 0; i < *number_output; i++)
  {
    nn_out_len[i] = info.outputs[i].size_bytes;
    printf("TRACE: NeuralNetwork_init: output[%d] ptr=%p bytes=%ld format=0x%08lX\n",
           i, nn_out[i], (long) nn_out_len[i], (unsigned long) info.outputs[i].format);
  }
}

static void NPURam_enable(void)
{
  __HAL_RCC_NPU_CLK_ENABLE();
  __HAL_RCC_NPU_FORCE_RESET();
  __HAL_RCC_NPU_RELEASE_RESET();

  /* Enable NPU RAMs (4x448KB) */
  __HAL_RCC_AXISRAM3_MEM_CLK_ENABLE();
  __HAL_RCC_AXISRAM4_MEM_CLK_ENABLE();
  __HAL_RCC_AXISRAM5_MEM_CLK_ENABLE();
  __HAL_RCC_AXISRAM6_MEM_CLK_ENABLE();
  __HAL_RCC_RAMCFG_CLK_ENABLE();
  RAMCFG_HandleTypeDef hramcfg = {0};
  hramcfg.Instance =  RAMCFG_SRAM3_AXI;
  HAL_RAMCFG_EnableAXISRAM(&hramcfg);
  hramcfg.Instance =  RAMCFG_SRAM4_AXI;
  HAL_RAMCFG_EnableAXISRAM(&hramcfg);
  hramcfg.Instance =  RAMCFG_SRAM5_AXI;
  HAL_RAMCFG_EnableAXISRAM(&hramcfg);
  hramcfg.Instance =  RAMCFG_SRAM6_AXI;
  HAL_RAMCFG_EnableAXISRAM(&hramcfg);
}

static void set_clk_sleep_mode(void)
{
  /*** Enable sleep mode support during NPU inference *************************/
  /* Configure peripheral clocks to remain active during sleep mode */
  /* Keep all IP's enabled during WFE so they can wake up CPU. Fine tune
   * this if you want to save maximum power
   */
  __HAL_RCC_XSPI1_CLK_SLEEP_ENABLE();    /* For display frame buffer */
  __HAL_RCC_XSPI2_CLK_SLEEP_ENABLE();    /* For NN weights */
  __HAL_RCC_NPU_CLK_SLEEP_ENABLE();      /* For NN inference */
  __HAL_RCC_CACHEAXI_CLK_SLEEP_ENABLE(); /* For NN inference */
  __HAL_RCC_DMA2D_CLK_SLEEP_ENABLE();    /* For display */
  __HAL_RCC_DCMIPP_CLK_SLEEP_ENABLE();   /* For camera configuration retention */
  __HAL_RCC_CSI_CLK_SLEEP_ENABLE();      /* For camera configuration retention */

  __HAL_RCC_FLEXRAM_MEM_CLK_SLEEP_ENABLE();
  __HAL_RCC_AXISRAM1_MEM_CLK_SLEEP_ENABLE();
  __HAL_RCC_AXISRAM2_MEM_CLK_SLEEP_ENABLE();
  __HAL_RCC_AXISRAM3_MEM_CLK_SLEEP_ENABLE();
  __HAL_RCC_AXISRAM4_MEM_CLK_SLEEP_ENABLE();
  __HAL_RCC_AXISRAM5_MEM_CLK_SLEEP_ENABLE();
  __HAL_RCC_AXISRAM6_MEM_CLK_SLEEP_ENABLE();
}

static void NPUCache_config(void)
{
  npu_cache_enable();
}

static void Security_Config(void)
{
  __HAL_RCC_RIFSC_CLK_ENABLE();
  RIMC_MasterConfig_t RIMC_master = {0};
  RIMC_master.MasterCID = RIF_CID_1;
  RIMC_master.SecPriv = RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV;
  HAL_RIF_RIMC_ConfigMasterAttributes(RIF_MASTER_INDEX_NPU, &RIMC_master);
  HAL_RIF_RIMC_ConfigMasterAttributes(RIF_MASTER_INDEX_DMA2D, &RIMC_master);
  HAL_RIF_RIMC_ConfigMasterAttributes(RIF_MASTER_INDEX_DCMIPP, &RIMC_master);
  HAL_RIF_RIMC_ConfigMasterAttributes(RIF_MASTER_INDEX_LTDC1 , &RIMC_master);
  HAL_RIF_RIMC_ConfigMasterAttributes(RIF_MASTER_INDEX_LTDC2 , &RIMC_master);
  HAL_RIF_RIMC_ConfigMasterAttributes(RIF_MASTER_INDEX_OTG1 , &RIMC_master);
  HAL_RIF_RISC_SetSlaveSecureAttributes(RIF_RISC_PERIPH_INDEX_NPU , RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV);
  HAL_RIF_RISC_SetSlaveSecureAttributes(RIF_RISC_PERIPH_INDEX_DMA2D , RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV);
  HAL_RIF_RISC_SetSlaveSecureAttributes(RIF_RISC_PERIPH_INDEX_CSI    , RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV);
  HAL_RIF_RISC_SetSlaveSecureAttributes(RIF_RISC_PERIPH_INDEX_DCMIPP , RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV);
  HAL_RIF_RISC_SetSlaveSecureAttributes(RIF_RISC_PERIPH_INDEX_LTDC   , RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV);
  HAL_RIF_RISC_SetSlaveSecureAttributes(RIF_RISC_PERIPH_INDEX_LTDCL1 , RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV);
  HAL_RIF_RISC_SetSlaveSecureAttributes(RIF_RISC_PERIPH_INDEX_LTDCL2 , RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV);
  HAL_RIF_RISC_SetSlaveSecureAttributes(RIF_RISC_PERIPH_INDEX_OTG1HS , RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV);
  HAL_RIF_RISC_SetSlaveSecureAttributes(RIF_RISC_PERIPH_INDEX_SPI5 , RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV);
}

static void IAC_Config(void)
{
/* Configure IAC to trap illegal access events */
  __HAL_RCC_IAC_CLK_ENABLE();
  __HAL_RCC_IAC_FORCE_RESET();
  __HAL_RCC_IAC_RELEASE_RESET();
}

void IAC_IRQHandler(void)
{
  printf("ERROR: IAC_IRQHandler triggered\n");
  while (1)
  {
  }
}

void Display_InvalidateCameraBuffer(void)
{
  SCB_InvalidateDCache_by_Addr(lcd_bg_buffer, sizeof(lcd_bg_buffer));
}

/**
* @brief Display Neural Network output classification results as well as other performances informations
*
* @param p_postprocess pointer to postprocessing output
* @param inference_ms inference time in ms
*/
static void Display_NetworkOutput(od_pp_out_t *p_postprocess, uint32_t inference_ms)
{

  od_pp_outBuffer_t *rois = p_postprocess->pOutBuff;
  uint32_t nb_rois = p_postprocess->nb_detect;
  int ret;

  __disable_irq();
  ret = SCRL_SetAddress_NoReload(lcd_fg_buffer[lcd_fg_buffer_rd_idx], SCRL_LAYER_1);
  assert(ret == HAL_OK);
  __enable_irq();

  /* Draw bounding boxes */
  UTIL_LCD_FillRect(0, 0, lcd_fg_area.XSize, lcd_fg_area.YSize, UTIL_LCD_COLOR_TRANSPARENT); /* Clear previous boxes */
  for (int32_t i = 0; i < nb_rois; i++)
  {
    const char *label = (rois[i].class_index < NB_CLASSES) ? classes_table[rois[i].class_index] : "unknown";
    uint32_t box_color = Display_GetBoxColor(rois[i].class_index);
    uint32_t x0 = (uint32_t) ((rois[i].x_center - rois[i].width / 2) * ((float32_t) lcd_bg_area.XSize));
    uint32_t y0 = (uint32_t) ((rois[i].y_center - rois[i].height / 2) * ((float32_t) lcd_bg_area.YSize));
    uint32_t width = (uint32_t) (rois[i].width * ((float32_t) lcd_bg_area.XSize));
    uint32_t height = (uint32_t) (rois[i].height * ((float32_t) lcd_bg_area.YSize));
    /* Draw boxes without going outside of the image */
    x0 = x0 < lcd_bg_area.XSize ? x0 : lcd_bg_area.XSize - 1;
    y0 = y0 < lcd_bg_area.YSize ? y0 : lcd_bg_area.YSize - 1;
    width = ((x0 + width) < lcd_bg_area.XSize) ? width : (lcd_bg_area.XSize - x0 - 1);
    height = ((y0 + height) < lcd_bg_area.YSize) ? height : (lcd_bg_area.YSize - y0 - 1);
    UTIL_LCD_SetTextColor(box_color);
    UTIL_LCD_DrawRect(x0, y0, width, height, box_color);
    UTIL_LCDEx_PrintfAt(x0, y0, LEFT_MODE, "%s %.0f%%", label, rois[i].conf * 100.0f);
  }

  UTIL_LCD_SetBackColor(0x40000000);
  UTIL_LCD_SetTextColor(UTIL_LCD_COLOR_WHITE);
  UTIL_LCDEx_PrintfAt(0, LINE(0), LEFT_MODE, "Inference");
  UTIL_LCDEx_PrintfAt(0, LINE(1), LEFT_MODE, "%ums", inference_ms);
  UTIL_LCDEx_PrintfAt(0, LINE(0), RIGHT_MODE, "Objects %u", nb_rois);
  UTIL_LCD_SetBackColor(0);

  Display_WelcomeScreen();

  SCB_CleanDCache_by_Addr(lcd_fg_buffer[lcd_fg_buffer_rd_idx], LCD_FG_FRAMEBUFFER_SIZE);
  __disable_irq();
  ret = SCRL_ReloadLayer(SCRL_LAYER_1);
  assert(ret == HAL_OK);
  __enable_irq();
  lcd_fg_buffer_rd_idx = 1 - lcd_fg_buffer_rd_idx;
}

static uint32_t Display_GetBoxColor(uint32_t class_index)
{
  if (class_index == 0U)
  {
    return UTIL_LCD_COLOR_BLUE;
  }

  if (class_index == 1U)
  {
    return UTIL_LCD_COLOR_RED;
  }

  return colors[class_index % NUMBER_COLORS];
}

static void Display_init(void)
{
  SCRL_LayerConfig layers_config[2] = {
    {
      .origin = {lcd_bg_area.X0, lcd_bg_area.Y0},
      .size = {lcd_bg_area.XSize, lcd_bg_area.YSize},
      .format = SCRL_RGB565,
      .address = lcd_bg_buffer,
    },
    {
      .origin = {lcd_fg_area.X0, lcd_fg_area.Y0},
      .size = {lcd_fg_area.XSize, lcd_fg_area.YSize},
      .format = SCRL_ARGB4444,
      .address = lcd_fg_buffer[lcd_fg_buffer_rd_idx],
    },
  };
  SCRL_ScreenConfig screen_config = {
    .size = {LCD_FG_WIDTH, LCD_FG_HEIGHT},
#ifdef SCR_LIB_USE_SPI
    .format = SCRL_RGB565,
#else
    .format = SCRL_YUV422, /* Use SCRL_RGB565 if host support this format to reduce cpu load */
#endif
    .address = screen_buffer,
    .fps = CAMERA_FPS,
  };
  int ret;

  /* Initialize the LCD to black */
#ifdef SCR_LIB_USE_SPI
  memset(screen_buffer, 0, sizeof(screen_buffer));
  SCB_CleanDCache_by_Addr(screen_buffer, sizeof(screen_buffer));
#else
  uint32_t *p_screen_buffer = (uint32_t *) screen_buffer;
  for (int i = 0; i < sizeof(screen_buffer)/4; i++)
  {
    p_screen_buffer[i] = 0x80108010;
  }
  SCB_CleanDCache_by_Addr(screen_buffer, sizeof(screen_buffer));
#endif

  ret = SCRL_Init((SCRL_LayerConfig *[2]){&layers_config[0], &layers_config[1]}, &screen_config);
  printf("TRACE: Display_init: SCRL_Init ret=%d screen=%lux%lu fps=%lu format=0x%08lX\n",
         ret,
         (unsigned long) screen_config.size.width,
         (unsigned long) screen_config.size.height,
         (unsigned long) screen_config.fps,
         (unsigned long) screen_config.format);
  assert(ret == 0);

  UTIL_LCD_SetLayer(SCRL_LAYER_1);
  UTIL_LCD_Clear(UTIL_LCD_COLOR_TRANSPARENT);
  UTIL_LCD_SetFont(&Font12);
  UTIL_LCD_SetTextColor(UTIL_LCD_COLOR_WHITE);
}

/**
 * @brief Displays a Welcome screen
 */
static void Display_WelcomeScreen(void)
{
  static uint32_t t0 = 0;
  if (t0 == 0)
    t0 = HAL_GetTick();

  if (HAL_GetTick() - t0 < 4000)
  {
    /* Draw logo */
    UTIL_LCD_FillRGBRect((lcd_bg_area.XSize-200)/2, 54, (uint8_t *) stlogo, 200, 107);

    /* Display welcome message */
    UTIL_LCD_SetBackColor(0x40000000);
    UTIL_LCDEx_PrintfAt(0, LINE(15), CENTER_MODE, "Object Detection");
    UTIL_LCDEx_PrintfAt(0, LINE(16), CENTER_MODE, WELCOME_MSG_1);
    UTIL_LCDEx_PrintfAt(0, LINE(17), CENTER_MODE, WELCOME_MSG_2[0]);
    UTIL_LCDEx_PrintfAt(0, LINE(18), CENTER_MODE, WELCOME_MSG_2[1]);
    UTIL_LCD_SetBackColor(0);
  }
}

/**
  * @brief  DCMIPP Clock Config for DCMIPP.
  * @param  hdcmipp  DCMIPP Handle
  *         Being __weak it can be overwritten by the application
  * @retval HAL_status
  */
HAL_StatusTypeDef MX_DCMIPP_ClockConfig(DCMIPP_HandleTypeDef *hdcmipp)
{
  RCC_PeriphCLKInitTypeDef RCC_PeriphCLKInitStruct = {0};
  HAL_StatusTypeDef ret = HAL_OK;

  RCC_PeriphCLKInitStruct.PeriphClockSelection = RCC_PERIPHCLK_DCMIPP;
  RCC_PeriphCLKInitStruct.DcmippClockSelection = RCC_DCMIPPCLKSOURCE_IC17;
  RCC_PeriphCLKInitStruct.ICSelection[RCC_IC17].ClockSelection = RCC_ICCLKSOURCE_PLL2;
  RCC_PeriphCLKInitStruct.ICSelection[RCC_IC17].ClockDivider = 3;
  ret = HAL_RCCEx_PeriphCLKConfig(&RCC_PeriphCLKInitStruct);
  if (ret)
  {
    return ret;
  }

  RCC_PeriphCLKInitStruct.PeriphClockSelection = RCC_PERIPHCLK_CSI;
  RCC_PeriphCLKInitStruct.ICSelection[RCC_IC18].ClockSelection = RCC_ICCLKSOURCE_PLL1;
  RCC_PeriphCLKInitStruct.ICSelection[RCC_IC18].ClockDivider = 40;
  ret = HAL_RCCEx_PeriphCLKConfig(&RCC_PeriphCLKInitStruct);
  if (ret)
  {
    return ret;
  }

  return ret;
}

static void SystemClock_Config(void)
{
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_PeriphCLKInitTypeDef RCC_PeriphCLKInitStruct = {0};

  /* Ensure VDDCORE=0.9V before increasing the system frequency */
  BSP_SMPS_Init(SMPS_VOLTAGE_OVERDRIVE);

  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_NONE;

  /* PLL1 = 64 x 25 / 2 = 800MHz */
  RCC_OscInitStruct.PLL1.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL1.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL1.PLLM = 2;
  RCC_OscInitStruct.PLL1.PLLN = 25;
  RCC_OscInitStruct.PLL1.PLLFractional = 0;
  RCC_OscInitStruct.PLL1.PLLP1 = 1;
  RCC_OscInitStruct.PLL1.PLLP2 = 1;

  /* PLL2 = 64 x 125 / 8 = 1000MHz */
  RCC_OscInitStruct.PLL2.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL2.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL2.PLLM = 8;
  RCC_OscInitStruct.PLL2.PLLFractional = 0;
  RCC_OscInitStruct.PLL2.PLLN = 125;
  RCC_OscInitStruct.PLL2.PLLP1 = 1;
  RCC_OscInitStruct.PLL2.PLLP2 = 1;

  /* PLL3 = (64 x 225 / 8) / (1 * 2) = 900MHz */
  RCC_OscInitStruct.PLL3.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL3.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL3.PLLM = 8;
  RCC_OscInitStruct.PLL3.PLLN = 225;
  RCC_OscInitStruct.PLL3.PLLFractional = 0;
  RCC_OscInitStruct.PLL3.PLLP1 = 1;
  RCC_OscInitStruct.PLL3.PLLP2 = 2;

  /* PLL4 = (64 x 225 / 8) / (6 * 6) = 50 MHz */
  RCC_OscInitStruct.PLL4.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL4.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL4.PLLM = 8;
  RCC_OscInitStruct.PLL4.PLLFractional = 0;
  RCC_OscInitStruct.PLL4.PLLN = 225;
  RCC_OscInitStruct.PLL4.PLLP1 = 6;
  RCC_OscInitStruct.PLL4.PLLP2 = 6;

  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    while(1);
  }

  RCC_ClkInitStruct.ClockType = (RCC_CLOCKTYPE_CPUCLK | RCC_CLOCKTYPE_SYSCLK |
                                 RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_PCLK1 |
                                 RCC_CLOCKTYPE_PCLK2 | RCC_CLOCKTYPE_PCLK4 |
                                 RCC_CLOCKTYPE_PCLK5);

  /* CPU CLock (sysa_ck) = ic1_ck = PLL1 output/ic1_divider = 800 MHz */
  RCC_ClkInitStruct.CPUCLKSource = RCC_CPUCLKSOURCE_IC1;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_IC2_IC6_IC11;
  RCC_ClkInitStruct.IC1Selection.ClockSelection = RCC_ICCLKSOURCE_PLL1;
  RCC_ClkInitStruct.IC1Selection.ClockDivider = 1;

  /* AXI Clock (sysb_ck) = ic2_ck = PLL1 output/ic2_divider = 400 MHz */
  RCC_ClkInitStruct.IC2Selection.ClockSelection = RCC_ICCLKSOURCE_PLL1;
  RCC_ClkInitStruct.IC2Selection.ClockDivider = 2;

  /* NPU Clock (sysc_ck) = ic6_ck = PLL2 output/ic6_divider = 1000 MHz */
  RCC_ClkInitStruct.IC6Selection.ClockSelection = RCC_ICCLKSOURCE_PLL2;
  RCC_ClkInitStruct.IC6Selection.ClockDivider = 1;

  /* AXISRAM3/4/5/6 Clock (sysd_ck) = ic11_ck = PLL3 output/ic11_divider = 900 MHz */
  RCC_ClkInitStruct.IC11Selection.ClockSelection = RCC_ICCLKSOURCE_PLL3;
  RCC_ClkInitStruct.IC11Selection.ClockDivider = 1;

  /* HCLK = sysb_ck / HCLK divider = 200 MHz */
  RCC_ClkInitStruct.AHBCLKDivider = RCC_HCLK_DIV2;

  /* PCLKx = HCLK / PCLKx divider = 200 MHz */
  RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_APB2_DIV1;
  RCC_ClkInitStruct.APB4CLKDivider = RCC_APB4_DIV1;
  RCC_ClkInitStruct.APB5CLKDivider = RCC_APB5_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct) != HAL_OK)
  {
    while(1);
  }

  RCC_PeriphCLKInitStruct.PeriphClockSelection = 0;

  /* XSPI1 kernel clock (ck_ker_xspi1) = HCLK = 200MHz */
  RCC_PeriphCLKInitStruct.PeriphClockSelection |= RCC_PERIPHCLK_XSPI1;
  RCC_PeriphCLKInitStruct.Xspi1ClockSelection = RCC_XSPI1CLKSOURCE_HCLK;

  /* XSPI2 kernel clock (ck_ker_xspi1) = HCLK =  200MHz */
  RCC_PeriphCLKInitStruct.PeriphClockSelection |= RCC_PERIPHCLK_XSPI2;
  RCC_PeriphCLKInitStruct.Xspi2ClockSelection = RCC_XSPI2CLKSOURCE_HCLK;

  if (HAL_RCCEx_PeriphCLKConfig(&RCC_PeriphCLKInitStruct) != HAL_OK)
  {
    while (1);
  }
}

static void CONSOLE_Config()
{
  GPIO_InitTypeDef gpio_init;

  __HAL_RCC_USART1_CLK_ENABLE();
  __HAL_RCC_GPIOE_CLK_ENABLE();

 /* DISCO & NUCLEO USART1 (PE5/PE6) */
  gpio_init.Mode      = GPIO_MODE_AF_PP;
  gpio_init.Pull      = GPIO_PULLUP;
  gpio_init.Speed     = GPIO_SPEED_FREQ_HIGH;
  gpio_init.Pin       = GPIO_PIN_5 | GPIO_PIN_6;
  gpio_init.Alternate = GPIO_AF7_USART1;
  HAL_GPIO_Init(GPIOE, &gpio_init);

  huart1.Instance          = USART1;
  huart1.Init.BaudRate     = 115200;
  huart1.Init.Mode         = UART_MODE_TX_RX;
  huart1.Init.Parity       = UART_PARITY_NONE;
  huart1.Init.WordLength   = UART_WORDLENGTH_8B;
  huart1.Init.StopBits     = UART_STOPBITS_1;
  huart1.Init.HwFlowCtl    = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_8;
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    while (1);
  }
}

int _write(int file, char *ptr, int len)
{
  HAL_StatusTypeDef status;

  if ((file != STDOUT_FILENO) && (file != STDERR_FILENO)) {
      errno = EBADF;
      return -1;
  }

  status = HAL_UART_Transmit(&huart1, (uint8_t*)ptr, len, ~0);

  return (status == HAL_OK ? len : 0);
}


void npu_cache_enable_clocks_and_reset(void)
{
  __HAL_RCC_CACHEAXIRAM_MEM_CLK_ENABLE();
  __HAL_RCC_CACHEAXI_CLK_ENABLE();
  __HAL_RCC_CACHEAXI_FORCE_RESET();
  __HAL_RCC_CACHEAXI_RELEASE_RESET();
}

void npu_cache_disable_clocks_and_reset(void)
{
  __HAL_RCC_CACHEAXIRAM_MEM_CLK_DISABLE();
  __HAL_RCC_CACHEAXI_CLK_DISABLE();
  __HAL_RCC_CACHEAXI_FORCE_RESET();
}

#ifdef  USE_FULL_ASSERT

/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t* file, uint32_t line)
{
  UNUSED(file);
  UNUSED(line);
  __BKPT(0);
  while (1)
  {
  }
}

#endif
