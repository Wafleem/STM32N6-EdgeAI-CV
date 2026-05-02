/**
  ******************************************************************************
  * @file    imx219.c
  * @author  MDG Application Team
  * @brief   This file provides the IMX219 camera driver.
  *
  * Register sequences ported from the Raspberry Pi Linux kernel driver:
  *   drivers/media/i2c/imx219.c  (rpi-6.6.y)
  *   Copyright (C) 2019, Raspberry Pi (Trading) Ltd
  *   SPDX-License-Identifier: GPL-2.0
  *
  * Adapted for the STM32 CMW sensor driver interface.
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

/* Includes ------------------------------------------------------------------*/
#include "imx219.h"
#include <math.h>    /* powf, log10f — FPU available on STM32N6 Cortex-M55 */
#include <string.h>

/** @addtogroup BSP
  * @{
  */

/** @addtogroup Components
  * @{
  */

/** @addtogroup IMX219
  * @brief     This file provides a set of functions needed to drive the
  *            IMX219 Camera module.
  * @{
  */

/** @defgroup IMX219_Private_TypesDefinitions Private Types definition
  * @{
  */
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

/*
 * Line period at 1920x1080:
 *   pixel_rate = 182 400 000 px/s,  HTS = 3448 px/line
 *   T_line = 3448 / 182400000 * 1e6 = 18.903 us
 */
#define IMX219_1H_PERIOD_USEC   (1000000.0F * 3448.0F / 182400000.0F)

static uint16_t IMX219_DefaultFrameLengthLines(uint32_t resolution)
{
  switch (resolution)
  {
    case IMX219_R1920_1080:
      return 1763U;
    case IMX219_R3280_2464:
      return 3526U;
    case IMX219_R1640_1232:
      return 1763U;
    default:
      return 1763U;
  }
}

/**
  * @}
  */

/** @defgroup IMX219_Private_Constants Private Constants
  * @{
  */

/* Each entry: { register_address_16bit, value_8bit } */
struct regval {
  uint16_t addr;
  uint8_t  val;
};

/* ── Common initialisation (always written regardless of mode) ────────────
 *
 * Sequence from the RPi Linux kernel imx219_common_regs[]:
 *
 *  1. Standby
 *  2. Unlock access to 0x3000-0x5FFF (mandatory per Sony App Note)
 *  3. Undocumented analog/timing tuning registers
 *  4. Line length (HTS = 3448 = 0x0D78)
 *  5. X/Y odd increment = 1 (no skipping)
 *  6. D-PHY timing = auto
 *  7. External clock frequency = 24 MHz (24 * 256 = 0x1800)
 * ──────────────────────────────────────────────────────────────────────── */
static const struct regval imx219_common_regs[] = {
  /* Standby */
  {0x0100, 0x00},

  /* Unlock registers 0x3000–0x5FFF */
  {0x30eb, 0x05},
  {0x30eb, 0x0c},
  {0x300a, 0xff},
  {0x300b, 0xff},
  {0x30eb, 0x05},
  {0x30eb, 0x09},

  /* Undocumented analog / timing tuning (from RPi kernel driver) */
  {0x455e, 0x00},
  {0x471e, 0x4b},
  {0x4767, 0x0f},
  {0x4750, 0x14},
  {0x4540, 0x00},
  {0x47b4, 0x14},
  {0x4713, 0x30},
  {0x478b, 0x10},
  {0x478f, 0x10},
  {0x4793, 0x10},
  {0x4797, 0x0e},
  {0x479b, 0x0e},

  /* Line length: HTS = 3448 = 0x0D78 */
  {0x0162, 0x0d},
  {0x0163, 0x78},

  /* X / Y odd increment (no subsampling on input side) */
  {0x0170, 0x01},
  {0x0171, 0x01},

  /* D-PHY timing: auto */
  {0x0128, 0x00},

  /* External clock frequency: 24 MHz -> 24 * 256 = 6144 = 0x1800 */
  {0x012a, 0x18},
  {0x012b, 0x00},
};

