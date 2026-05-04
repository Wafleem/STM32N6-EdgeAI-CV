 /**
 ******************************************************************************
 * @file    app_config.h
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

#ifndef APP_CONFIG
#define APP_CONFIG

#include "arm_math.h"

#define USE_DCACHE

/*Defines: CMW_MIRRORFLIP_NONE; CMW_MIRRORFLIP_FLIP; CMW_MIRRORFLIP_MIRROR; CMW_MIRRORFLIP_FLIP_MIRROR;*/
#define CAMERA_FLIP CMW_MIRRORFLIP_NONE

#define ASPECT_RATIO_CROP       (1) /* Crop both pipes to nn input aspect ratio; Original aspect ratio kept */
#define ASPECT_RATIO_FIT        (2) /* Resize both pipe to NN input aspect ratio; Original aspect ratio not kept */
#define ASPECT_RATIO_FULLSCREEN (3) /* Resize camera image to NN input size and display a maximized image. See Doc/Build-Options.md#aspect-ratio-mode */

#define APP_MODEL_PROFILE_GENERIC   (0)
#define APP_MODEL_PROFILE_SAFAL_OBB (1)

#ifndef APP_MODEL_PROFILE
  #define APP_MODEL_PROFILE APP_MODEL_PROFILE_SAFAL_OBB
#endif

#if APP_MODEL_PROFILE == APP_MODEL_PROFILE_GENERIC

#define ASPECT_RATIO_MODE ASPECT_RATIO_FULLSCREEN

/* Model Related Info */
#define POSTPROCESS_TYPE POSTPROCESS_OD_YOLO_V2_UI

#define COLOR_BGR (0)
#define COLOR_RGB (1)
#define COLOR_MODE COLOR_RGB

#define NB_CLASSES 2
#define CLASSES_TABLE const char* classes_table[NB_CLASSES] = {\
"person",   "not_person"}

/* I/O configuration */
#define AI_OD_YOLOV2_PP_NB_CLASSES        (1)
#define AI_OD_YOLOV2_PP_NB_ANCHORS        (5)
#define AI_OD_YOLOV2_PP_GRID_WIDTH        (7)
#define AI_OD_YOLOV2_PP_GRID_HEIGHT       (7)
#define AI_OD_YOLOV2_PP_NB_INPUT_BOXES    (AI_OD_YOLOV2_PP_GRID_WIDTH * AI_OD_YOLOV2_PP_GRID_HEIGHT)

/* Anchor boxes */
static const float32_t AI_OD_YOLOV2_PP_ANCHORS[2*AI_OD_YOLOV2_PP_NB_ANCHORS] = {
    0.9883000000f,     3.3606000000f,
    2.1194000000f,     5.3759000000f,
    3.0520000000f,     9.1336000000f,
    5.5517000000f,     9.3066000000f,
    9.7260000000f,     11.1422000000f,
  };

/* --------  Tuning below can be modified by the application --------- */
#define AI_OD_YOLOV2_PP_CONF_THRESHOLD    (0.6f)
#define AI_OD_YOLOV2_PP_IOU_THRESHOLD     (0.3f)
#define AI_OD_YOLOV2_PP_MAX_BOXES_LIMIT   (10)

/* Display */
#define WELCOME_MSG_1         "quantized_tiny_yolo_v2_224_.tflite"
#define WELCOME_MSG_2         ((char *[2]) {"Model Running in STM32 MCU", "internal memory"})

#elif APP_MODEL_PROFILE == APP_MODEL_PROFILE_SAFAL_OBB

#define ASPECT_RATIO_MODE ASPECT_RATIO_CROP

/* Model Related Info */
#define POSTPROCESS_TYPE POSTPROCESS_CUSTOM

#define COLOR_BGR (0)
#define COLOR_RGB (1)
#define COLOR_MODE COLOR_RGB

#define NB_CLASSES 2
#define CLASSES_TABLE const char* classes_table[NB_CLASSES] = {\
"blue", "red"}

/* Safal Jetson model lineage: best-roboflow-nitish-obb exported at 320x320. */
#define AI_OD_OBB_PP_NB_CLASSES         (NB_CLASSES)
#define AI_OD_OBB_PP_TOTAL_BOXES        (2100)
#define AI_OD_OBB_PP_CANDIDATES_LIMIT   (160)
#define AI_OD_OBB_PP_CONF_THRESHOLD     (0.45f)
#define AI_OD_OBB_PP_IOU_THRESHOLD      (0.35f)
#define AI_OD_OBB_PP_MAX_BOXES_LIMIT    (20)

/* Display */
#define WELCOME_MSG_1         "Nitish Safal OBB 320"
#define WELCOME_MSG_2         ((char *[2]) {"blue/red armor from Safal Jetson lineage", "Regenerate Model/* from the Nitish checkpoint"})

#else
  #error "Unsupported APP_MODEL_PROFILE value."
#endif

#endif
