 /**
 ******************************************************************************
 * @file    stm32n6xx_it.c
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

/* Includes ------------------------------------------------------------------*/
#include "stm32n6xx_hal.h"
#include "stm32n6xx_it.h"

#include "cmw_camera.h"
#include "main.h"

#include <stdio.h>

static void DumpFaultState(const char *fault_name)
{
  static volatile uint32_t fault_dumped = 0;
  DCMIPP_HandleTypeDef *hcamera_dcmipp = CMW_CAMERA_GetDCMIPPHandle();

  __disable_irq();

  if (fault_dumped == 0U)
  {
    fault_dumped = 1U;
    printf("FATAL: %s stage=%lu frame=%lu tick=%lu MSP=0x%08lX PSP=0x%08lX CONTROL=0x%08lX\n",
           fault_name,
           (unsigned long) g_app_trace_stage,
           (unsigned long) g_app_trace_frame_index,
           (unsigned long) HAL_GetTick(),
           (unsigned long) __get_MSP(),
           (unsigned long) __get_PSP(),
           (unsigned long) __get_CONTROL());
    printf("FATAL: %s SCB CFSR=0x%08lX HFSR=0x%08lX DFSR=0x%08lX MMFAR=0x%08lX BFAR=0x%08lX ICSR=0x%08lX VTOR=0x%08lX\n",
           fault_name,
           (unsigned long) SCB->CFSR,
           (unsigned long) SCB->HFSR,
           (unsigned long) SCB->DFSR,
           (unsigned long) SCB->MMFAR,
           (unsigned long) SCB->BFAR,
           (unsigned long) SCB->ICSR,
           (unsigned long) SCB->VTOR);

    if (hcamera_dcmipp != NULL)
    {
      printf("FATAL: %s DCMIPP state=%lu pipe1=%lu pipe2=%lu err=0x%08lX cmsr1=0x%08lX cmsr2=0x%08lX cmier=0x%08lX\n",
             fault_name,
             (unsigned long) HAL_DCMIPP_GetState(hcamera_dcmipp),
             (unsigned long) HAL_DCMIPP_PIPE_GetState(hcamera_dcmipp, DCMIPP_PIPE1),
             (unsigned long) HAL_DCMIPP_PIPE_GetState(hcamera_dcmipp, DCMIPP_PIPE2),
             (unsigned long) HAL_DCMIPP_GetError(hcamera_dcmipp),
             (unsigned long) hcamera_dcmipp->Instance->CMSR1,
             (unsigned long) hcamera_dcmipp->Instance->CMSR2,
             (unsigned long) hcamera_dcmipp->Instance->CMIER);
    }
  }

  while (1)
  {
  }
}

/**
  * @brief   This function handles NMI exception.
  * @param  None
  * @retval None
  */
void NMI_Handler(void)
{
}

/**
  * @brief  This function handles Hard Fault exception.
  * @param  None
  * @retval None
  */
void HardFault_Handler(void)
{
  DumpFaultState("HardFault");
}

/**
  * @brief  This function handles Memory Manage exception.
  * @param  None
  * @retval None
  */
void MemManage_Handler(void)
{
  DumpFaultState("MemManage");
}

/**
  * @brief  This function handles Bus Fault exception.
  * @param  None
  * @retval None
  */
void BusFault_Handler(void)
{
  DumpFaultState("BusFault");
}

/**
  * @brief  This function handles Usage Fault exception.
  * @param  None
  * @retval None
  */
void UsageFault_Handler(void)
{
  DumpFaultState("UsageFault");
}

/**
  * @brief  This function handles Secure Fault exception.
  * @param  None
  * @retval None
  */
void SecureFault_Handler(void)
{
  DumpFaultState("SecureFault");
}

/**
  * @brief  This function handles SVCall exception.
  * @param  None
  * @retval None
  */
void SVC_Handler(void)
{
}

/**
  * @brief  This function handles Debug Monitor exception.
  * @param  None
  * @retval None
  */
void DebugMon_Handler(void)
{
  DumpFaultState("DebugMon");
}

/**
  * @brief  This function handles PendSVC exception.
  * @param  None
  * @retval None
  */
void PendSV_Handler(void)
{
  DumpFaultState("PendSV");
}

/**
  * @brief  This function handles SysTick Handler.
  * @param  None
  * @retval None
  */
void SysTick_Handler(void)
{
  HAL_IncTick();
}

/******************************************************************************/
/*                 STM32N6xx Peripherals Interrupt Handlers                   */
/*  Add here the Interrupt Handler for the used peripheral(s) (PPP), for the  */
/*  available peripheral interrupt handler's name please refer to the startup */
/*  file (startup_stm32n6xx.s).                                               */
/******************************************************************************/
void CSI_IRQHandler(void)
{
  DCMIPP_HandleTypeDef *hcamera_dcmipp = CMW_CAMERA_GetDCMIPPHandle();
  HAL_DCMIPP_CSI_IRQHandler(hcamera_dcmipp);
}

void DCMIPP_IRQHandler(void)
{
  DCMIPP_HandleTypeDef *hcamera_dcmipp = CMW_CAMERA_GetDCMIPPHandle();
  HAL_DCMIPP_IRQHandler(hcamera_dcmipp);
}