/* ── 2-lane PLL + CSI lane mode ───────────────────────────────────────────
 *
 * Produces 912 Mbps/lane with 24 MHz INCK.
 * See register calculation comment in imx219_reg.h.
 * ──────────────────────────────────────────────────────────────────────── */
static const struct regval imx219_2lane_pll_regs[] = {
  {0x0301, 0x05},   /* VTPXCK_DIV      = 5  */
  {0x0303, 0x01},   /* VTSYCK_DIV      = 1  */
  {0x0304, 0x03},   /* PREPLLCK_VT_DIV = 3  */
  {0x0305, 0x03},   /* PREPLLCK_OP_DIV = 3  */
  {0x0306, 0x00},   /* PLL_VT_MPY MSB  = 0  */
  {0x0307, 0x39},   /* PLL_VT_MPY LSB  = 57 */
  {0x030b, 0x01},   /* OPSYCK_DIV      = 1  */
  {0x030c, 0x00},   /* PLL_OP_MPY MSB  = 0  */
  {0x030d, 0x72},   /* PLL_OP_MPY LSB  = 114*/
  {0x0114, 0x01},   /* CSI_LANE_MODE   = 2 lanes */
};

/* ── RAW10 output format ──────────────────────────────────────────────────
 *
 * CSI data format = 0x0A0A (10-bit in, 10-bit out)
 * OPPXCK_DIV = 10
 * ──────────────────────────────────────────────────────────────────────── */
static const struct regval imx219_raw10_format_regs[] = {
  {0x018c, 0x0a},   /* CSI_DATA_FORMAT_A MSB */
  {0x018d, 0x0a},   /* CSI_DATA_FORMAT_A LSB */
  {0x0309, 0x0a},   /* OPPXCK_DIV = 10       */
};

/* ── Resolution mode: 1920x1080 (centre crop, no binning) ────────────────
 *
 * Crop window from the 3280x2464 array:
 *   X: 680 to 2599  (width  = 1920)
 *   Y: 692 to 1771  (height = 1080)
 *
 * VTS = 1763 for 30 fps (pixel_rate=182.4e6, HTS=3448):
 *   fps = 182400000 / (3448 * 1763) = 30.00  OK
 * ──────────────────────────────────────────────────────────────────────── */
static const struct regval mode_1920_1080_regs[] = {
  /* Crop start / end */
  {0x0164, 0x02}, {0x0165, 0xa8},   /* X_ADD_STA_A = 680    */
  {0x0166, 0x0a}, {0x0167, 0x27},   /* X_ADD_END_A = 2599   */
  {0x0168, 0x02}, {0x0169, 0xb4},   /* Y_ADD_STA_A = 692    */
  {0x016a, 0x06}, {0x016b, 0xeb},   /* Y_ADD_END_A = 1771   */
  /* Output size */
  {0x016c, 0x07}, {0x016d, 0x80},   /* X_OUTPUT_SIZE = 1920 */
  {0x016e, 0x04}, {0x016f, 0x38},   /* Y_OUTPUT_SIZE = 1080 */
  /* No binning */
  {0x0174, 0x00}, {0x0175, 0x00},
  /* VTS = 1763 = 0x06E3 @ 30 fps */
  {0x0160, 0x06}, {0x0161, 0xe3},
  /* Test-pattern window (matches output) */
  {0x0624, 0x07}, {0x0625, 0x80},   /* TP_WINDOW_WIDTH  = 1920 */
  {0x0626, 0x04}, {0x0627, 0x38},   /* TP_WINDOW_HEIGHT = 1080 */
};

/* ── Resolution mode: 3280x2464 (full pixel array, 15 fps) ───────────────
 *
 * VTS = 3526 for 15 fps:
 *   fps = 182400000 / (3448 * 3526) = 15.00  OK
 * ──────────────────────────────────────────────────────────────────────── */
