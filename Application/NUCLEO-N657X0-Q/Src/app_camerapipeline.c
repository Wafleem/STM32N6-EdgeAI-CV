 /**
 ******************************************************************************
 * @file    app_camerapipeline.c
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

#include <assert.h>
#include <stdio.h>
#include "cmw_camera.h"
#include "app_camerapipeline.h"
#include "app_config.h"
#include "crop_img.h"
#include "scrl.h"
#include "main.h"
#include "stai_network.h"


/* Leave the driver use the default resolution */
#define CAMERA_WIDTH 0
#define CAMERA_HEIGHT 0

extern int32_t cameraFrameReceived;
static uint32_t pipe2_vsync_trace_count;
static uint32_t pipe2_frame_trace_count;
static uint32_t pipe1_vsync_trace_count;
static uint32_t pipe1_frame_trace_count;

static void DCMIPP_PipeInitDisplay(CMW_CameraInit_t *camConf, uint32_t *layers_width[2], uint32_t *layers_height[2])
{
  CMW_Aspect_Ratio_Mode_t aspect_ratio;
  CMW_DCMIPP_Conf_t dcmipp_conf = {0};
  int ret;

  if (ASPECT_RATIO_MODE == ASPECT_RATIO_CROP)
  {
    aspect_ratio = CMW_Aspect_ratio_crop;
  }
  else if (ASPECT_RATIO_MODE == ASPECT_RATIO_FIT)
  {
    aspect_ratio = CMW_Aspect_ratio_fit;
  }
  else if (ASPECT_RATIO_MODE == ASPECT_RATIO_FULLSCREEN)
  {
    aspect_ratio = CMW_Aspect_ratio_fullscreen;
  }

  int lcd_bg_width;
  int lcd_bg_height;

  lcd_bg_height = (camConf->height <= SCREEN_HEIGHT) ? camConf->height : SCREEN_HEIGHT;

#if ASPECT_RATIO_MODE == ASPECT_RATIO_FULLSCREEN
  lcd_bg_width = (((camConf->width*lcd_bg_height)/camConf->height) - ((camConf->width*lcd_bg_height)/camConf->height) % 16);
#else
  lcd_bg_width = (camConf->height <= SCREEN_HEIGHT) ? camConf->height : SCREEN_HEIGHT;
#endif

  *layers_width[0] = lcd_bg_width;
  *layers_height[0] = lcd_bg_height;
  *layers_width[1] = lcd_bg_width;
  *layers_height[1] = lcd_bg_height;

  dcmipp_conf.output_width = lcd_bg_width;
  dcmipp_conf.output_height = lcd_bg_height;
  dcmipp_conf.output_format = DCMIPP_PIXEL_PACKER_FORMAT_RGB565_1;
  dcmipp_conf.output_bpp = 2;
  dcmipp_conf.mode = aspect_ratio;
  dcmipp_conf.enable_gamma_conversion = 0;
  uint32_t pitch;
  printf("TRACE: DCMIPP_PipeInitDisplay: request output=%dx%d bpp=%lu mode=%d\n",
         lcd_bg_width, lcd_bg_height, (unsigned long) dcmipp_conf.output_bpp, aspect_ratio);
  ret = CMW_CAMERA_SetPipeConfig(DCMIPP_PIPE1, &dcmipp_conf, &pitch);
  printf("TRACE: DCMIPP_PipeInitDisplay: CMW_CAMERA_SetPipeConfig pipe=1 ret=%d pitch=%lu\n",
         ret, (unsigned long) pitch);
  assert(ret == HAL_OK);
  assert(dcmipp_conf.output_width * dcmipp_conf.output_bpp == pitch);
}

