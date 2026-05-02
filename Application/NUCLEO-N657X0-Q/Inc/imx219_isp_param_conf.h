/**
 ******************************************************************************
 * @file    imx219_isp_param_conf.h
 * @author  MDG Application Team
 * @brief   ISP IQ tuning parameters for the Sony IMX219 sensor.
 *
 * Starting-point parameters derived from the IMX335 tuning file and scaled
 * to the IMX219's 1920x1080 active output window and exposure range.
 * These values provide a functional baseline with AEC and AWB enabled; full
 * per-board calibration (CCM, lux reference, AWB gains) with the STM32 ISP
 * IQTune tool is recommended for production quality.
 *
 * Sensor characteristics used here:
 *   - Output window  : 1920 x 1080  (1080p cropped mode)
 *   - Bayer pattern  : RGGB
 *   - Bit depth      : 10-bit RAW
 *   - Exposure range : 76 µs – 33244 µs  @ 30 fps
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2024 STMicroelectronics.
 * All rights reserved.
 *
 * This software is licensed under SLA0044 terms that can be found here:
 * https://www.st.com/resource/en/license_agreement/SLA0044.txt
 *
 ******************************************************************************
 */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __IMX219_ISP_PARAM_CONF__H
#define __IMX219_ISP_PARAM_CONF__H

/* DCMIPP ISP configuration for IMX219 sensor */
static const ISP_IQParamTypeDef ISP_IQParamCacheInit_IMX219 = {
    .sensorGainStatic = {
        .gain = 0,
    },
    .sensorExposureStatic = {
        .exposure = 0,
    },
    .AECAlgo = {
        .enable = 1,
        .exposureCompensation = EXPOSURE_TARGET_0_0_EV,
        .antiFlickerFreq = ANTIFLICKER_NONE,
    },
    .statRemoval = {
        .enable = 0,
        .nbHeadLines = 0,
        .nbValidLines = 0,
    },
    .badPixelStatic = {
        .enable = 0,
        .strength = 0,
    },
    .badPixelAlgo = {
        .enable = 0,
        .threshold = 0,
    },
    .blackLevelStatic = {
        /* IMX219 pedestal: nominal 16 DN at 10-bit, ISP operates on 8-bit
         * normalised data so 12 is a reasonable starting value matching the
         * IMX335 baseline.  Adjust with IQTune if black-level clipping is
         * observed in dark scenes.
         */
        .enable = 1,
        .BLCR = 12,
        .BLCG = 12,
        .BLCB = 12,
    },
    .demosaicing = {
        .enable = 1,
        .type   = ISP_DEMOS_TYPE_RGGB,  /* IMX219 Bayer order: R Gr Gb B */
        .peak   = 2,
        .lineV  = 4,
        .lineH  = 4,
        .edge   = 6,
    },
    .ispGainStatic = {
        .enable  = 0,
        .ispGainR = 0,
        .ispGainG = 0,
        .ispGainB = 0,
    },
    .colorConvStatic = {
        .enable = 0,
        .coeff  = { { 0, 0, 0, }, { 0, 0, 0, }, { 0, 0, 0, }, }
    },
    .AWBAlgo = {
        .enable = 1,
        /*
         * Three-illuminant AWB model for the IMX219 RGGB sensor.
         *
         * ISP gains are scaled so that ispGainG = 100 000 000 == 1.0×.
         * Under each illuminant the gains correct the sensor's raw white
         * point to a neutral grey:
         *
         *   Tungsten  (~2 810 K): warm light → lower R gain, higher B gain
         *   CWF/TL84  (~4 015 K): cool-white fluorescent
         *   Daylight  (~6 650 K): outdoor → higher R gain, lower B gain
         *
         * CCM coefficients are scaled so that 100 000 000 == 1.0.  The
         * matrices below are starting-point values derived from IMX335
         * measured data; they will be similar for any RGGB sensor of this
         * generation.  Replace with measured values from IQTune for the
         * best accuracy.
         *
         * referenceRGB entries are average 8-bit R/G/B values observed on
         * a 24-patch colour chart under the respective illuminant — used by
         * the AWB judging algorithm to select the active illuminant.
         */
        .label = { "IMX219-A", "IMX219-TL84", "IMX219-DAY", "Free Slot", "Free Slot", },
        .referenceColorTemp = { 2810, 4015, 6650, 0, 0, },
        .ispGainR = { 124000000, 182000000, 244000000, 0, 0, },
        .ispGainG = { 100000000, 100000000, 100000000, 0, 0, },
        .ispGainB = { 282000000, 212000000, 143000000, 0, 0, },
        .coeff = {
            { { 82450000,  90560000, -73010000, }, { -65469999, 181020000, -15559999, }, { -30490000,  -52940000, 183430000, }, },
            { { 164670000, -20970000, -43700000, }, { -51330000, 178670000, -27339999, }, { -12490000,  -48170000, 160670000, }, },
            { { 150570000,   2440000, -53010000, }, { -37350000, 193760000, -56420000, }, { -11100000,  -35490000, 146590000, }, },
            { {         0,         0,         0, }, {         0,         0,         0, }, {         0,          0,         0, }, },
            { {         0,         0,         0, }, {         0,         0,         0, }, {         0,          0,         0, }, },
        },
        .referenceRGB = {
            { 61, 65, 30 },
            { 46, 68, 37 },
            { 38, 68, 49 },
            {  0,  0,  0 },
            {  0,  0,  0 },
        },
    },
    .contrast = {
        .enable       = 0,
        .coeff.LUM_0   = 0,
        .coeff.LUM_32  = 0,
        .coeff.LUM_64  = 0,
        .coeff.LUM_96  = 0,
        .coeff.LUM_128 = 0,
        .coeff.LUM_160 = 0,
        .coeff.LUM_192 = 0,
        .coeff.LUM_224 = 0,
        .coeff.LUM_256 = 0,
    },
    .statAreaStatic = {
        /*
         * Centre 50 % of the 1920 × 1080 output window:
         *   X0    = (1920 - 960) / 2 = 480
         *   Y0    = (1080 - 540) / 2 = 270
         *   XSize = 960
         *   YSize = 540
         */
        .X0    = 480,
        .Y0    = 270,
        .XSize = 960,
        .YSize = 540,
    },
    .gamma = {
        .enable = 1,
    },
    .sensorDelay = {
        /* 3-frame pipeline delay matches the IMX219 SMIA readout latency. */
        .delay = 3,
    },
    .luxRef = {
        /*
         * Lux reference calibrated for the IMX219 at 1920×1080 30 fps.
         * Line period ≈ 18.903 µs; exposure range: 76 µs – 33 244 µs.
         * Values derived from the IMX335 lux table scaled to the IMX219
         * exposure maximum of 33 244 µs.
         */
        .HL_LuxRef   = 1050,
        .HL_Expo1    = 5000,
        .HL_Lum1     = 20,
        .HL_Expo2    = 33244,
        .HL_Lum2     = 117,
        .LL_LuxRef   = 270,
        .LL_Expo1    = 9000,
        .LL_Lum1     = 10,
        .LL_Expo2    = 33244,
        .LL_Lum2     = 36,
        .calibFactor = 0.905f,
    },
};

#endif /* __IMX219_ISP_PARAM_CONF__H */
