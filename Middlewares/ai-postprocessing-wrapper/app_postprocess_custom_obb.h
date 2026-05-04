/**
 ******************************************************************************
 * @file    app_postprocess_custom_obb.h
 * @author  OpenAI Codex
 *
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2026.
 * This file is provided as-is for project integration work.
 *
 ******************************************************************************
 */

#ifndef APP_POSTPROCESS_CUSTOM_OBB_H
#define APP_POSTPROCESS_CUSTOM_OBB_H

#include <stdint.h>

typedef struct
{
  float raw_output_scale;
  int16_t raw_output_zero_point;
  uint32_t input_width;
  uint32_t input_height;
  uint32_t total_boxes;
  uint32_t channels_per_box;
  uint32_t nb_classes;
  uint32_t max_boxes_limit;
  float conf_threshold;
  float iou_threshold;
  uint8_t output_is_channel_last;
} od_obb_custom_pp_static_param_t;

#endif /* APP_POSTPROCESS_CUSTOM_OBB_H */
