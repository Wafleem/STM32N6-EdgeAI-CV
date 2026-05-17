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
#include "logger.h"
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
#if DISPLAY_ASPECT_RATIO_MODE == ASPECT_RATIO_CROP || DISPLAY_ASPECT_RATIO_MODE == ASPECT_RATIO_FIT
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
#if DISPLAY_ASPECT_RATIO_MODE == ASPECT_RATIO_CROP || DISPLAY_ASPECT_RATIO_MODE == ASPECT_RATIO_FIT
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
static uint32_t g_last_loop_ms = 0;
static uint32_t g_last_camera_wait_ms = 0;
static uint32_t g_last_loop_fps_x10 = 0;
static int32_t g_last_detection_count = -1;

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
#define ALIGN_TO_32(value) (((value) + 31) & ~31)

#ifndef APP_TRACE_STARTUP
#define APP_TRACE_STARTUP (1U)
#endif
#ifndef APP_TRACE_NN_INPUT
#define APP_TRACE_NN_INPUT (1U)
#endif
#ifndef APP_TRACE_DETECTIONS
#define APP_TRACE_DETECTIONS (1U)
#endif
#ifndef APP_TRACE_PERFORMANCE
#define APP_TRACE_PERFORMANCE (1U)
#endif
#ifndef APP_TRACE_FRAME_PERIOD
#define APP_TRACE_FRAME_PERIOD (30U)
#endif

/* When NN input dimensions are not a multiple of 16, the DCMIPP output needs cropping */
#if (STAI_NETWORK_IN_1_WIDTH * STAI_NETWORK_IN_1_CHANNEL) != ALIGN_TO_16(STAI_NETWORK_IN_1_WIDTH * STAI_NETWORK_IN_1_CHANNEL)
#define DCMIPP_NN_NEEDS_CROP 1
#else
#define DCMIPP_NN_NEEDS_CROP 0
#endif

#if (STAI_NETWORK_IN_1_FLAGS & STAI_FLAG_CHANNEL_FIRST)
#define NN_INPUT_IS_CHANNEL_FIRST 1
#else
#define NN_INPUT_IS_CHANNEL_FIRST 0
#endif

#define NN_INPUT_NEEDS_PREPROC (DCMIPP_NN_NEEDS_CROP || NN_INPUT_IS_CHANNEL_FIRST)

#if NN_INPUT_NEEDS_PREPROC
#define NN_CAMERA_CAPTURE_ROW_BYTES (ALIGN_TO_16(STAI_NETWORK_IN_1_WIDTH * STAI_NETWORK_IN_1_CHANNEL))
#define NN_CAMERA_CAPTURE_LEN       (NN_CAMERA_CAPTURE_ROW_BYTES * STAI_NETWORK_IN_1_HEIGHT)
#define NN_CAMERA_CAPTURE_BUFF_LEN  (ALIGN_TO_32(NN_CAMERA_CAPTURE_LEN))

/*
 * Pipe2 produces interleaved camera pixels. Keep that DMA target separate from
 * nn_in so the next camera snapshot can overlap the NPU reading the previous
 * transposed/quantized input.
 */
__attribute__ ((aligned (32)))
static uint8_t nn_camera_capture[NN_CAMERA_CAPTURE_BUFF_LEN];
#endif

#if NN_INPUT_IS_CHANNEL_FIRST
#define NN_INPUT_VISITED_BYTES ((STAI_NETWORK_IN_1_SIZE_BYTES + 7U) / 8U)
static uint8_t nn_input_transpose_visited[NN_INPUT_VISITED_BYTES];
#endif

/* model */
STAI_NETWORK_CONTEXT_DECLARE(network_context, STAI_NETWORK_CONTEXT_SIZE)
/* Lcd Background Buffer */
__attribute__ ((aligned (32)))
static uint8_t lcd_bg_buffer[LCD_BG_WIDTH * LCD_BG_HEIGHT * 2];
/* Lcd Foreground Buffer */
#if NN_INPUT_NEEDS_PREPROC
/*
 * Overlapped NN capture needs a full RGB888 camera DMA buffer. Use a single
 * foreground overlay buffer to keep the debug build inside AXISRAM.
 */