static void DCMIPP_PipeInitNn(uint32_t *pitch)
{
  CMW_Aspect_Ratio_Mode_t aspect_ratio;
  CMW_DCMIPP_Conf_t dcmipp_conf;
  int ret;

  if (ASPECT_RATIO_MODE == ASPECT_RATIO_CROP)
  {
    aspect_ratio = CMW_Aspect_ratio_crop;
  }
  else if (ASPECT_RATIO_MODE == ASPECT_RATIO_FIT)
  {
    aspect_ratio = CMW_Aspect_ratio_fit;
  }
  else if (ASPECT_RATIO_MODE == ASPECT_RATIO_FULLSCREEN)
  {
    aspect_ratio = CMW_Aspect_ratio_fit;
  }

  dcmipp_conf.output_width = STAI_NETWORK_IN_1_WIDTH;
  dcmipp_conf.output_height = STAI_NETWORK_IN_1_HEIGHT;
  dcmipp_conf.output_format = DCMIPP_PIXEL_PACKER_FORMAT_RGB888_YUV444_1;
  dcmipp_conf.output_bpp = STAI_NETWORK_IN_1_CHANNEL;
  dcmipp_conf.mode = aspect_ratio;
  dcmipp_conf.enable_swap = COLOR_MODE;
  dcmipp_conf.enable_gamma_conversion = 0;
  printf("TRACE: DCMIPP_PipeInitNn: request output=%lux%lu channels=%lu mode=%d\n",
         (unsigned long) dcmipp_conf.output_width,
         (unsigned long) dcmipp_conf.output_height,
         (unsigned long) dcmipp_conf.output_bpp,
         aspect_ratio);
  ret = CMW_CAMERA_SetPipeConfig(DCMIPP_PIPE2, &dcmipp_conf, pitch);
  printf("TRACE: DCMIPP_PipeInitNn: CMW_CAMERA_SetPipeConfig pipe=2 ret=%d pitch=%lu\n",
         ret, (unsigned long) *pitch);
  assert(ret == HAL_OK);
}

/**
* @brief Init the camera and the 2 DCMIPP pipes
* @param lcd_bg_width display width
* @param lcd_bg_height display height
* @param pitch_nn output pitch computed by the CMW
*/
void CameraPipeline_Init(uint32_t *layers_width[2], uint32_t *layers_height[2], uint32_t *pitch_nn)
{
  int ret;
  CMW_CameraInit_t cam_conf;

  cam_conf.width = CAMERA_WIDTH;
  cam_conf.height = CAMERA_HEIGHT;
  cam_conf.fps = CAMERA_FPS;
  cam_conf.mirror_flip = CAMERA_FLIP;

  printf("TRACE: CameraPipeline_Init: CMW_CAMERA_Init begin width=%lu height=%lu fps=%lu mirror_flip=%lu\n",
         (unsigned long) cam_conf.width,
         (unsigned long) cam_conf.height,
         (unsigned long) cam_conf.fps,
         (unsigned long) cam_conf.mirror_flip);
  ret = CMW_CAMERA_Init(&cam_conf, NULL);
  printf("TRACE: CameraPipeline_Init: CMW_CAMERA_Init ret=%d resolved_width=%lu resolved_height=%lu\n",
         ret, (unsigned long) cam_conf.width, (unsigned long) cam_conf.height);
  assert(ret == CMW_ERROR_NONE);
  DCMIPP_PipeInitDisplay(&cam_conf, layers_width, layers_height);
  DCMIPP_PipeInitNn(pitch_nn);
  printf("TRACE: CameraPipeline_Init: complete\n");
}

void CameraPipeline_DeInit(void)
{
  int ret;
  ret = CMW_CAMERA_DeInit();
  assert(ret == CMW_ERROR_NONE);
}

void CameraPipeline_DisplayPipe_Start(uint8_t *display_pipe_dst, uint32_t cam_mode)
{
  int ret;
  ret = CMW_CAMERA_Start(DCMIPP_PIPE1, display_pipe_dst, cam_mode);
  printf("TRACE: CameraPipeline_DisplayPipe_Start: ret=%d dst=%p mode=%lu\n",
         ret, display_pipe_dst, (unsigned long) cam_mode);
  assert(ret == CMW_ERROR_NONE);
}

void CameraPipeline_NNPipe_Start(uint8_t *nn_pipe_dst, uint32_t cam_mode)
{
  int ret;
  static uint32_t trace_count = 0;

  ret = CMW_CAMERA_Start(DCMIPP_PIPE2, nn_pipe_dst, cam_mode);
  if ((trace_count < 5U) || (ret != CMW_ERROR_NONE))
  {
    printf("TRACE: CameraPipeline_NNPipe_Start: ret=%d dst=%p mode=%lu count=%lu\n",
           ret, nn_pipe_dst, (unsigned long) cam_mode, (unsigned long) trace_count);
  }
  trace_count++;
  assert(ret == CMW_ERROR_NONE);
}

