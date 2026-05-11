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
#include <stdio.h>
#include <string.h>

#ifndef M_PI
  #define M_PI 3.14159265358979323846
#endif

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

#ifndef AI_OD_OBB_PP_REG_MAX
  #define AI_OD_OBB_PP_REG_MAX (16U)
#endif

#ifndef AI_OD_OBB_PP_OUTPUT_IS_RAW_YOLO26
  #define AI_OD_OBB_PP_OUTPUT_IS_RAW_YOLO26 (0U)
#endif

typedef struct
{
  float32_t x;
  float32_t y;
} obb_point_t;

typedef struct
{
  float32_t x1;
  float32_t y1;
  float32_t x2;
  float32_t y2;
  obb_point_t corners[4];
  float32_t conf;
  int32_t class_index;
} obb_candidate_t;

enum
{
  OBB_REJECT_NONE = 0,
  OBB_REJECT_ANCHOR,
  OBB_REJECT_DIMS,
  OBB_REJECT_CONF,
  OBB_REJECT_IGNORE_CLASS,
  OBB_REJECT_CLAMP
};

POSTPROCESS_WRAPPER_SECTION
static od_pp_outBuffer_t out_detections[AI_OD_OBB_PP_MAX_BOXES_LIMIT];
POSTPROCESS_WRAPPER_SECTION
static obb_candidate_t shortlist[AI_OD_OBB_PP_CANDIDATES_LIMIT];
POSTPROCESS_WRAPPER_SECTION
static obb_candidate_t kept_candidates[AI_OD_OBB_PP_MAX_BOXES_LIMIT];

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
  params->reg_max = AI_OD_OBB_PP_REG_MAX;
  params->output_is_raw_dfl = 0U;
  params->output_is_raw_yolo26 = 0U;

  if (channels == (AI_OD_OBB_PP_NB_CLASSES + 5U))
  {
    params->nb_classes = channels - 5U;
    params->output_is_raw_yolo26 = (uint8_t)AI_OD_OBB_PP_OUTPUT_IS_RAW_YOLO26;
  }
  else if (channels == ((4U * AI_OD_OBB_PP_REG_MAX) + AI_OD_OBB_PP_NB_CLASSES + 1U))
  {
    params->nb_classes = AI_OD_OBB_PP_NB_CLASSES;
    params->output_is_raw_dfl = 1U;
  }
  else
  {
    return AI_OD_POSTPROCESS_ERROR;
  }

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

static float32_t sigmoid_f32(float32_t value);

static float32_t tensor_class_confidence(const int8_t *tensor,
                                         const od_obb_custom_pp_static_param_t *params,
                                         uint32_t box_index,
                                         uint32_t class_index)
{
  if ((params->output_is_raw_dfl != 0U) || (params->output_is_raw_yolo26 != 0U))
  {
    uint32_t class_offset = (params->output_is_raw_dfl != 0U) ? (4U * params->reg_max) : 4U;
    return sigmoid_f32(tensor_value(tensor, params, box_index, class_offset + class_index));
  }

  return tensor_value(tensor, params, box_index, 4U + class_index);
}

static float32_t sigmoid_f32(float32_t value)
{
  if (value >= 0.0f)
  {
    float32_t z = expf(-value);
    return 1.0f / (1.0f + z);
  }
  else
  {
    float32_t z = expf(value);
    return z / (1.0f + z);
  }
}

static float32_t dfl_expectation(const int8_t *tensor,
                                 const od_obb_custom_pp_static_param_t *params,
                                 uint32_t box_index,
                                 uint32_t channel_offset)
{
  float32_t max_logit = -1.0e30f;
  float32_t denom = 0.0f;
  float32_t weighted_sum = 0.0f;
  uint32_t bin_index;

  for (bin_index = 0U; bin_index < params->reg_max; bin_index++)
  {
    float32_t logit = tensor_value(tensor, params, box_index, channel_offset + bin_index);
    if (logit > max_logit)
    {
      max_logit = logit;
    }
  }

  for (bin_index = 0U; bin_index < params->reg_max; bin_index++)
  {
    float32_t logit = tensor_value(tensor, params, box_index, channel_offset + bin_index);
    float32_t probability = expf(logit - max_logit);
    denom += probability;
    weighted_sum += probability * (float32_t)bin_index;
  }

  if (denom <= 0.0f)
  {
    return 0.0f;
  }

  return weighted_sum / denom;
}

