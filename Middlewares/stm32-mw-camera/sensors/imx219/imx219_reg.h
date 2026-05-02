/**
  ******************************************************************************
  * @file    imx219_reg.h
  * @author  MDG Application Team
  * @brief   Header of imx219_reg.c
  *
  * Register map sourced from:
  *   - Sony IMX219 Product Brief / Application Notes
  *   - Raspberry Pi Linux kernel driver:
  *       drivers/media/i2c/imx219.c  (rpi-6.6.y)
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
#ifndef IMX219_REG_H
#define IMX219_REG_H

#include <cmsis_compiler.h>

#ifdef __cplusplus
 extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
/** @addtogroup BSP
  * @{
  */

/** @addtogroup Components
  * @{
  */

/** @addtogroup IMX219
  * @{
  */

/** @defgroup IMX219_Exported_Types
  * @{
  */

/**
  * @}
  */

/** @defgroup IMX219_Exported_Constants IMX219 Exported Constants
  * @{
  */

/* Streaming / standby -------------------------------------------------------*/
#define IMX219_REG_MODE_SELECT          0x0100U
#define IMX219_MODE_STANDBY             0x00U
#define IMX219_MODE_STREAMING           0x01U

/* Software reset ------------------------------------------------------------*/
#define IMX219_REG_SOFTWARE_RESET       0x0103U

/* Chip ID (16-bit, big-endian at 0x0000 / 0x0001) ---------------------------*/
#define IMX219_REG_CHIP_ID_MSB          0x0000U
#define IMX219_CHIP_ID                  0x0219U  /* (MSB<<8)|LSB = 0x02<<8|0x19 */

/* CSI / D-PHY ---------------------------------------------------------------*/
#define IMX219_REG_CSI_LANE_MODE        0x0114U
#define IMX219_CSI_2_LANE_MODE          0x01U
#define IMX219_CSI_4_LANE_MODE          0x03U

#define IMX219_REG_DPHY_CTRL            0x0128U
#define IMX219_DPHY_CTRL_TIMING_AUTO    0x00U

/* External clock frequency: value = MHz * 256, written as 16-bit big-endian */
#define IMX219_REG_EXCK_FREQ_MSB        0x012AU
#define IMX219_REG_EXCK_FREQ_LSB        0x012BU

/* PLL -----------------------------------------------------------------------*/
/*
 * 2-lane, 912 Mbps/lane, 24 MHz INCK:
 *
 *   PREPLLCK_VT_DIV = 3  -> VT pre-PLL = 24/3 = 8 MHz
 *   PLL_VT_MPY      = 57 -> PLLCK_VT   = 8 x 57 = 456 MHz
 *   VTPXCK_DIV      = 5  -> VTPXCK     = 456/5 = 91.2 MHz
 *   Pixel rate      = 2 x VTPXCK = 182.4 Mpix/s
 *
 *   PREPLLCK_OP_DIV = 3  -> OP pre-PLL = 8 MHz
 *   PLL_OP_MPY      = 114-> PLLCK_OP   = 8 x 114 = 912 MHz
 *   OPSYCK_DIV      = 1  -> OPSYCK     = 912 MHz
 *   Per-lane rate   = OPSYCK/2 * 2 (DDR) = 912 Mbps/lane  OK
 *   OPPXCK_DIV      = 10 for RAW10
 */
#define IMX219_REG_VTPXCK_DIV           0x0301U
#define IMX219_REG_VTSYCK_DIV           0x0303U
#define IMX219_REG_PREPLLCK_VT_DIV      0x0304U
#define IMX219_REG_PREPLLCK_OP_DIV      0x0305U
#define IMX219_REG_PLL_VT_MPY_MSB       0x0306U
#define IMX219_REG_PLL_VT_MPY_LSB       0x0307U
#define IMX219_REG_OPPXCK_DIV           0x0309U
#define IMX219_REG_OPSYCK_DIV           0x030BU
#define IMX219_REG_PLL_OP_MPY_MSB       0x030CU
#define IMX219_REG_PLL_OP_MPY_LSB       0x030DU