static const struct regval mode_3280_2464_regs[] = {
  {0x0164, 0x00}, {0x0165, 0x00},   /* X_ADD_STA_A = 0      */
  {0x0166, 0x0c}, {0x0167, 0xcf},   /* X_ADD_END_A = 3279   */
  {0x0168, 0x00}, {0x0169, 0x00},   /* Y_ADD_STA_A = 0      */
  {0x016a, 0x09}, {0x016b, 0x9f},   /* Y_ADD_END_A = 2463   */
  {0x016c, 0x0c}, {0x016d, 0xd0},   /* X_OUTPUT_SIZE = 3280 */
  {0x016e, 0x09}, {0x016f, 0xa0},   /* Y_OUTPUT_SIZE = 2464 */
  {0x0174, 0x00}, {0x0175, 0x00},   /* No binning           */
  /* VTS = 3526 = 0x0DC6 @ 15 fps */
  {0x0160, 0x0d}, {0x0161, 0xc6},
  {0x0624, 0x0c}, {0x0625, 0xd0},   /* TP_WINDOW_WIDTH  = 3280 */
  {0x0626, 0x09}, {0x0627, 0xa0},   /* TP_WINDOW_HEIGHT = 2464 */
};

/* ── Resolution mode: 1640x1232 (2x2 binned, full FOV, 30 fps) ───────────
 *
 * Reads the full 3280x2464 array then bins 2x2 to produce 1640x1232.
 * Useful when full FOV is needed without sacrificing frame rate.
 * VTS = 1763 for 30 fps (same timing as 1080p).
 * ──────────────────────────────────────────────────────────────────────── */
static const struct regval mode_1640_1232_regs[] = {
  {0x0164, 0x00}, {0x0165, 0x00},   /* X_ADD_STA_A = 0      */
  {0x0166, 0x0c}, {0x0167, 0xcf},   /* X_ADD_END_A = 3279   */
  {0x0168, 0x00}, {0x0169, 0x00},   /* Y_ADD_STA_A = 0      */
  {0x016a, 0x09}, {0x016b, 0x9f},   /* Y_ADD_END_A = 2463   */
  {0x016c, 0x06}, {0x016d, 0x68},   /* X_OUTPUT_SIZE = 1640 */
  {0x016e, 0x04}, {0x016f, 0xd0},   /* Y_OUTPUT_SIZE = 1232 */
  /* 2x2 analogue binning */
  {0x0174, 0x01}, {0x0175, 0x01},
  /* VTS = 1763 = 0x06E3 @ 30 fps */
  {0x0160, 0x06}, {0x0161, 0xe3},
  {0x0624, 0x06}, {0x0625, 0x68},   /* TP_WINDOW_WIDTH  = 1640 */
  {0x0626, 0x04}, {0x0627, 0xd0},   /* TP_WINDOW_HEIGHT = 1232 */
};

/**
  * @}
  */

/** @defgroup IMX219_Private_Functions_Prototypes Private Functions Prototypes
  * @{
  */
static int32_t IMX219_WriteTable(IMX219_Object_t *pObj, const struct regval *regs, uint32_t size);
static int32_t IMX219_ReadRegWrap(void *handle, uint16_t Reg, uint8_t *Data, uint16_t Length);
static int32_t IMX219_WriteRegWrap(void *handle, uint16_t Reg, uint8_t *Data, uint16_t Length);
static int32_t IMX219_Delay(IMX219_Object_t *pObj, uint32_t Delay);
static int32_t IMX219_WriteU16BE(IMX219_Object_t *pObj, uint16_t reg, uint16_t value);

/**
  * @}
  */

/** @defgroup IMX219_Private_Functions Private Functions
  * @{
  */