static uint32_t yolo_grid_total_for_size(uint32_t width, uint32_t height)
{
  static const uint32_t strides[] = {8U, 16U, 32U};
  uint32_t total_boxes = 0U;
  uint32_t level_index;

  if ((width == 0U) || (height == 0U))
  {
    return 0U;
  }

  for (level_index = 0U; level_index < (sizeof(strides) / sizeof(strides[0])); level_index++)
  {
    uint32_t level_stride = strides[level_index];
    total_boxes += (width / level_stride) * (height / level_stride);
  }

  return total_boxes;
}

static uint32_t infer_square_input_dim_from_bytes(uint32_t size_bytes, uint32_t channels)
{
  uint32_t pixels;
  uint32_t dim;

  if ((size_bytes == 0U) || (channels == 0U) || ((size_bytes % channels) != 0U))
  {
    return 0U;
  }

  pixels = size_bytes / channels;
  for (dim = 16U; dim <= 2048U; dim++)
  {
    uint32_t square = dim * dim;
    if (square == pixels)
    {
      return dim;
    }
    if (square > pixels)
    {
      break;
    }
  }

  return 0U;
}

static int32_t yolo_anchor_for_box(const od_obb_custom_pp_static_param_t *params,
                                   uint32_t box_index,
                                   float32_t *anchor_x,
                                   float32_t *anchor_y,
                                   float32_t *stride)
{
  static const uint32_t strides[] = {8U, 16U, 32U};
  uint32_t remaining = box_index;
  uint32_t level_index;

  for (level_index = 0U; level_index < (sizeof(strides) / sizeof(strides[0])); level_index++)
  {
    uint32_t level_stride = strides[level_index];
    uint32_t grid_w = params->input_width / level_stride;
    uint32_t grid_h = params->input_height / level_stride;
    uint32_t level_boxes = grid_w * grid_h;

    if (remaining < level_boxes)
    {
      uint32_t row = remaining / grid_w;
      uint32_t col = remaining - (row * grid_w);
      *stride = (float32_t)level_stride;
      *anchor_x = ((float32_t)col + 0.5f) * (*stride);
      *anchor_y = ((float32_t)row + 0.5f) * (*stride);
      return AI_OD_POSTPROCESS_ERROR_NO;
    }

    remaining -= level_boxes;
  }

  return AI_OD_POSTPROCESS_ERROR;
}

static float32_t polygon_signed_area(const obb_point_t *points, uint32_t count)
{
  float32_t area = 0.0f;
  uint32_t i;

  if (count < 3U)
  {
    return 0.0f;
  }

  for (i = 0U; i < count; i++)
  {
    uint32_t next = (i + 1U) % count;
    area += (points[i].x * points[next].y) - (points[next].x * points[i].y);
  }

  return area * 0.5f;
}

static float32_t polygon_area(const obb_point_t *points, uint32_t count)
{
  float32_t area = polygon_signed_area(points, count);
  return area < 0.0f ? -area : area;
}

static float32_t cross_product(const obb_point_t *a, const obb_point_t *b, const obb_point_t *c)
{
  return ((b->x - a->x) * (c->y - a->y)) - ((b->y - a->y) * (c->x - a->x));
}

static uint8_t point_inside_edge(const obb_point_t *point,
                                 const obb_point_t *edge_start,
                                 const obb_point_t *edge_end,
                                 float32_t clip_orientation)
{
  float32_t cross = cross_product(edge_start, edge_end, point);
  float32_t epsilon = 1.0e-5f;

  if (clip_orientation >= 0.0f)
  {
    return (cross >= -epsilon) ? 1U : 0U;
  }

  return (cross <= epsilon) ? 1U : 0U;
}