/* Analog / digital gain -----------------------------------------------------*/
#define IMX219_REG_ANALOG_GAIN          0x0157U  /* 8-bit,  range 0 to 232         */
#define IMX219_REG_DIGITAL_GAIN_MSB     0x0158U  /* 16-bit BE, Q8.8, 0x0100 = 1.0x */
#define IMX219_REG_DIGITAL_GAIN_LSB     0x0159U

/* Exposure (integration time) -----------------------------------------------*/
#define IMX219_REG_EXPOSURE_MSB         0x015AU  /* 16-bit BE, in lines */
#define IMX219_REG_EXPOSURE_LSB         0x015BU

/* Frame / line timing -------------------------------------------------------*/
#define IMX219_REG_VTS_MSB              0x0160U  /* Frame Length Lines, 16-bit BE */
#define IMX219_REG_VTS_LSB              0x0161U
#define IMX219_REG_HTS_MSB              0x0162U  /* Line Length Pixels, 16-bit BE */
#define IMX219_REG_HTS_LSB              0x0163U

/* Windowing / cropping ------------------------------------------------------*/
#define IMX219_REG_X_ADD_STA_A_MSB      0x0164U
#define IMX219_REG_X_ADD_STA_A_LSB      0x0165U
#define IMX219_REG_X_ADD_END_A_MSB      0x0166U
#define IMX219_REG_X_ADD_END_A_LSB      0x0167U
#define IMX219_REG_Y_ADD_STA_A_MSB      0x0168U
#define IMX219_REG_Y_ADD_STA_A_LSB      0x0169U
#define IMX219_REG_Y_ADD_END_A_MSB      0x016AU
#define IMX219_REG_Y_ADD_END_A_LSB      0x016BU
#define IMX219_REG_X_OUTPUT_SIZE_MSB    0x016CU
#define IMX219_REG_X_OUTPUT_SIZE_LSB    0x016DU
#define IMX219_REG_Y_OUTPUT_SIZE_MSB    0x016EU
#define IMX219_REG_Y_OUTPUT_SIZE_LSB    0x016FU
#define IMX219_REG_X_ODD_INC_A          0x0170U
#define IMX219_REG_Y_ODD_INC_A          0x0171U

/* Orientation (mirror / flip) -----------------------------------------------*/
#define IMX219_REG_ORIENTATION          0x0172U  /* bit0=HFLIP, bit1=VFLIP */
#define IMX219_ORIENTATION_NORMAL       0x00U    /* no flip, no mirror       */
#define IMX219_ORIENTATION_HFLIP        0x01U    /* bit0 set: horizontal mirror */
#define IMX219_ORIENTATION_VFLIP        0x02U    /* bit1 set: vertical flip   */
#define IMX219_ORIENTATION_HFLIP_VFLIP  0x03U    /* both bits set             */

/* Binning -------------------------------------------------------------------*/
#define IMX219_REG_BINNING_H            0x0174U
#define IMX219_REG_BINNING_V            0x0175U

/* Output format -------------------------------------------------------------*/
#define IMX219_REG_CSI_DATA_FORMAT_A_MSB  0x018CU
#define IMX219_REG_CSI_DATA_FORMAT_A_LSB  0x018DU

/* Test pattern --------------------------------------------------------------*/
#define IMX219_REG_TEST_PATTERN_MSB     0x0600U  /* 16-bit BE */
#define IMX219_REG_TEST_PATTERN_LSB     0x0601U
#define IMX219_TEST_PATTERN_DISABLE     0U
#define IMX219_TEST_PATTERN_SOLID_COLOR 1U
#define IMX219_TEST_PATTERN_COLOR_BARS  2U
#define IMX219_TEST_PATTERN_GREY_COLOR  3U
#define IMX219_TEST_PATTERN_PN9         4U
#define IMX219_REG_TP_WINDOW_WIDTH_MSB  0x0624U
#define IMX219_REG_TP_WINDOW_WIDTH_LSB  0x0625U
#define IMX219_REG_TP_WINDOW_HEIGHT_MSB 0x0626U
#define IMX219_REG_TP_WINDOW_HEIGHT_LSB 0x0627U