/**
  * @brief  Write a table of (address, value) register pairs
  * @param  pObj   pointer to component object
  * @param  regs   pointer to register table
  * @param  size   number of entries in the table
  * @retval IMX219_OK / IMX219_ERROR
  */
static int32_t IMX219_WriteTable(IMX219_Object_t *pObj, const struct regval *regs, uint32_t size)
{
  uint32_t index;
  int32_t ret = IMX219_OK;

  for (index = 0; index < size; index++)
  {
    if (ret != IMX219_ERROR)
    {
      if (imx219_register_set(&pObj->Ctx, regs[index].addr, regs[index].val) != IMX219_OK)
      {
        ret = IMX219_ERROR;
      }
    }
  }
  return ret;
}

/**
  * @brief  Write a big-endian 16-bit value to consecutive 8-bit registers
  * @param  pObj   pointer to component object
  * @param  reg    address of the MSB register
  * @param  value  16-bit value (MSB sent first)
  * @retval IMX219_OK / IMX219_ERROR
  */
static int32_t IMX219_WriteU16BE(IMX219_Object_t *pObj, uint16_t reg, uint16_t value)
{
  uint8_t buf[2];
  buf[0] = (uint8_t)((value >> 8) & 0xFFU);
  buf[1] = (uint8_t)(value & 0xFFU);
  return imx219_write_reg(&pObj->Ctx, reg, buf, 2);
}

/**
  * @brief  Polling delay using GetTick
  * @param  pObj   pointer to component object
  * @param  Delay  milliseconds to wait
  * @retval IMX219_OK
  */
static int32_t IMX219_Delay(IMX219_Object_t *pObj, uint32_t Delay)
{
  uint32_t tickstart = pObj->IO.GetTick();
  while ((pObj->IO.GetTick() - tickstart) < Delay)
  {
  }
  return IMX219_OK;
}

/**
  * @brief  Wrap component ReadReg to Bus Read function
  */
static int32_t IMX219_ReadRegWrap(void *handle, uint16_t Reg, uint8_t *pData, uint16_t Length)
{
  IMX219_Object_t *pObj = (IMX219_Object_t *)handle;
  return pObj->IO.ReadReg(pObj->IO.Address, Reg, pData, Length);
}

/**
  * @brief  Wrap component WriteReg to Bus Write function
  */
static int32_t IMX219_WriteRegWrap(void *handle, uint16_t Reg, uint8_t *pData, uint16_t Length)
{
  IMX219_Object_t *pObj = (IMX219_Object_t *)handle;
  return pObj->IO.WriteReg(pObj->IO.Address, Reg, pData, Length);
}

/**
  * @}
  */

/** @defgroup IMX219_Exported_Functions IMX219 Exported Functions
  * @{
  */

/**
  * @brief  Register component IO bus
  * @param  pObj  component object pointer
  * @param  pIO   IO structure pointer
  * @retval Component status
  */
int32_t IMX219_RegisterBusIO(IMX219_Object_t *pObj, IMX219_IO_t *pIO)
{
  int32_t ret;

  if (pObj == NULL)
  {
    ret = IMX219_ERROR;
  }
  else
  {
    pObj->IO.Init      = pIO->Init;
    pObj->IO.DeInit    = pIO->DeInit;
    pObj->IO.Address   = pIO->Address;
    pObj->IO.WriteReg  = pIO->WriteReg;
    pObj->IO.ReadReg   = pIO->ReadReg;
    pObj->IO.GetTick   = pIO->GetTick;

    pObj->Ctx.ReadReg  = IMX219_ReadRegWrap;
    pObj->Ctx.WriteReg = IMX219_WriteRegWrap;
    pObj->Ctx.handle   = pObj;

    if (pObj->IO.Init != NULL)
    {
      ret = pObj->IO.Init();
    }
    else
    {
      ret = IMX219_ERROR;
    }
  }

  return ret;
}