#define LCD_FG_BUFFER_COUNT 1
#else
#define LCD_FG_BUFFER_COUNT 2
#endif
__attribute__ ((aligned (32)))
static uint8_t lcd_fg_buffer[LCD_FG_BUFFER_COUNT][LCD_FG_WIDTH * LCD_FG_HEIGHT * 2];
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
static uint8_t ShouldTraceFrame(uint32_t frame_index);
static void StartNNFrameCapture(uint8_t *dst, uint32_t frame_index);
static uint32_t WaitForNNFrameCapture(uint32_t frame_index);
#if NN_INPUT_NEEDS_PREPROC
static void PreprocessCameraFrameToNNInput(const uint8_t *src, uint8_t *dst, uint32_t src_stride);
static void PrepareCapturedFrameForInference(uint8_t *capture, uint32_t pitch_nn, uint32_t nn_in_len);
#endif
static uint32_t FpsX10FromMs(uint32_t elapsed_ms);
static void TraceDetections(uint32_t frame_index, const od_pp_out_t *output);
static void TraceFramePerformance(uint32_t frame_index, int32_t detections, uint32_t loop_ms,
                                  uint32_t headless_loop_ms, uint32_t inference_ms,
                                  uint32_t compute_ms, uint32_t preprocess_ms,
                                  uint32_t postprocess_ms, uint32_t display_ms,
                                  uint32_t camera_wait_ms);

/* -------------------------------------------------------------------------- */
/* Camera-to-NN tensor preparation                                             */
/* -------------------------------------------------------------------------- */
#if NN_INPUT_IS_CHANNEL_FIRST
static uint32_t HwcIndexToChwIndex(uint32_t index)
{
  uint32_t pixel_index = index / STAI_NETWORK_IN_1_CHANNEL;
  uint32_t channel_index = index % STAI_NETWORK_IN_1_CHANNEL;
  return (channel_index * STAI_NETWORK_IN_1_WIDTH * STAI_NETWORK_IN_1_HEIGHT) + pixel_index;
}

static uint8_t VisitedBitIsSet(uint32_t index)
{
  return (nn_input_transpose_visited[index >> 3] & (uint8_t)(1U << (index & 7U))) != 0U;
}

static void SetVisitedBit(uint32_t index)
{
  nn_input_transpose_visited[index >> 3] |= (uint8_t)(1U << (index & 7U));
}

static void TransposeHwcToChwInPlace(uint8_t *buffer)
{
  memset(nn_input_transpose_visited, 0, sizeof(nn_input_transpose_visited));

  for (uint32_t i = 0; i < STAI_NETWORK_IN_1_SIZE_BYTES; i++)
  {
    uint32_t current;
    uint8_t value;

    if (VisitedBitIsSet(i) != 0U)
    {
      continue;
    }

    current = i;
    value = buffer[i];

    while (VisitedBitIsSet(current) == 0U)
    {
      uint32_t next = HwcIndexToChwIndex(current);
      uint8_t next_value = buffer[next];

      SetVisitedBit(current);
      buffer[next] = value;
      value = next_value;
      current = next;
    }
  }
}
#endif

/* -------------------------------------------------------------------------- */
/* Low-noise performance and detection tracing                                 */
/* -------------------------------------------------------------------------- */
static uint32_t SampleBytesChecksum32(const uint8_t *data, uint32_t len)
{
  uint32_t sum = 0;

  for (uint32_t i = 0; i < len; i++)
  {
    sum = (sum * 131U) + data[i];
  }

  return sum;
}

static uint8_t ShouldTraceFrame(uint32_t frame_index)
{
  return ((frame_index < 5U) || ((frame_index % APP_TRACE_FRAME_PERIOD) == 0U)) ? 1U : 0U;
}

static uint32_t FpsX10FromMs(uint32_t elapsed_ms)
{
  if (elapsed_ms == 0U)
  {
    return 0U;
  }

  return (10000U + (elapsed_ms / 2U)) / elapsed_ms;
}