/* Sensor characteristics used by CMW / ISP layers --------------------------*/
#define IMX219_NAME                     "IMX219"
#define IMX219_BAYER_PATTERN            0        /* RGGB = ISP_DEMOS_TYPE_RGGB   */
#define IMX219_COLOR_DEPTH              10       /* bits per pixel               */

/*
 * Sensor active pixel array.
 * The middleware reports the maximum active array size here; individual
 * streaming modes are defined below.
 */
#define IMX219_WIDTH                    3280U
#define IMX219_HEIGHT                   2464U

#define IMX219_MODE_1080P_WIDTH         1920U
#define IMX219_MODE_1080P_HEIGHT        1080U
#define IMX219_MODE_FULL_WIDTH          3280U
#define IMX219_MODE_FULL_HEIGHT         2464U
#define IMX219_MODE_BINNED_WIDTH        1640U
#define IMX219_MODE_BINNED_HEIGHT       1232U

/*
 * Gain range in milli-dB (mdB):
 *
 *   Analog: gain_reg in [0, 232].
 *           Linear gain = 256 / (256 - gain_reg)
 *           Max: 256/24 = 10.67x -> 20.56 dB = 20560 mdB
 *
 *   Digital: Q8.8 register [0x0100, 0x0FFF]
 *            Max: 0x0FFF/256 = 15.99x -> 24.07 dB ~ 24070 mdB
 *
 *   Total max: 20560 + 24070 = 44630 mdB
 */
#define IMX219_GAIN_MIN                 (0)
#define IMX219_GAIN_MAX                 (44630)
#define IMX219_AGAIN_MAX                (20560)
#define IMX219_GAIN_DEFAULT             (0)

/*
 * Exposure range in microseconds at 1920x1080 @ 30 fps:
 *   Line period = HTS / pixel_rate = 3448 / 182400000 = 18.903 us
 *   Min = 4 lines = ~76 us
 *   Max = (VTS_30FPS - 4) = 1759 lines = ~33244 us
 */
#define IMX219_EXPOSURE_MIN             76      /* us */
#define IMX219_EXPOSURE_MAX             33244   /* us, sensor @ 30fps 1080p */

/* Minimum exposure in lines: sensor requires >= 4 lines integration time */
#define IMX219_EXPOSURE_MIN_LINES       4U
#define IMX219_EXPOSURE_OFFSET_LINES    4U

/* ANA_GAIN register limits */
#define IMX219_ANA_GAIN_MIN             0U
#define IMX219_ANA_GAIN_MAX             232U

/* Digital gain register limits (raw) */
#define IMX219_DGTL_GAIN_MIN            0x0100U  /* 1.0x */
#define IMX219_DGTL_GAIN_MAX            0x0FFFU  /* ~15.99x */

/**
  * @}
  */

/************** Generic Function  *******************/

typedef int32_t (*IMX219_Write_Func)(void *, uint16_t, uint8_t*, uint16_t);
typedef int32_t (*IMX219_Read_Func) (void *, uint16_t, uint8_t*, uint16_t);

typedef struct
{
  IMX219_Write_Func   WriteReg;
  IMX219_Read_Func    ReadReg;
  void               *handle;
} imx219_ctx_t;

/*******************************************************************************
* Register      : Generic - All
* Address       : Generic - All
* Bit Group Name: None
* Permission    : W
*******************************************************************************/
int32_t imx219_write_reg(imx219_ctx_t *ctx, uint16_t reg, uint8_t *pdata, uint16_t length);
int32_t imx219_read_reg (imx219_ctx_t *ctx, uint16_t reg, uint8_t *pdata, uint16_t length);
int32_t imx219_register_set(imx219_ctx_t *ctx, uint16_t reg, uint8_t value);


/**
  * @}
  */
#ifdef __cplusplus
}
#endif

#endif /* IMX219_REG_H */
/**
  * @}
  */

/**
  * @}
  */

/**
  * @}
  */