/**
  * @brief  Initialise the IMX219 sensor and write the full register sequence.
  * @param  pObj        pointer to component object
  * @param  Resolution  IMX219_R1920_1080 / IMX219_R3280_2464 / IMX219_R1640_1232
  * @param  PixelFormat IMX219_RAW_RGGB10 or IMX219_RAW_RGGB8
  * @retval Component status
  */
int32_t IMX219_Init(IMX219_Object_t *pObj, uint32_t Resolution, uint32_t PixelFormat)
{
  int32_t ret = IMX219_OK;

  if (pObj->IsInitialized == 0U)
  {
    /* 1. Common registers (standby, unlock, analog tuning, HTS, DPHY, EXCK) */
    if (IMX219_WriteTable(pObj, imx219_common_regs, ARRAY_SIZE(imx219_common_regs)) != IMX219_OK)
    {
      return IMX219_ERROR;
    }

    /* 2. PLL configuration for 2-lane / 912 Mbps */
    if (IMX219_WriteTable(pObj, imx219_2lane_pll_regs, ARRAY_SIZE(imx219_2lane_pll_regs)) != IMX219_OK)
    {
      return IMX219_ERROR;
    }

    /* 3. Resolution-specific crop window and VTS */
    switch (Resolution)
    {
      case IMX219_R1920_1080:
        ret = IMX219_WriteTable(pObj, mode_1920_1080_regs, ARRAY_SIZE(mode_1920_1080_regs));
        break;
      case IMX219_R3280_2464:
        ret = IMX219_WriteTable(pObj, mode_3280_2464_regs, ARRAY_SIZE(mode_3280_2464_regs));
        break;
      case IMX219_R1640_1232:
        ret = IMX219_WriteTable(pObj, mode_1640_1232_regs, ARRAY_SIZE(mode_1640_1232_regs));
        break;
      default:
        ret = IMX219_ERROR;
        break;
    }

    if (ret != IMX219_OK)
    {
      return IMX219_ERROR;
    }

    /* 4. Pixel format */
    switch (PixelFormat)
    {
      case IMX219_RAW_RGGB10:
        ret = IMX219_WriteTable(pObj, imx219_raw10_format_regs, ARRAY_SIZE(imx219_raw10_format_regs));
        break;
      case IMX219_RAW_RGGB8:
        /* CSI data format 0x0808, OPPXCK_DIV = 8 */
        if (imx219_register_set(&pObj->Ctx, 0x018c, 0x08) != IMX219_OK) { return IMX219_ERROR; }
        if (imx219_register_set(&pObj->Ctx, 0x018d, 0x08) != IMX219_OK) { return IMX219_ERROR; }
        if (imx219_register_set(&pObj->Ctx, 0x0309, 0x08) != IMX219_OK) { return IMX219_ERROR; }
        break;
      default:
        ret = IMX219_ERROR;
        break;
    }

    if (ret != IMX219_OK)
    {
      return IMX219_ERROR;
    }

    pObj->CurrentResolution = Resolution;
    pObj->CurrentFrameLengthLines = IMX219_DefaultFrameLengthLines(Resolution);
    pObj->IsInitialized = 1U;
  }

  return IMX219_OK;
}

/**
  * @brief  Start streaming.
  * @param  pObj  pointer to component object
  * @retval Component status
  */
int32_t IMX219_Start(IMX219_Object_t *pObj)
{
  int32_t ret;

  ret = imx219_register_set(&pObj->Ctx, IMX219_REG_MODE_SELECT, IMX219_MODE_STREAMING);
  if (ret != IMX219_OK)
  {
    return IMX219_ERROR;
  }
  /* Allow PLL and clock lanes to stabilise before first line */
  IMX219_Delay(pObj, 20);
  return IMX219_OK;
}

/**
  * @brief  De-initialise the camera sensor (back to standby).
  * @param  pObj  pointer to component object
  * @retval Component status
  */