static obb_point_t line_intersection(const obb_point_t *line_a,
                                     const obb_point_t *line_b,
                                     const obb_point_t *edge_a,
                                     const obb_point_t *edge_b)
{
  obb_point_t result = *line_b;
  float32_t rx = line_b->x - line_a->x;
  float32_t ry = line_b->y - line_a->y;
  float32_t sx = edge_b->x - edge_a->x;
  float32_t sy = edge_b->y - edge_a->y;
  float32_t denom = (rx * sy) - (ry * sx);
  float32_t t;

  if ((denom > -1.0e-6f) && (denom < 1.0e-6f))
  {
    return result;
  }

  t = (((edge_a->x - line_a->x) * sy) - ((edge_a->y - line_a->y) * sx)) / denom;
  result.x = line_a->x + (t * rx);
  result.y = line_a->y + (t * ry);
  return result;
}

static uint32_t clip_polygon_against_edge(const obb_point_t *input,
                                          uint32_t input_count,
                                          obb_point_t *output,
                                          const obb_point_t *edge_start,
                                          const obb_point_t *edge_end,
                                          float32_t clip_orientation)
{
  obb_point_t previous;
  uint8_t previous_inside;
  uint32_t output_count = 0U;
  uint32_t i;

  if (input_count == 0U)
  {
    return 0U;
  }

  previous = input[input_count - 1U];
  previous_inside = point_inside_edge(&previous, edge_start, edge_end, clip_orientation);

  for (i = 0U; i < input_count; i++)
  {
    obb_point_t current = input[i];
    uint8_t current_inside = point_inside_edge(&current, edge_start, edge_end, clip_orientation);

    if (current_inside != 0U)
    {
      if (previous_inside == 0U)
      {
        if (output_count < 8U)
        {
          output[output_count++] = line_intersection(&previous, &current, edge_start, edge_end);
        }
      }
      if (output_count < 8U)
      {
        output[output_count++] = current;
      }
    }
    else if (previous_inside != 0U)
    {
      if (output_count < 8U)
      {
        output[output_count++] = line_intersection(&previous, &current, edge_start, edge_end);
      }
    }

    previous = current;
    previous_inside = current_inside;
  }

  return output_count;
}