static void TraceDetections(uint32_t frame_index, const od_pp_out_t *output)
{
  int32_t trace_count = output->nb_detect;

  if (trace_count > 2)
  {
    trace_count = 2;
  }

  printf("TRACE: detect: frame=%lu count=%ld",
         (unsigned long) frame_index,
         (long) output->nb_detect);

  for (int32_t i = 0; i < trace_count; i++)
  {
    const od_pp_outBuffer_t *box = &output->pOutBuff[i];
    printf(" box%ld[class=%ld conf=%.3f cx=%.1f cy=%.1f w=%.1f h=%.1f]",
           (long) i,
           (long) box->class_index,
           (double) box->conf,
           (double) box->x_center,
           (double) box->y_center,
           (double) box->width,
           (double) box->height);
  }

  printf("\n");
}

static void TraceFramePerformance(uint32_t frame_index, int32_t detections, uint32_t loop_ms,
                                  uint32_t headless_loop_ms, uint32_t inference_ms,
                                  uint32_t compute_ms, uint32_t preprocess_ms,
                                  uint32_t postprocess_ms, uint32_t display_ms,
                                  uint32_t camera_wait_ms)
{
  uint32_t loop_fps_x10 = FpsX10FromMs(loop_ms);
  uint32_t headless_fps_x10 = FpsX10FromMs(headless_loop_ms);
  uint32_t inference_fps_x10 = FpsX10FromMs(inference_ms);
  uint32_t compute_fps_x10 = FpsX10FromMs(compute_ms);

  printf("TRACE: perf: frame=%lu det=%ld loop=%lums/%lu.%lufps headless_est=%lums/%lu.%lufps nn=%lums/%lu.%lufps compute=%lums/%lu.%lufps prep=%lu post=%lu uvc_display=%lu cam_wait=%lu\n",
         (unsigned long) frame_index,
         (long) detections,
         (unsigned long) loop_ms,
         (unsigned long) (loop_fps_x10 / 10U),
         (unsigned long) (loop_fps_x10 % 10U),
         (unsigned long) headless_loop_ms,
         (unsigned long) (headless_fps_x10 / 10U),
         (unsigned long) (headless_fps_x10 % 10U),
         (unsigned long) inference_ms,
         (unsigned long) (inference_fps_x10 / 10U),
         (unsigned long) (inference_fps_x10 % 10U),
         (unsigned long) compute_ms,
         (unsigned long) (compute_fps_x10 / 10U),
         (unsigned long) (compute_fps_x10 % 10U),
         (unsigned long) preprocess_ms,
         (unsigned long) postprocess_ms,
         (unsigned long) display_ms,
         (unsigned long) camera_wait_ms);
}

static void StartNNFrameCapture(uint8_t *dst, uint32_t frame_index)
{
  CameraPipeline_IspUpdate();

  __disable_irq();
  cameraFrameReceived = 0;
  __enable_irq();

  CameraPipeline_NNPipe_Start(dst, CMW_MODE_SNAPSHOT);

  if (frame_index < 5U)
  {
    printf("TRACE: main loop: frame=%lu NN capture started dst=%p\n",
           (unsigned long) frame_index,
           dst);
  }
}

static uint32_t WaitForNNFrameCapture(uint32_t frame_index)
{
  uint32_t frame_wait_start = HAL_GetTick();

  g_app_trace_stage = APP_TRACE_STAGE_WAITING_FOR_FRAME;
  while (cameraFrameReceived == 0)
  {
    if ((HAL_GetTick() - frame_wait_start) > 500U)
    {
      printf("ERROR: main: timeout waiting for NN frame event frame=%lu\n",
             (unsigned long) frame_index);
      LogInferenceRuntimeState("main: nn-frame-timeout");
      LogCameraPipelineState("main: nn-wait-timeout");
      assert(0);
    }
  }

  __disable_irq();
  cameraFrameReceived = 0;
  __enable_irq();

  if (frame_index < 5U)
  {
    printf("TRACE: main loop: frame=%lu camera frame received\n",
           (unsigned long) frame_index);
  }

  return HAL_GetTick() - frame_wait_start;
}