int32_t IMX219_DeInit(IMX219_Object_t *pObj)
{
  if (pObj->IsInitialized == 1U)
  {
    pObj->IsInitialized = 0U;
  }
  return IMX219_OK;
}

/**
  * @brief  Read the IMX219 chip identifier.
  *
  *  The chip ID is a 16-bit value stored big-endian at registers
  *  0x0000 (MSB = 0x02) and 0x0001 (LSB = 0x19), expected value 0x0219.
  *
  * @param  pObj  pointer to component object
  * @param  Id    pointer to returned chip ID
  * @retval Component status
  */
int32_t IMX219_ReadID(IMX219_Object_t *pObj, uint32_t *Id)
{
  int32_t ret;
  uint8_t id_bytes[2];

  /* Initialise the I2C bus */
  pObj->IO.Init();

  ret = imx219_read_reg(&pObj->Ctx, IMX219_REG_CHIP_ID_MSB, id_bytes, 2);
  if (ret != IMX219_OK)
  {
    return IMX219_ERROR;
  }

  /* Sensor sends MSB first; combine into 16-bit value */
  *Id = ((uint32_t)id_bytes[0] << 8U) | (uint32_t)id_bytes[1];

  return IMX219_OK;
}

/**
  * @brief  Read the IMX219 capabilities.
  * @param  pObj          pointer to component object
  * @param  Capabilities  pointer to capabilities structure
  * @retval Component status
  */
int32_t IMX219_GetCapabilities(IMX219_Object_t *pObj, IMX219_Capabilities_t *Capabilities)
{
  if (pObj == NULL)
  {
    return IMX219_ERROR;
  }

  Capabilities->Config_Brightness    = 0;
  Capabilities->Config_Contrast      = 0;
  Capabilities->Config_HueDegree     = 0;
  Capabilities->Config_Gain          = 1;
  Capabilities->Config_Exposure      = 1;
  Capabilities->Config_ExposureMode  = 0;
  Capabilities->Config_LightMode     = 0;
  Capabilities->Config_MirrorFlip    = 1;
  Capabilities->Config_NightMode     = 0;
  Capabilities->Config_Resolution    = 1;
  Capabilities->Config_Saturation    = 0;
  Capabilities->Config_SpecialEffect = 0;
  Capabilities->Config_Zoom          = 0;
  Capabilities->Config_SensorInfo    = 1;
  Capabilities->Config_TestPattern   = 1;

  return IMX219_OK;
}

/**
  * @brief  Set the analogue (and, if needed, digital) gain.
  *
  *  The IMX219 analogue gain register is non-linear:
  *    Linear_gain = 256 / (256 - gain_reg)
  *    => gain_reg = 256 * (1 - 10^(-gain_mdB / 20000))
  *
  *  When the requested gain exceeds the analogue maximum (20 560 mdB) the
  *  analogue register is clamped to max (232) and the remainder is applied
  *  via the 16-bit Q8.8 digital gain register (0x0158/0x0159).
  *
  * @param  pObj  pointer to component object
  * @param  gain  requested total gain in milli-dB
  * @retval Component status
  */