static float32_t rotated_box_iou(const obb_candidate_t *a, const obb_candidate_t *b)
{
  obb_point_t clip_buffer_a[8];
  obb_point_t clip_buffer_b[8];
  const obb_point_t *input = clip_buffer_a;
  obb_point_t *output = clip_buffer_b;
  uint32_t input_count = 4U;
  uint32_t edge_idx;
  float32_t area_a = polygon_area(a->corners, 4U);
  float32_t area_b = polygon_area(b->corners, 4U);
  float32_t clip_orientation = polygon_signed_area(b->corners, 4U);
  float32_t inter_area;
  float32_t union_area;

  if ((area_a <= 0.0f) || (area_b <= 0.0f))
  {
    return 0.0f;
  }

  memcpy(clip_buffer_a, a->corners, sizeof(a->corners));

  for (edge_idx = 0U; edge_idx < 4U; edge_idx++)
  {
    const obb_point_t *edge_start = &b->corners[edge_idx];
    const obb_point_t *edge_end = &b->corners[(edge_idx + 1U) & 3U];
    input_count = clip_polygon_against_edge(input, input_count, output, edge_start, edge_end, clip_orientation);

    if (input_count == 0U)
    {
      return 0.0f;
    }

    input = output;
    output = (output == clip_buffer_b) ? clip_buffer_a : clip_buffer_b;
  }

  inter_area = polygon_area(input, input_count);
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
                               obb_candidate_t *candidate,
                               uint32_t *reject_reason)
{
  float32_t cx;
  float32_t cy;
  float32_t width;
  float32_t height;
  float32_t angle;
  float32_t best_conf = -1.0f;
  int32_t best_class = -1;
  float32_t cos_a = 1.0f;
  float32_t sin_a = 0.0f;
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

  *reject_reason = OBB_REJECT_NONE;

  if (params->output_is_raw_dfl != 0U)
  {
    float32_t stride;
    float32_t anchor_x;
    float32_t anchor_y;
    float32_t left;
    float32_t top;
    float32_t right;
    float32_t bottom;
    float32_t center_dx;
    float32_t center_dy;
    uint32_t class_offset = 4U * params->reg_max;
    uint32_t angle_offset = class_offset + params->nb_classes;

    if (yolo_anchor_for_box(params, box_index, &anchor_x, &anchor_y, &stride) != AI_OD_POSTPROCESS_ERROR_NO)
    {
      *reject_reason = OBB_REJECT_ANCHOR;
      return AI_OD_POSTPROCESS_ERROR;
    }

    angle = (sigmoid_f32(tensor_value(tensor, params, box_index, angle_offset)) - 0.25f) * (float32_t)M_PI;
    cos_a = cosf(angle);
    sin_a = sinf(angle);

    left = dfl_expectation(tensor, params, box_index, 0U * params->reg_max) * stride;
    top = dfl_expectation(tensor, params, box_index, 1U * params->reg_max) * stride;
    right = dfl_expectation(tensor, params, box_index, 2U * params->reg_max) * stride;
    bottom = dfl_expectation(tensor, params, box_index, 3U * params->reg_max) * stride;

    center_dx = (right - left) * 0.5f;
    center_dy = (bottom - top) * 0.5f;
    cx = anchor_x + (center_dx * cos_a) - (center_dy * sin_a);
    cy = anchor_y + (center_dx * sin_a) + (center_dy * cos_a);
    width = left + right;
    height = top + bottom;

    for (class_idx = 0U; class_idx < params->nb_classes; class_idx++)
    {
      float32_t conf = sigmoid_f32(tensor_value(tensor, params, box_index, class_offset + class_idx));
      if (conf > best_conf)
      {
        best_conf = conf;
        best_class = (int32_t)class_idx;
      }
    }
  }
  else if (params->output_is_raw_yolo26 != 0U)
  {
    float32_t stride;
    float32_t anchor_x;
    float32_t anchor_y;
    float32_t left;
    float32_t top;
    float32_t right;
    float32_t bottom;
    float32_t center_dx;
    float32_t center_dy;
    uint32_t class_offset = 4U;
    uint32_t angle_offset = class_offset + params->nb_classes;

    if (yolo_anchor_for_box(params, box_index, &anchor_x, &anchor_y, &stride) != AI_OD_POSTPROCESS_ERROR_NO)
    {
      *reject_reason = OBB_REJECT_ANCHOR;
      return AI_OD_POSTPROCESS_ERROR;
    }

    left = tensor_value(tensor, params, box_index, 0U);
    top = tensor_value(tensor, params, box_index, 1U);
    right = tensor_value(tensor, params, box_index, 2U);
    bottom = tensor_value(tensor, params, box_index, 3U);
    angle = tensor_value(tensor, params, box_index, angle_offset);
    cos_a = cosf(angle);
    sin_a = sinf(angle);

    center_dx = (right - left) * 0.5f;
    center_dy = (bottom - top) * 0.5f;
    cx = anchor_x + ((center_dx * cos_a) - (center_dy * sin_a)) * stride;
    cy = anchor_y + ((center_dx * sin_a) + (center_dy * cos_a)) * stride;
    width = (left + right) * stride;
    height = (top + bottom) * stride;

    for (class_idx = 0U; class_idx < params->nb_classes; class_idx++)
    {
      float32_t conf = sigmoid_f32(tensor_value(tensor, params, box_index, class_offset + class_idx));
      if (conf > best_conf)
      {
        best_conf = conf;
        best_class = (int32_t)class_idx;
      }
    }
  }
  else
  {
    cx = tensor_value(tensor, params, box_index, 0U);
    cy = tensor_value(tensor, params, box_index, 1U);
    width = tensor_value(tensor, params, box_index, 2U);
    height = tensor_value(tensor, params, box_index, 3U);
    angle = tensor_value(tensor, params, box_index, params->channels_per_box - 1U);

    for (class_idx = 0U; class_idx < params->nb_classes; class_idx++)
    {
      float32_t conf = tensor_value(tensor, params, box_index, 4U + class_idx);
      if (conf > best_conf)
      {
        best_conf = conf;
        best_class = (int32_t)class_idx;
      }
    }
  }

  if ((width <= 0.0f) || (height <= 0.0f))
  {
    *reject_reason = OBB_REJECT_DIMS;
    return AI_OD_POSTPROCESS_ERROR;
  }

  if ((best_class < 0) || (best_conf < params->conf_threshold))
  {
    *reject_reason = OBB_REJECT_CONF;
    return AI_OD_POSTPROCESS_ERROR;
  }

  if ((uint32_t)best_class == AI_OD_OBB_PP_IGNORE_CLASS_INDEX)
  {
    *reject_reason = OBB_REJECT_IGNORE_CLASS;
    return AI_OD_POSTPROCESS_ERROR;
  }

  if ((params->output_is_raw_dfl == 0U) && (params->output_is_raw_yolo26 == 0U))
  {
    cos_a = cosf(angle);
    sin_a = sinf(angle);
  }

  for (point_idx = 0U; point_idx < 4U; point_idx++)
  {
    float32_t dx = offsets[point_idx][0] * width;
    float32_t dy = offsets[point_idx][1] * height;
    float32_t px = cx + (dx * cos_a) - (dy * sin_a);
    float32_t py = cy + (dx * sin_a) + (dy * cos_a);

    candidate->corners[point_idx].x = px;
    candidate->corners[point_idx].y = py;
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
    *reject_reason = OBB_REJECT_CLAMP;
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
  uint32_t grid_boxes;
  uint32_t fallback_dim;
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

  error = select_output_layout(&NN_Info->outputs[0], params);
  if (error != AI_OD_POSTPROCESS_ERROR_NO)
  {
    printf("TRACE: OBB postprocess init: output layout error=%ld\n", (long)error);
    return error;
  }

  grid_boxes = yolo_grid_total_for_size(params->input_width, params->input_height);
  if (((params->output_is_raw_dfl != 0U) || (params->output_is_raw_yolo26 != 0U)) &&
      (grid_boxes != params->total_boxes))
  {
    fallback_dim = infer_square_input_dim_from_bytes((uint32_t)NN_Info->inputs[0].size_bytes, 3U);
    if ((fallback_dim != 0U) &&
        (yolo_grid_total_for_size(fallback_dim, fallback_dim) == params->total_boxes))
    {
      params->input_width = fallback_dim;
      params->input_height = fallback_dim;
      grid_boxes = yolo_grid_total_for_size(params->input_width, params->input_height);
    }
  }

  printf("TRACE: OBB postprocess init: input=%lux%lu input_bytes=%lu boxes=%lu grid_boxes=%lu channels=%lu raw_dfl=%u raw_yolo26=%u scale=%.6f zp=%ld\n",
         (unsigned long)params->input_width,
         (unsigned long)params->input_height,
         (unsigned long)NN_Info->inputs[0].size_bytes,
         (unsigned long)params->total_boxes,
         (unsigned long)grid_boxes,
         (unsigned long)params->channels_per_box,
         (unsigned int)params->output_is_raw_dfl,
         (unsigned int)params->output_is_raw_yolo26,
         params->raw_output_scale,
         (long)params->raw_output_zero_point);

  if ((params->input_width == 0U) || (params->input_height == 0U))
  {
    return AI_OD_POSTPROCESS_ERROR;
  }

  if (((params->output_is_raw_dfl != 0U) || (params->output_is_raw_yolo26 != 0U)) &&
      (grid_boxes != params->total_boxes))
  {
    return AI_OD_POSTPROCESS_ERROR;
  }

  return AI_OD_POSTPROCESS_ERROR_NO;
}

int32_t app_postprocess_run(void *pInput[], int nb_input, void *pOutput, void *pInput_param)
{
  const int8_t *tensor = (const int8_t *)pInput[0];
  od_pp_out_t *output = (od_pp_out_t *)pOutput;
  const od_obb_custom_pp_static_param_t *params = (const od_obb_custom_pp_static_param_t *)pInput_param;
  uint32_t candidate_count = 0U;
  uint32_t box_index;
  static uint32_t run_count = 0U;
  float32_t max_conf = -1.0f;
  uint32_t max_conf_box = 0U;
  uint32_t max_conf_class = 0U;
  uint32_t reject_counts[6] = {0U, 0U, 0U, 0U, 0U, 0U};

  assert(nb_input == 1);
  assert(tensor != NULL);
  assert(output != NULL);
  assert(params != NULL);

  output->pOutBuff = out_detections;
  output->nb_detect = 0;

  for (box_index = 0U; box_index < params->total_boxes; box_index++)
  {
    obb_candidate_t candidate;
    uint32_t class_idx;
    uint32_t reject_reason = OBB_REJECT_NONE;

    for (class_idx = 0U; class_idx < params->nb_classes; class_idx++)
    {
      float32_t conf = tensor_class_confidence(tensor, params, box_index, class_idx);
      if (conf > max_conf)
      {
        max_conf = conf;
        max_conf_box = box_index;
        max_conf_class = class_idx;
      }
    }

    if (build_candidate(tensor, params, box_index, &candidate, &reject_reason) == AI_OD_POSTPROCESS_ERROR_NO)
    {
      insert_candidate(&candidate, &candidate_count);
    }
    else if (reject_reason < (sizeof(reject_counts) / sizeof(reject_counts[0])))
    {
      reject_counts[reject_reason]++;
    }
  }

  for (box_index = 0U; box_index < candidate_count; box_index++)
  {
    uint32_t kept_index;
    uint8_t keep = 1U;

    for (kept_index = 0U; kept_index < (uint32_t)output->nb_detect; kept_index++)
    {
      const obb_candidate_t *kept = &kept_candidates[kept_index];

      if (kept->class_index != shortlist[box_index].class_index)
      {
        continue;
      }

      if (rotated_box_iou(&shortlist[box_index], kept) > params->iou_threshold)
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
    kept_candidates[output->nb_detect] = shortlist[box_index];
    output->nb_detect++;

    if ((uint32_t)output->nb_detect >= params->max_boxes_limit)
    {
      break;
    }
  }

  run_count++;
  if ((run_count <= 5U) || ((run_count % 30U) == 0U))
  {
    float32_t top_left = tensor_value(tensor, params, max_conf_box, 0U);
    float32_t top_top = tensor_value(tensor, params, max_conf_box, 1U);
    float32_t top_right = tensor_value(tensor, params, max_conf_box, 2U);
    float32_t top_bottom = tensor_value(tensor, params, max_conf_box, 3U);
    float32_t top_angle = tensor_value(tensor, params, max_conf_box, 4U + params->nb_classes);
    float32_t top_logit = tensor_value(tensor, params, max_conf_box, 4U + max_conf_class);

    printf("TRACE: OBB postprocess: run=%lu raw_dfl=%lu raw_yolo26=%lu rotated_nms=1 threshold=%.3f max_conf=%.3f max_logit=%.3f max_box=%lu max_class=%lu candidates=%lu detections=%lu reject[anchor=%lu dims=%lu conf=%lu ignore=%lu clamp=%lu] top_ltrba=[%.3f %.3f %.3f %.3f %.3f]\n",
           (unsigned long)run_count,
           (unsigned long)params->output_is_raw_dfl,
           (unsigned long)params->output_is_raw_yolo26,
           (double)params->conf_threshold,
           (double)max_conf,
           (double)top_logit,
           (unsigned long)max_conf_box,
           (unsigned long)max_conf_class,
           (unsigned long)candidate_count,
           (unsigned long)output->nb_detect,
           (unsigned long)reject_counts[OBB_REJECT_ANCHOR],
           (unsigned long)reject_counts[OBB_REJECT_DIMS],
           (unsigned long)reject_counts[OBB_REJECT_CONF],
           (unsigned long)reject_counts[OBB_REJECT_IGNORE_CLASS],
           (unsigned long)reject_counts[OBB_REJECT_CLAMP],
           (double)top_left,
           (double)top_top,
           (double)top_right,
           (double)top_bottom,
           (double)top_angle);
  }

  return AI_OD_POSTPROCESS_ERROR_NO;
}
#endif
