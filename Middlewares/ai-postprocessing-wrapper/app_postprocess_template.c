 /**
 ******************************************************************************
 * @file    app_postprocess_template.c
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

#include "app_postprocess.h"
#include "app_config.h"

#if POSTPROCESS_TYPE == POSTPROCESS_CUSTOM
#include <assert.h>
#include <math.h>
#include <string.h>

#ifndef AI_OD_OBB_PP_NB_CLASSES
  #error "AI_OD_OBB_PP_NB_CLASSES must be defined for POSTPROCESS_CUSTOM."
#endif

#ifndef AI_OD_OBB_PP_TOTAL_BOXES
  #error "AI_OD_OBB_PP_TOTAL_BOXES must be defined for POSTPROCESS_CUSTOM."
#endif

#ifndef AI_OD_OBB_PP_MAX_BOXES_LIMIT
  #error "AI_OD_OBB_PP_MAX_BOXES_LIMIT must be defined for POSTPROCESS_CUSTOM."
#endif

#ifndef AI_OD_OBB_PP_CANDIDATES_LIMIT
  #error "AI_OD_OBB_PP_CANDIDATES_LIMIT must be defined for POSTPROCESS_CUSTOM."
#endif

#ifndef AI_OD_OBB_PP_IGNORE_CLASS_INDEX
  #define AI_OD_OBB_PP_IGNORE_CLASS_INDEX (0xFFFFFFFFU)
#endif

typedef struct
{
  float32_t x1;
  float32_t y1;
  float32_t x2;
  float32_t y2;
  float32_t conf;
  int32_t class_index;
} obb_candidate_t;

POSTPROCESS_WRAPPER_SECTION
static od_pp_outBuffer_t out_detections[AI_OD_OBB_PP_MAX_BOXES_LIMIT];
POSTPROCESS_WRAPPER_SECTION
static obb_candidate_t shortlist[AI_OD_OBB_PP_CANDIDATES_LIMIT];

static float32_t clamp_f32(float32_t value, float32_t min_value, float32_t max_value)
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

static uint32_t tensor_height(const stai_tensor *tensor)
{
  const int32_t *dims = tensor->shape.data;
  uint32_t shape_size = tensor->shape.size;

  if ((tensor->flags & STAI_FLAG_HAS_BATCH) && (shape_size >= 4U))
  {
    if (tensor->flags & STAI_FLAG_CHANNEL_FIRST)
    {
      return (uint32_t)dims[2];
    }
    return (uint32_t)dims[1];
  }

  if (shape_size >= 3U)
  {
    if (tensor->flags & STAI_FLAG_CHANNEL_FIRST)
    {
      return (uint32_t)dims[1];
    }
    return (uint32_t)dims[0];
  }

  return 0U;
}

static uint32_t tensor_width(const stai_tensor *tensor)
{
  const int32_t *dims = tensor->shape.data;
  uint32_t shape_size = tensor->shape.size;

  if ((tensor->flags & STAI_FLAG_HAS_BATCH) && (shape_size >= 4U))
  {
    if (tensor->flags & STAI_FLAG_CHANNEL_FIRST)
    {
      return (uint32_t)dims[3];
    }
    return (uint32_t)dims[2];
  }

  if (shape_size >= 3U)
  {
    if (tensor->flags & STAI_FLAG_CHANNEL_FIRST)
    {
      return (uint32_t)dims[2];
    }
    return (uint32_t)dims[1];
  }

  return 0U;
}

static int32_t select_output_layout(const stai_tensor *tensor,
                                    od_obb_custom_pp_static_param_t *params)
{
  uint32_t shape_size = tensor->shape.size;
  uint32_t start_dim;
  uint32_t channels;
  uint32_t boxes;
  uint32_t dim_index;

  if (shape_size < 2U)
  {
    return AI_OD_POSTPROCESS_ERROR;
  }

  start_dim = (tensor->flags & STAI_FLAG_HAS_BATCH) ? 1U : 0U;
  if ((shape_size - start_dim) < 2U)
  {
    return AI_OD_POSTPROCESS_ERROR;
  }

  boxes = 1U;
  if (tensor->flags & STAI_FLAG_CHANNEL_FIRST)
  {
    channels = (uint32_t)tensor->shape.data[start_dim];
    params->output_is_channel_last = 0U;

    for (dim_index = start_dim + 1U; dim_index < shape_size; dim_index++)
    {
      boxes *= (uint32_t)tensor->shape.data[dim_index];
    }
  }
  else
  {
    params->output_is_channel_last = 1U;

    for (dim_index = start_dim; dim_index < (shape_size - 1U); dim_index++)
    {
      boxes *= (uint32_t)tensor->shape.data[dim_index];
    }
    channels = (uint32_t)tensor->shape.data[shape_size - 1U];
  }

  if ((channels < 6U) || (boxes == 0U))
  {
    return AI_OD_POSTPROCESS_ERROR;
  }

  if (boxes > AI_OD_OBB_PP_TOTAL_BOXES)
  {
    return AI_OD_POSTPROCESS_ERROR;
  }

  params->channels_per_box = channels;
  params->total_boxes = boxes;
  params->nb_classes = channels - 5U;

  if (params->nb_classes != AI_OD_OBB_PP_NB_CLASSES)
  {
    return AI_OD_POSTPROCESS_ERROR;
  }

  return AI_OD_POSTPROCESS_ERROR_NO;
}

static float32_t tensor_value(const int8_t *tensor,
                              const od_obb_custom_pp_static_param_t *params,
                              uint32_t box_index,
                              uint32_t channel_index)
{
  uint32_t linear_index;
  int32_t raw_value;

  if (params->output_is_channel_last != 0U)
  {
    linear_index = (box_index * params->channels_per_box) + channel_index;
  }
  else
  {
    linear_index = (channel_index * params->total_boxes) + box_index;
  }

  raw_value = tensor[linear_index];
  return ((float32_t)raw_value - (float32_t)params->raw_output_zero_point) * params->raw_output_scale;
}

static float32_t box_iou(const obb_candidate_t *a, const obb_candidate_t *b)
{
  float32_t inter_x1 = a->x1 > b->x1 ? a->x1 : b->x1;
  float32_t inter_y1 = a->y1 > b->y1 ? a->y1 : b->y1;
  float32_t inter_x2 = a->x2 < b->x2 ? a->x2 : b->x2;
  float32_t inter_y2 = a->y2 < b->y2 ? a->y2 : b->y2;
  float32_t inter_w = inter_x2 - inter_x1;
  float32_t inter_h = inter_y2 - inter_y1;
  float32_t inter_area;
  float32_t area_a;
  float32_t area_b;
  float32_t union_area;

  if ((inter_w <= 0.0f) || (inter_h <= 0.0f))
  {
    return 0.0f;
  }

  inter_area = inter_w * inter_h;
  area_a = (a->x2 - a->x1) * (a->y2 - a->y1);
  area_b = (b->x2 - b->x1) * (b->y2 - b->y1);
  union_area = area_a + area_b - inter_area;

  if (union_area <= 0.0f)
  {
    return 0.0f;
  }

  return inter_area / union_area;
}

static void insert_candidate(const obb_candidate_t *candidate, uint32_t *candidate_count)
{
  uint32_t insert_at;
  uint32_t limit = AI_OD_OBB_PP_CANDIDATES_LIMIT;

  if (*candidate_count < limit)
  {
    insert_at = *candidate_count;
    (*candidate_count)++;
  }
  else
  {
    if (candidate->conf <= shortlist[limit - 1U].conf)
    {
      return;
    }
    insert_at = limit - 1U;
  }

  while ((insert_at > 0U) && (shortlist[insert_at - 1U].conf < candidate->conf))
  {
    shortlist[insert_at] = shortlist[insert_at - 1U];
    insert_at--;
  }

  shortlist[insert_at] = *candidate;
}

static int32_t build_candidate(const int8_t *tensor,
                               const od_obb_custom_pp_static_param_t *params,
                               uint32_t box_index,
                               obb_candidate_t *candidate)
{
  float32_t cx = tensor_value(tensor, params, box_index, 0U);
  float32_t cy = tensor_value(tensor, params, box_index, 1U);
  float32_t width = tensor_value(tensor, params, box_index, 2U);
  float32_t height = tensor_value(tensor, params, box_index, 3U);
  float32_t angle = tensor_value(tensor, params, box_index, params->channels_per_box - 1U);
  float32_t best_conf = -1.0f;
  int32_t best_class = -1;
  float32_t cos_a;
  float32_t sin_a;
  float32_t min_x = 1.0e30f;
  float32_t min_y = 1.0e30f;
  float32_t max_x = -1.0e30f;
  float32_t max_y = -1.0e30f;
  float32_t offsets[4][2] = {
      {-0.5f, -0.5f},
      { 0.5f, -0.5f},
      { 0.5f,  0.5f},
      {-0.5f,  0.5f},
  };
  uint32_t class_idx;
  uint32_t point_idx;

  if ((width <= 0.0f) || (height <= 0.0f))
  {
    return AI_OD_POSTPROCESS_ERROR;
  }

  for (class_idx = 0U; class_idx < params->nb_classes; class_idx++)
  {
    float32_t conf = tensor_value(tensor, params, box_index, 4U + class_idx);
    if (conf > best_conf)
    {
      best_conf = conf;
      best_class = (int32_t)class_idx;
    }
  }

  if ((best_class < 0) || (best_conf < params->conf_threshold))
  {
    return AI_OD_POSTPROCESS_ERROR;
  }

  if ((uint32_t)best_class == AI_OD_OBB_PP_IGNORE_CLASS_INDEX)
  {
    return AI_OD_POSTPROCESS_ERROR;
  }

  cos_a = cosf(angle);
  sin_a = sinf(angle);

  for (point_idx = 0U; point_idx < 4U; point_idx++)
  {
    float32_t dx = offsets[point_idx][0] * width;
    float32_t dy = offsets[point_idx][1] * height;
    float32_t px = cx + (dx * cos_a) - (dy * sin_a);
    float32_t py = cy + (dx * sin_a) + (dy * cos_a);

    min_x = px < min_x ? px : min_x;
    min_y = py < min_y ? py : min_y;
    max_x = px > max_x ? px : max_x;
    max_y = py > max_y ? py : max_y;
  }

  min_x = clamp_f32(min_x, 0.0f, (float32_t)params->input_width);
  min_y = clamp_f32(min_y, 0.0f, (float32_t)params->input_height);
  max_x = clamp_f32(max_x, 0.0f, (float32_t)params->input_width);
  max_y = clamp_f32(max_y, 0.0f, (float32_t)params->input_height);

  if ((max_x <= min_x) || (max_y <= min_y))
  {
    return AI_OD_POSTPROCESS_ERROR;
  }

  candidate->x1 = min_x / (float32_t)params->input_width;
  candidate->y1 = min_y / (float32_t)params->input_height;
  candidate->x2 = max_x / (float32_t)params->input_width;
  candidate->y2 = max_y / (float32_t)params->input_height;
  candidate->conf = best_conf;
  candidate->class_index = best_class;

  return AI_OD_POSTPROCESS_ERROR_NO;
}

int32_t app_postprocess_init(void *params_postprocess, stai_network_info *NN_Info)
{
  int32_t error;
  od_obb_custom_pp_static_param_t *params = (od_obb_custom_pp_static_param_t *)params_postprocess;

  assert(params != NULL);
  assert(NN_Info != NULL);
  assert(NN_Info->n_inputs == 1U);
  assert(NN_Info->n_outputs == 1U);

  memset(params, 0, sizeof(*params));
  params->raw_output_scale = NN_Info->outputs[0].scale.data[0];
  params->raw_output_zero_point = NN_Info->outputs[0].zeropoint.data[0];
  params->input_width = tensor_width(&NN_Info->inputs[0]);
  params->input_height = tensor_height(&NN_Info->inputs[0]);
  params->max_boxes_limit = AI_OD_OBB_PP_MAX_BOXES_LIMIT;
  params->conf_threshold = AI_OD_OBB_PP_CONF_THRESHOLD;
  params->iou_threshold = AI_OD_OBB_PP_IOU_THRESHOLD;

  if ((params->input_width == 0U) || (params->input_height == 0U))
  {
    return AI_OD_POSTPROCESS_ERROR;
  }

  error = select_output_layout(&NN_Info->outputs[0], params);
  return error;
}

int32_t app_postprocess_run(void *pInput[], int nb_input, void *pOutput, void *pInput_param)
{
  const int8_t *tensor = (const int8_t *)pInput[0];
  od_pp_out_t *output = (od_pp_out_t *)pOutput;
  const od_obb_custom_pp_static_param_t *params = (const od_obb_custom_pp_static_param_t *)pInput_param;
  uint32_t candidate_count = 0U;
  uint32_t box_index;

  assert(nb_input == 1);
  assert(tensor != NULL);
  assert(output != NULL);
  assert(params != NULL);

  output->pOutBuff = out_detections;
  output->nb_detect = 0;

  for (box_index = 0U; box_index < params->total_boxes; box_index++)
  {
    obb_candidate_t candidate;
    if (build_candidate(tensor, params, box_index, &candidate) == AI_OD_POSTPROCESS_ERROR_NO)
    {
      insert_candidate(&candidate, &candidate_count);
    }
  }

  for (box_index = 0U; box_index < candidate_count; box_index++)
  {
    uint32_t kept_index;
    uint8_t keep = 1U;

    for (kept_index = 0U; kept_index < (uint32_t)output->nb_detect; kept_index++)
    {
      obb_candidate_t kept = {
          .x1 = out_detections[kept_index].x_center - (out_detections[kept_index].width * 0.5f),
          .y1 = out_detections[kept_index].y_center - (out_detections[kept_index].height * 0.5f),
          .x2 = out_detections[kept_index].x_center + (out_detections[kept_index].width * 0.5f),
          .y2 = out_detections[kept_index].y_center + (out_detections[kept_index].height * 0.5f),
          .class_index = out_detections[kept_index].class_index,
      };

      if (kept.class_index != shortlist[box_index].class_index)
      {
        continue;
      }

      if (box_iou(&shortlist[box_index], &kept) > params->iou_threshold)
      {
        keep = 0U;
        break;
      }
    }

    if (keep == 0U)
    {
      continue;
    }

    out_detections[output->nb_detect].x_center = (shortlist[box_index].x1 + shortlist[box_index].x2) * 0.5f;
    out_detections[output->nb_detect].y_center = (shortlist[box_index].y1 + shortlist[box_index].y2) * 0.5f;
    out_detections[output->nb_detect].width = shortlist[box_index].x2 - shortlist[box_index].x1;
    out_detections[output->nb_detect].height = shortlist[box_index].y2 - shortlist[box_index].y1;
    out_detections[output->nb_detect].conf = shortlist[box_index].conf;
    out_detections[output->nb_detect].class_index = shortlist[box_index].class_index;
    output->nb_detect++;

    if ((uint32_t)output->nb_detect >= params->max_boxes_limit)
    {
      break;
    }
  }

  return AI_OD_POSTPROCESS_ERROR_NO;
}
#endif