int32_t IMX219_SetGain(IMX219_Object_t *pObj, int32_t gain)
{
  uint8_t  ana_reg;
  uint16_t dig_reg;
  float    linear;

  if ((gain < IMX219_GAIN_MIN) || (gain > IMX219_GAIN_MAX))
  {
    return IMX219_ERROR;
  }

  if (gain <= IMX219_AGAIN_MAX)
  {
    /* Analogue gain covers the full requested range */
    linear  = powf(10.0F, (float)gain / 20000.0F);
    ana_reg = (uint8_t)(256.0F * (1.0F - (1.0F / linear)));
    if (ana_reg > IMX219_ANA_GAIN_MAX) { ana_reg = IMX219_ANA_GAIN_MAX; }
    dig_reg = IMX219_DGTL_GAIN_MIN;  /* 1.0x digital */
  }
  else
  {
    /* Saturate analogue at max; apply rest as digital gain */
    ana_reg = IMX219_ANA_GAIN_MAX;
    linear  = powf(10.0F, (float)(gain - IMX219_AGAIN_MAX) / 20000.0F);
    dig_reg = (uint16_t)(linear * 256.0F);
    if (dig_reg < IMX219_DGTL_GAIN_MIN) { dig_reg = IMX219_DGTL_GAIN_MIN; }
    if (dig_reg > IMX219_DGTL_GAIN_MAX) { dig_reg = IMX219_DGTL_GAIN_MAX; }
  }

  /* Write analogue gain (single byte) */
  if (imx219_register_set(&pObj->Ctx, IMX219_REG_ANALOG_GAIN, ana_reg) != IMX219_OK)
  {
    return IMX219_ERROR;
  }

  /* Write digital gain (16-bit big-endian) */
  if (IMX219_WriteU16BE(pObj, IMX219_REG_DIGITAL_GAIN_MSB, dig_reg) != IMX219_OK)
  {
    return IMX219_ERROR;
  }

  return IMX219_OK;
}

/**
  * @brief  Set the integration (exposure) time.
  *
  *  Converts microseconds to line periods and writes the 16-bit big-endian
  *  exposure register (0x015A / 0x015B).
  *
  *  Line period at 1920x1080: T_line = HTS/pixel_rate = 3448/182400000 = 18.903 us
  *  The register value is clamped between 4 lines (min) and VTS-4 lines.
  *
  * @param  pObj      pointer to component object
  * @param  exposure  exposure time in microseconds
  * @retval Component status
  */
int32_t IMX219_SetExposure(IMX219_Object_t *pObj, int32_t exposure)
{
  uint16_t exp_lines;
  uint16_t max_exp_lines;

  /* Convert us -> lines and clamp */
  exp_lines = (uint16_t)((float)exposure / IMX219_1H_PERIOD_USEC);

  if (exp_lines < IMX219_EXPOSURE_MIN_LINES)
  {
    exp_lines = (uint16_t)IMX219_EXPOSURE_MIN_LINES;
  }

  max_exp_lines = (pObj->CurrentFrameLengthLines > IMX219_EXPOSURE_OFFSET_LINES) ?
                  (uint16_t)(pObj->CurrentFrameLengthLines - IMX219_EXPOSURE_OFFSET_LINES) :
                  (uint16_t)IMX219_EXPOSURE_MIN_LINES;
  if (exp_lines > max_exp_lines)
  {
    exp_lines = max_exp_lines;
  }

  /* Write big-endian to 0x015A (MSB) / 0x015B (LSB) */
  if (IMX219_WriteU16BE(pObj, IMX219_REG_EXPOSURE_MSB, exp_lines) != IMX219_OK)
  {
    return IMX219_ERROR;
  }

  return IMX219_OK;
}

/**
  * @brief  Set the input clock frequency (currently only 24 MHz is supported).
  * @param  pObj      pointer to component object
  * @param  frequency IMX219_INCK_24MHZ
  * @retval Component status
  */
int32_t IMX219_SetFrequency(IMX219_Object_t *pObj, int32_t frequency)
{
  if (frequency != IMX219_INCK_24MHZ)
  {
    return IMX219_ERROR;
  }

  /* EXCK_FREQ = 24 * 256 = 0x1800 (already written in Init; this is a no-op
   * but keeps the interface consistent with other CMW sensor drivers) */
  if (imx219_register_set(&pObj->Ctx, IMX219_REG_EXCK_FREQ_MSB, 0x18U) != IMX219_OK)
  {
    return IMX219_ERROR;
  }
  if (imx219_register_set(&pObj->Ctx, IMX219_REG_EXCK_FREQ_LSB, 0x00U) != IMX219_OK)
  {
    return IMX219_ERROR;
  }

  return IMX219_OK;
}

