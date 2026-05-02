/**
  ******************************************************************************
  * @file    imx219.h
  * @author  MDG Application Team
  * @brief   This file contains all the functions prototypes for the imx219.c
  *          driver.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2024 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef IMX219_H
#define IMX219_H

#ifdef __cplusplus
 extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "imx219_reg.h"
#include <stddef.h>

/** @addtogroup BSP
  * @{
  */

/** @addtogroup Components
  * @{
  */

/** @addtogroup imx219
  * @{
  */

/** @defgroup IMX219_Exported_Types
  * @{
  */

typedef int32_t (*IMX219_Init_Func)    (void);
typedef int32_t (*IMX219_DeInit_Func)  (void);
typedef int32_t (*IMX219_GetTick_Func) (void);
typedef int32_t (*IMX219_WriteReg_Func)(uint16_t, uint16_t, uint8_t*, uint16_t);
typedef int32_t (*IMX219_ReadReg_Func) (uint16_t, uint16_t, uint8_t*, uint16_t);

typedef struct
{
  IMX219_Init_Func          Init;
  IMX219_DeInit_Func        DeInit;
  uint16_t                  Address;
  IMX219_WriteReg_Func      WriteReg;
  IMX219_ReadReg_Func       ReadReg;
  IMX219_GetTick_Func       GetTick;
} IMX219_IO_t;

typedef struct
{
  IMX219_IO_t         IO;
  imx219_ctx_t        Ctx;
  uint8_t             IsInitialized;
  uint32_t            CurrentResolution;  /* Active resolution constant, used by SetFramerate */
  uint16_t            CurrentFrameLengthLines; /* Active VTS, used to clamp exposure */
} IMX219_Object_t;

typedef struct
{
  uint32_t Config_Resolution;
  uint32_t Config_LightMode;
  uint32_t Config_SpecialEffect;
  uint32_t Config_Brightness;
  uint32_t Config_Saturation;
  uint32_t Config_Contrast;
  uint32_t Config_HueDegree;
  uint32_t Config_Gain;
  uint32_t Config_Exposure;
  uint32_t Config_MirrorFlip;
  uint32_t Config_Zoom;
  uint32_t Config_NightMode;
  uint32_t Config_ExposureMode;
  uint32_t Config_SensorInfo;
  uint32_t Config_TestPattern;
} IMX219_Capabilities_t;

/**
  * @}
  */

/** @defgroup IMX219_Exported_Constants
  * @{
  */
#define IMX219_OK                      (0)
#define IMX219_ERROR                   (-1)

/**
 * @brief  IMX219 Features Parameters
 */
/* Camera resolutions */
#define IMX219_R1920_1080               0U   /* 1920x1080  (1080p, cropped)       */
#define IMX219_R3280_2464               1U   /* 3280x2464  (full pixel array)      */
#define IMX219_R1640_1232               2U   /* 1640x1232  (2x2 binned, full FOV)  */

/* Camera pixel format */
#define IMX219_RAW_RGGB10              10U   /* RAW10 RGGB Bayer */
#define IMX219_RAW_RGGB8               8U    /* RAW8  RGGB Bayer */

/* Input clock frequency */
#define IMX219_INCK_24MHZ               0U

/* Mirror / Flip */
#define IMX219_MIRROR_FLIP_NONE         0x00U
#define IMX219_FLIP                     0x01U
#define IMX219_MIRROR                   0x02U
#define IMX219_MIRROR_FLIP              0x03U

/**
  * @}
  */

/** @defgroup IMX219_Exported_Functions IMX219 Exported Functions
  * @{
  */
int32_t IMX219_RegisterBusIO(IMX219_Object_t *pObj, IMX219_IO_t *pIO);
int32_t IMX219_Init(IMX219_Object_t *pObj, uint32_t Resolution, uint32_t PixelFormat);
int32_t IMX219_Start(IMX219_Object_t *pObj);
int32_t IMX219_DeInit(IMX219_Object_t *pObj);
int32_t IMX219_ReadID(IMX219_Object_t *pObj, uint32_t *Id);
int32_t IMX219_GetCapabilities(IMX219_Object_t *pObj, IMX219_Capabilities_t *Capabilities);
int32_t IMX219_SetGain(IMX219_Object_t *pObj, int32_t gain);
int32_t IMX219_SetExposure(IMX219_Object_t *pObj, int32_t exposure);
int32_t IMX219_SetFrequency(IMX219_Object_t *pObj, int32_t frequency);
int32_t IMX219_SetFramerate(IMX219_Object_t *pObj, int32_t framerate);
int32_t IMX219_MirrorFlipConfig(IMX219_Object_t *pObj, uint32_t Config);
int32_t IMX219_SetTestPattern(IMX219_Object_t *pObj, int32_t mode);

/**
  * @}
  */
#ifdef __cplusplus
}
#endif

#endif
/**
  * @}
  */

/**
  * @}
  */

/**
  * @}
  */