void CameraPipeline_DisplayPipe_Stop()
{
  int ret;
  ret = CMW_CAMERA_Suspend(DCMIPP_PIPE1);
  assert(ret == CMW_ERROR_NONE);
}

void CameraPipeline_IspUpdate(void)
{
  int ret = CMW_ERROR_NONE;
  ret = CMW_CAMERA_Run();
  if (ret != CMW_ERROR_NONE)
  {
    printf("ERROR: CameraPipeline_IspUpdate: CMW_CAMERA_Run ret=%d\n", ret);
  }
  assert(ret == CMW_ERROR_NONE);
}

/**
  * @brief  Frame event callback
  * @param  hdcmipp pointer to the DCMIPP handle
  * @retval None
  */
int CMW_CAMERA_PIPE_FrameEventCallback(uint32_t pipe)
{
  int ret;

  switch (pipe)
  {
    case DCMIPP_PIPE1 :
      if (pipe1_frame_trace_count < 5U)
      {
        printf("TRACE: CMW_CAMERA_PIPE_FrameEventCallback: pipe=%lu count=%lu\n",
               (unsigned long) pipe,
               (unsigned long) pipe1_frame_trace_count);
      }
      pipe1_frame_trace_count++;
      break;

    case DCMIPP_PIPE2 :
      if (pipe2_frame_trace_count < 5U)
      {
        printf("TRACE: CMW_CAMERA_PIPE_FrameEventCallback: pipe=%lu count=%lu\n",
               (unsigned long) pipe,
               (unsigned long) pipe2_frame_trace_count);
      }
      pipe2_frame_trace_count++;
      cameraFrameReceived++;
      Display_InvalidateCameraBuffer();

      ret = SRCL_Update();
      if ((pipe2_frame_trace_count <= 5U) || (ret != 0))
      {
        printf("TRACE: CMW_CAMERA_PIPE_FrameEventCallback: SRCL_Update ret=%d\n", ret);
      }
      assert(ret == 0);
      break;
  }
  return 0;
}

int CMW_CAMERA_PIPE_VsyncEventCallback(uint32_t pipe)
{
  if ((pipe == DCMIPP_PIPE1) && (pipe1_vsync_trace_count < 5U))
  {
    printf("TRACE: CMW_CAMERA_PIPE_VsyncEventCallback: pipe=%lu count=%lu\n",
           (unsigned long) pipe,
           (unsigned long) pipe1_vsync_trace_count);
    pipe1_vsync_trace_count++;
  }

  if ((pipe == DCMIPP_PIPE2) && (pipe2_vsync_trace_count < 5U))
  {
    printf("TRACE: CMW_CAMERA_PIPE_VsyncEventCallback: pipe=%lu count=%lu\n",
           (unsigned long) pipe,
           (unsigned long) pipe2_vsync_trace_count);
    pipe2_vsync_trace_count++;
  }

  return 0;
}

void CMW_CAMERA_PIPE_ErrorCallback(uint32_t pipe)
{
  DCMIPP_HandleTypeDef *hcamera_dcmipp = CMW_CAMERA_GetDCMIPPHandle();
  printf("ERROR: CMW_CAMERA_PIPE_ErrorCallback: pipe=%lu dcmipp_state=%lu pipe1_state=%lu pipe2_state=%lu err=0x%08lX cmsr1=0x%08lX cmsr2=0x%08lX cmier=0x%08lX\n",
         (unsigned long) pipe,
         (unsigned long) HAL_DCMIPP_GetState(hcamera_dcmipp),
         (unsigned long) HAL_DCMIPP_PIPE_GetState(hcamera_dcmipp, DCMIPP_PIPE1),
         (unsigned long) HAL_DCMIPP_PIPE_GetState(hcamera_dcmipp, DCMIPP_PIPE2),
         (unsigned long) HAL_DCMIPP_GetError(hcamera_dcmipp),
         (unsigned long) hcamera_dcmipp->Instance->CMSR1,
         (unsigned long) hcamera_dcmipp->Instance->CMSR2,
         (unsigned long) hcamera_dcmipp->Instance->CMIER);
}