/**
  * @brief  Set the frame rate by updating the VTS (frame length lines) register.
  *
  *  Supported rates depend on the active resolution:
  *    1920x1080 : 10 / 15 / 20 / 25 / 30 fps
  *    3280x2464 : 15 fps (maximum for full-res mode)
  *    1640x1232 : 10 / 15 / 20 / 25 / 30 fps
  *
  *  VTS = pixel_rate / (HTS * fps) = 182400000 / (3448 * fps)
  *
  * @param  pObj      pointer to component object
  * @param  framerate desired frame rate (integer fps)
  * @retval Component status
  */
int32_t IMX219_SetFramerate(IMX219_Object_t *pObj, int32_t framerate)
{
  uint16_t vts;

  /* Full-resolution mode is limited to 15 fps */
  if ((pObj->CurrentResolution == IMX219_R3280_2464) && (framerate > 15))
  {
    return IMX219_ERROR;
  }

  switch (framerate)
  {
    case 30: vts = 1763U; break;
    case 25: vts = 2115U; break;
    case 20: vts = 2644U; break;
    case 15: vts = 3526U; break;
    case 10: vts = 5290U; break;
    default: return IMX219_ERROR;
  }

  if (IMX219_WriteU16BE(pObj, IMX219_REG_VTS_MSB, vts) != IMX219_OK)
  {
    return IMX219_ERROR;
  }

  pObj->CurrentFrameLengthLines = vts;

  return IMX219_OK;
}

/**
  * @brief  Configure horizontal mirror and/or vertical flip.
  *
  *  The IMX219 ORIENTATION register (0x0172) uses:
  *    bit 0 : HFLIP (horizontal mirror)
  *    bit 1 : VFLIP (vertical flip)
  *
  * @param  pObj    pointer to component object
  * @param  Config  IMX219_MIRROR_FLIP_NONE / IMX219_FLIP / IMX219_MIRROR / IMX219_MIRROR_FLIP
  * @retval Component status
  */
int32_t IMX219_MirrorFlipConfig(IMX219_Object_t *pObj, uint32_t Config)
{
  uint8_t orientation;

  switch (Config)
  {
    case IMX219_FLIP:
      orientation = IMX219_ORIENTATION_VFLIP;
      break;
    case IMX219_MIRROR:
      orientation = IMX219_ORIENTATION_HFLIP;
      break;
    case IMX219_MIRROR_FLIP:
      orientation = IMX219_ORIENTATION_HFLIP_VFLIP;
      break;
    case IMX219_MIRROR_FLIP_NONE:
    default:
      orientation = IMX219_ORIENTATION_NORMAL;
      break;
  }

  return imx219_register_set(&pObj->Ctx, IMX219_REG_ORIENTATION, orientation);
}

/**
  * @brief  Enable or disable the internal test pattern generator.
  *
  *  mode >= 0 : enable test pattern (values 0–4, see register map)
  *              0 = Disable       1 = Solid colour
  *              2 = Colour bars   3 = Grey ramp
  *              4 = PN9 sequence
  *  mode  < 0 : disable test pattern
  *
  * @param  pObj  pointer to component object
  * @param  mode  pattern mode or -1 to disable
  * @retval Component status
  */
int32_t IMX219_SetTestPattern(IMX219_Object_t *pObj, int32_t mode)
{
  uint16_t tp_val;

  if (mode < 0)
  {
    tp_val = IMX219_TEST_PATTERN_DISABLE;
  }
  else if (mode <= 4)
  {
    tp_val = (uint16_t)mode;
  }
  else
  {
    return IMX219_ERROR;
  }

  if (IMX219_WriteU16BE(pObj, IMX219_REG_TEST_PATTERN_MSB, tp_val) != IMX219_OK)
  {
    return IMX219_ERROR;
  }

  return IMX219_OK;
}

/**
  * @}
  */

/**
  * @}
  */

/**
  * @}
  */

/**
  * @}
  */