#if NN_INPUT_NEEDS_PREPROC
static void PreprocessCameraFrameToNNInput(const uint8_t *src, uint8_t *dst, uint32_t src_stride)
{
  const uint32_t row_bytes = STAI_NETWORK_IN_1_WIDTH * STAI_NETWORK_IN_1_CHANNEL;

#if APP_MODEL_PROFILE == APP_MODEL_PROFILE_SAFAL_OBB
  if (NN_INPUT_IS_CHANNEL_FIRST != 0)
  {
    const uint32_t plane_size = STAI_NETWORK_IN_1_WIDTH * STAI_NETWORK_IN_1_HEIGHT;

    if (STAI_NETWORK_IN_1_FORMAT == STAI_FORMAT_U8)
    {
      if (src == dst)
      {
        TransposeHwcToChwInPlace(dst);
        return;
      }

      for (uint32_t y = 0; y < STAI_NETWORK_IN_1_HEIGHT; y++)
      {
        const uint8_t *src_row = src + (y * src_stride);

        for (uint32_t x = 0; x < STAI_NETWORK_IN_1_WIDTH; x++)
        {
          const uint32_t src_index = x * STAI_NETWORK_IN_1_CHANNEL;
          const uint32_t dst_index = (y * STAI_NETWORK_IN_1_WIDTH) + x;

          dst[dst_index] = src_row[src_index];
          dst[plane_size + dst_index] = src_row[src_index + 1U];
          dst[(2U * plane_size) + dst_index] = src_row[src_index + 2U];
        }
      }
      return;
    }
  }

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

static void PrepareCapturedFrameForInference(uint8_t *capture, uint32_t pitch_nn, uint32_t nn_in_len)
{
  /*
   * The DMA capture buffer is interleaved/padded camera data. Copy/transpose it
   * into nn_in only after the snapshot is complete, then start the next snapshot
   * while the NPU consumes nn_in.
   */
  g_app_trace_stage = APP_TRACE_STAGE_PREPROCESS;
  SCB_InvalidateDCache_by_Addr(capture, NN_CAMERA_CAPTURE_BUFF_LEN);
  PreprocessCameraFrameToNNInput(capture, nn_in, pitch_nn);
  SCB_CleanInvalidateDCache_by_Addr(nn_in, nn_in_len);
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
  uint32_t iter = 0;
  stai_return_code ret;

  g_app_trace_stage = APP_TRACE_STAGE_INFERENCE_START;
  ret = stai_network_run(network_context, STAI_MODE_ASYNC);
  if (ret >= STAI_ERROR_GENERIC)
  {
    printf("ERROR: main loop: frame=%lu stai_network_run ret=0x%06lX first_error=0x%06lX\n",
           (unsigned long) frame_index,
           (unsigned long) ret,
           (unsigned long) stai_network_get_error(network_context));
    return ret;
  }

  while (1)
  {
    stai_return_code status = stai_ext_network_get_nn_run_status(network_context);
    uint32_t elapsed_ms = HAL_GetTick() - inference_start;

    if (status == STAI_DONE)
    {
      stai_return_code reset_ret = stai_ext_network_new_inference(network_context);
      if (reset_ret >= STAI_ERROR_GENERIC)
      {
        printf("ERROR: main loop: frame=%lu inference reset error=0x%06lX elapsed_ms=%lu\n",
               (unsigned long) frame_index,
               (unsigned long) reset_ret,
               (unsigned long) elapsed_ms);
      }
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

  /*** NN Init ****************************************************************/
  uint32_t nn_in_len = 0;
  stai_size number_output = 0;
  stai_ptr nn_out[STAI_NETWORK_OUT_NUM] = {0};
  int32_t nn_out_len[STAI_NETWORK_OUT_NUM] = {0};

  g_app_trace_stage = APP_TRACE_STAGE_NN_INIT;
  NeuralNetwork_init(&nn_in_len, nn_out, &number_output, nn_out_len);

  /*** Post Processing Init ***************************************************/
  stai_network_info info;
  int ret;

  g_app_trace_stage = APP_TRACE_STAGE_POSTPROCESS_INIT;
  ret = stai_network_get_info(network_context, &info);
  assert(ret == STAI_SUCCESS);
  ret = app_postprocess_init(&pp_params, &info);
  assert(ret == 0);

  /*** Camera Init ************************************************************/
  uint32_t pitch_nn = 0;
  g_app_trace_stage = APP_TRACE_STAGE_CAMERA_INIT;
  CameraPipeline_Init((uint32_t *[2]) {&lcd_bg_area.XSize, &lcd_fg_area.XSize}, (uint32_t *[2]) {&lcd_bg_area.YSize, &lcd_fg_area.YSize}, &pitch_nn);

  g_app_trace_stage = APP_TRACE_STAGE_DISPLAY_INIT;
  Display_init();

  /* Start LCD Display camera pipe stream */
  g_app_trace_stage = APP_TRACE_STAGE_DISPLAY_PIPE_START;
  CameraPipeline_DisplayPipe_Start(lcd_bg_buffer, CMW_MODE_CONTINUOUS);

  /*** App header *************************************************************/
#if APP_TRACE_STARTUP
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
  printf("Display: bg=%lux%lu@%lu,%lu fg=%lux%lu@%lu,%lu nn_pitch=%lu color_swap=%u chw=%u\n",
         (unsigned long) lcd_bg_area.XSize, (unsigned long) lcd_bg_area.YSize,
         (unsigned long) lcd_bg_area.X0, (unsigned long) lcd_bg_area.Y0,
         (unsigned long) lcd_fg_area.XSize, (unsigned long) lcd_fg_area.YSize,
         (unsigned long) lcd_fg_area.X0, (unsigned long) lcd_fg_area.Y0,
         (unsigned long) pitch_nn,
         (unsigned int) COLOR_MODE,
         (unsigned int) NN_INPUT_IS_CHANNEL_FIRST);
  printf("========================================\n");
#endif

  /*** App Loop ***************************************************************/
  uint32_t app_loop_trace_count = 0;

#if NN_INPUT_NEEDS_PREPROC
  /*
   * Overlapped capture/inference loop:
   *   1. Capture one camera frame into a DMA-safe RGB/HWC buffer.
   *   2. Convert that completed frame into nn_in, including HWC->CHW when needed.
   *   3. Start the next camera capture before running the NPU.
   *   4. Run inference/postprocess/display on the current frame.
   *   5. Wait only for the next capture remainder; cam_wait=0 means it was hidden.
   *
   * This trades AXISRAM for lower exposed camera wait without changing model
   * outputs or postprocessing behavior.
   */
  g_app_trace_frame_index = 0U;
  StartNNFrameCapture(nn_camera_capture, 0U);
  WaitForNNFrameCapture(0U);
#endif

  while (1)
  {
    uint32_t loop_start_ms = HAL_GetTick();
    uint32_t preprocess_ms = 0U;
    uint32_t inference_ms = 0U;
    uint32_t postprocess_ms = 0U;
    uint32_t display_ms = 0U;
    uint32_t camera_wait_ms = 0U;

    g_app_trace_frame_index = app_loop_trace_count;

#if NN_INPUT_NEEDS_PREPROC
    uint32_t preprocess_start_ms = HAL_GetTick();
    PrepareCapturedFrameForInference(nn_camera_capture, pitch_nn, nn_in_len);
    preprocess_ms = HAL_GetTick() - preprocess_start_ms;
#else
    /* Start NN camera single capture Snapshot directly into NN input */
    StartNNFrameCapture(nn_in, app_loop_trace_count);
    camera_wait_ms = WaitForNNFrameCapture(app_loop_trace_count);

    g_app_trace_stage = APP_TRACE_STAGE_PREPROCESS;
    SCB_InvalidateDCache_by_Addr(nn_in, nn_in_len);
#endif

    uint32_t inference_start_ms = HAL_GetTick();
#if APP_TRACE_NN_INPUT
    if (ShouldTraceFrame(app_loop_trace_count) != 0U)
    {
      uint32_t sample_len = (nn_in_len < 64U) ? nn_in_len : 64U;
      const uint8_t *nn_bytes = (const uint8_t *)nn_in;
      uint32_t plane_size = STAI_NETWORK_IN_1_WIDTH * STAI_NETWORK_IN_1_HEIGHT;
      printf("TRACE: NN input: frame=%lu pitch=%lu align=0x%lX checksum64=0x%08lX sample=[%u %u %u %u] output0_len=%ld\n",
             (unsigned long) app_loop_trace_count,
             (unsigned long) pitch_nn,
             (unsigned long) (((uintptr_t)nn_in) & 31U),
             (unsigned long) SampleBytesChecksum32((const uint8_t *)nn_in, sample_len),
             (unsigned int) nn_bytes[0],
             (unsigned int) nn_bytes[1],
             (unsigned int) nn_bytes[plane_size],
             (unsigned int) nn_bytes[2U * plane_size],
             (long) nn_out_len[0]);
    }
#endif

#if NN_INPUT_NEEDS_PREPROC
    StartNNFrameCapture(nn_camera_capture, app_loop_trace_count + 1U);
#endif

    ret = RunInferenceWithTracing(app_loop_trace_count);
    assert(ret == 0);
    inference_ms = HAL_GetTick() - inference_start_ms;

    g_app_trace_stage = APP_TRACE_STAGE_POSTPROCESS;
    uint32_t postprocess_start_ms = HAL_GetTick();
    int32_t ret = app_postprocess_run((void **) nn_out, number_output, &pp_output, &pp_params);
    assert(ret == 0);
    postprocess_ms = HAL_GetTick() - postprocess_start_ms;

    g_app_trace_stage = APP_TRACE_STAGE_DISPLAY;
    uint32_t display_start_ms = HAL_GetTick();
    Display_NetworkOutput(&pp_output, inference_ms);
    /* Discard nn_out region (used by pp_input and pp_outputs variables) to avoid Dcache evictions during nn inference */
    for (int i = 0; i < number_output; i++)
    {
      void *tmp = nn_out[i];
      SCB_InvalidateDCache_by_Addr(tmp, nn_out_len[i]);
    }
    display_ms = HAL_GetTick() - display_start_ms;

#if NN_INPUT_NEEDS_PREPROC
    camera_wait_ms = WaitForNNFrameCapture(app_loop_trace_count + 1U);
#endif

    g_last_loop_ms = HAL_GetTick() - loop_start_ms;
    g_last_camera_wait_ms = camera_wait_ms;
    g_last_loop_fps_x10 = FpsX10FromMs(g_last_loop_ms);
    uint32_t headless_loop_ms = (g_last_loop_ms > display_ms) ? (g_last_loop_ms - display_ms) : g_last_loop_ms;
    uint32_t compute_ms = preprocess_ms + inference_ms + postprocess_ms;

#if APP_TRACE_DETECTIONS
    uint8_t trace_detection = (pp_output.nb_detect > 0) &&
                              ((g_last_detection_count != pp_output.nb_detect) ||
                               (app_loop_trace_count < 5U) ||
                               ((app_loop_trace_count % 10U) == 0U));

    g_last_detection_count = pp_output.nb_detect;

    if (trace_detection != 0U)
    {
      TraceDetections(app_loop_trace_count, &pp_output);
    }
#endif

#if APP_TRACE_PERFORMANCE
    if (ShouldTraceFrame(app_loop_trace_count) != 0U)
    {
      TraceFramePerformance(app_loop_trace_count, pp_output.nb_detect, g_last_loop_ms,
                            headless_loop_ms, inference_ms, compute_ms, preprocess_ms,
                            postprocess_ms, display_ms, camera_wait_ms);
    }
#endif

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
  Logger_Init();
  LOG_INFO(LOG_TAG_SYS, "console ready at 115200 baud");

  LOG_TRACE(LOG_TAG_SYS, "NPURam_enable begin");
  NPURam_enable();
  LOG_TRACE(LOG_TAG_SYS, "NPURam_enable OK");

  LOG_TRACE(LOG_TAG_SYS, "Fuse_Programming begin");
  Fuse_Programming();
  LOG_TRACE(LOG_TAG_SYS, "Fuse_Programming OK");

  LOG_TRACE(LOG_TAG_SYS, "NPUCache_config begin");
  NPUCache_config();
  LOG_TRACE(LOG_TAG_SYS, "NPUCache_config OK");

  /*** External NOR Flash *********************************************/
  LOG_TRACE(LOG_TAG_SYS, "external NOR init begin");
  BSP_XSPI_NOR_Init_t NOR_Init;
  NOR_Init.InterfaceMode = BSP_XSPI_NOR_OPI_MODE;
  NOR_Init.TransferRate = BSP_XSPI_NOR_DTR_TRANSFER;
  BSP_XSPI_NOR_Init(0, &NOR_Init);
  BSP_XSPI_NOR_EnableMemoryMappedMode(0);
  LOG_TRACE(LOG_TAG_SYS, "external NOR memory mapped OK");

  /* Set all required IPs as secure privileged */
  LOG_TRACE(LOG_TAG_SYS, "Security_Config begin");
  Security_Config();
  LOG_TRACE(LOG_TAG_SYS, "Security_Config OK");

  LOG_TRACE(LOG_TAG_SYS, "IAC_Config begin");
  IAC_Config();
  LOG_TRACE(LOG_TAG_SYS, "IAC_Config OK");
  set_clk_sleep_mode();
  LOG_TRACE(LOG_TAG_SYS, "sleep clock config OK");

}

static void NeuralNetwork_init(uint32_t *nn_in_length, stai_ptr *nn_out, stai_size *number_output, int32_t nn_out_len[])
{
  stai_network_info info;
  int ret;

  /* initialize runtime */
  ret = stai_runtime_init();
  assert(ret == STAI_SUCCESS);
  /* init model instance */
  ret = stai_network_init(network_context);
  assert(ret == STAI_SUCCESS);

  ret = stai_network_get_info(network_context, &info);
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
  assert(ret == STAI_SUCCESS);

  /* Get the output buffers size & address */
  ret = stai_network_get_outputs(network_context, nn_out, number_output);
  assert(ret == STAI_SUCCESS);
  g_number_output = *number_output;
  for (int i = 0; i < *number_output; i++)
  {
    nn_out_len[i] = info.outputs[i].size_bytes;
  }

  printf("TRACE: NN init: input=%lux%lux%lu bytes=%lu fmt=0x%08lX flags=0x%08lX scale=%.6f zp=%ld outputs=%lu out0_bytes=%ld out0_fmt=0x%08lX\n",
         (unsigned long) STAI_NETWORK_IN_1_WIDTH,
         (unsigned long) STAI_NETWORK_IN_1_HEIGHT,
         (unsigned long) STAI_NETWORK_IN_1_CHANNEL,
         (unsigned long) *nn_in_length,
         (unsigned long) info.inputs[0].format,
         (unsigned long) info.inputs[0].flags,
         nn_input_scale,
         (long) nn_input_zero_point,
         (unsigned long) *number_output,
         (long) nn_out_len[0],
         (unsigned long) info.outputs[0].format);
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

static float32_t Display_ClampF32(float32_t value, float32_t min_value, float32_t max_value)
{
  if (value < min_value)
  {
    return min_value;
  }
  if (value > max_value)
  {
    return max_value;
  }
  return value;
}

static void Display_DrawThickRect(uint32_t x0, uint32_t y0, uint32_t width, uint32_t height, uint32_t color)
{
  const uint32_t thickness = 3U;

  if ((width <= thickness) || (height <= thickness))
  {
    UTIL_LCD_DrawRect(x0, y0, width, height, color);
    return;
  }

  UTIL_LCD_FillRect(x0, y0, width, thickness, color);
  UTIL_LCD_FillRect(x0, y0 + height - thickness, width, thickness, color);
  UTIL_LCD_FillRect(x0, y0, thickness, height, color);
  UTIL_LCD_FillRect(x0 + width - thickness, y0, thickness, height, color);
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
  uint32_t drawn_rois = 0U;
  static uint32_t display_frame_count = 0U;
  uint32_t roi_area_width = lcd_fg_area.XSize;
  uint32_t roi_area_height = lcd_fg_area.YSize;
  uint32_t inference_fps_x10 = FpsX10FromMs(inference_ms);
  int ret;

  __disable_irq();
  ret = SCRL_SetAddress_NoReload(lcd_fg_buffer[lcd_fg_buffer_rd_idx], SCRL_LAYER_1);
  assert(ret == HAL_OK);
  __enable_irq();

  /* Draw bounding boxes */
  memset(lcd_fg_buffer, 0, sizeof(lcd_fg_buffer));
  UTIL_LCD_FillRect(0, 0, lcd_fg_area.XSize, lcd_fg_area.YSize, UTIL_LCD_COLOR_TRANSPARENT); /* Clear previous boxes */

  for (int32_t i = 0; i < nb_rois; i++)
  {
    uint32_t box_color = Display_GetBoxColor(rois[i].class_index);
    float32_t x1 = rois[i].x_center - (rois[i].width * 0.5f);
    float32_t y1 = rois[i].y_center - (rois[i].height * 0.5f);
    float32_t x2 = rois[i].x_center + (rois[i].width * 0.5f);
    float32_t y2 = rois[i].y_center + (rois[i].height * 0.5f);
    uint32_t x0;
    uint32_t y0;
    uint32_t width;
    uint32_t height;

    if ((x2 <= 0.0f) || (y2 <= 0.0f) || (x1 >= 1.0f) || (y1 >= 1.0f))
    {
      continue;
    }

    x1 = Display_ClampF32(x1, 0.0f, 1.0f);
    y1 = Display_ClampF32(y1, 0.0f, 1.0f);
    x2 = Display_ClampF32(x2, 0.0f, 1.0f);
    y2 = Display_ClampF32(y2, 0.0f, 1.0f);

    if (((x2 - x1) < 0.035f) || ((y2 - y1) < 0.035f))
    {
      continue;
    }

    x0 = (uint32_t)(x1 * ((float32_t)roi_area_width));
    y0 = (uint32_t)(y1 * ((float32_t)roi_area_height));
    width = (uint32_t)((x2 - x1) * ((float32_t)roi_area_width));
    height = (uint32_t)((y2 - y1) * ((float32_t)roi_area_height));

    if ((width < 3U) || (height < 3U))
    {
      continue;
    }

    if ((x0 + width) >= roi_area_width)
    {
      width = roi_area_width - x0 - 1U;
    }
    if ((y0 + height) >= roi_area_height)
    {
      height = roi_area_height - y0 - 1U;
    }

    UTIL_LCD_SetTextColor(box_color);
    Display_DrawThickRect(x0, y0, width, height, box_color);
    drawn_rois++;
  }

  UTIL_LCD_SetBackColor(0x40000000);
  UTIL_LCD_SetTextColor(UTIL_LCD_COLOR_WHITE);
  UTIL_LCDEx_PrintfAt(0, LINE(0), LEFT_MODE, "Inf %ums", inference_ms);
  UTIL_LCDEx_PrintfAt(0, LINE(1), LEFT_MODE, "NN %lu.%lufps",
                      (unsigned long)(inference_fps_x10 / 10U),
                      (unsigned long)(inference_fps_x10 % 10U));
  UTIL_LCDEx_PrintfAt(0, LINE(2), LEFT_MODE, "Loop %lu.%lufps",
                      (unsigned long)(g_last_loop_fps_x10 / 10U),
                      (unsigned long)(g_last_loop_fps_x10 % 10U));
  UTIL_LCDEx_PrintfAt(0, LINE(0), RIGHT_MODE, "Objects %u", drawn_rois);
  UTIL_LCDEx_PrintfAt(0, LINE(1), RIGHT_MODE, "Wait %lums", (unsigned long)g_last_camera_wait_ms);
  UTIL_LCD_SetBackColor(0);

  Display_WelcomeScreen();

  if (ShouldTraceFrame(display_frame_count) != 0U)
  {
    printf("TRACE: display: frame=%lu raw=%lu drawn=%lu fg=%lux%lu@%lu,%lu bg=%lux%lu@%lu,%lu\n",
           (unsigned long)display_frame_count,
           (unsigned long)nb_rois,
           (unsigned long)drawn_rois,
           (unsigned long)lcd_fg_area.XSize,
           (unsigned long)lcd_fg_area.YSize,
           (unsigned long)lcd_fg_area.X0,
           (unsigned long)lcd_fg_area.Y0,
           (unsigned long)lcd_bg_area.XSize,
           (unsigned long)lcd_bg_area.YSize,
           (unsigned long)lcd_bg_area.X0,
           (unsigned long)lcd_bg_area.Y0);
  }
  display_frame_count++;

  SCB_CleanDCache_by_Addr(lcd_fg_buffer[lcd_fg_buffer_rd_idx], LCD_FG_FRAMEBUFFER_SIZE);
  __disable_irq();
  ret = SCRL_ReloadLayer(SCRL_LAYER_1);
  assert(ret == HAL_OK);
  __enable_irq();
  lcd_fg_buffer_rd_idx = (lcd_fg_buffer_rd_idx + 1) % LCD_FG_BUFFER_COUNT;
}

static uint32_t Display_GetBoxColor(uint32_t class_index)
{
  if (class_index == 0U)
  {
    return UTIL_LCD_COLOR_GREEN;
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
