/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    app_usbx_host.c
  * @author  MCD Application Team
  * @brief   USBX host applicative file
  ******************************************************************************
    * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "app_usbx_host.h"
#include "ux_api.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <msc_test/msc_test.h>

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
static TX_THREAD ux_host_app_thread;
/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
static VOID app_ux_host_thread_entry(ULONG thread_input);
static UINT ux_host_event_callback(ULONG event, UX_HOST_CLASS *current_class, VOID *current_instance);
static VOID ux_host_error_callback(UINT system_level, UINT system_context, UINT error_code);
/* USER CODE BEGIN PFP */
static void print_device_desc(const UX_DEVICE *dev);

/* USER CODE END PFP */

/**
  * @brief  Application USBX Host Initialization.
  * @param  memory_ptr: memory pointer
  * @retval status
  */
UINT MX_USBX_Host_Init(VOID *memory_ptr)
{
  UINT ret = UX_SUCCESS;

  UCHAR *pointer;
  TX_BYTE_POOL *byte_pool = (TX_BYTE_POOL*)memory_ptr;

  /* USER CODE BEGIN MX_USBX_Host_Init0 */

  /* USER CODE END MX_USBX_Host_Init0 */

  /* Allocate the stack for USBX Memory */
  if (tx_byte_allocate(byte_pool, (VOID **) &pointer,
                       USBX_HOST_MEMORY_STACK_SIZE, TX_NO_WAIT) != TX_SUCCESS)
  {
    /* USER CODE BEGIN USBX_ALLOCATE_STACK_ERROR */
    return TX_POOL_ERROR;
    /* USER CODE END USBX_ALLOCATE_STACK_ERROR */
  }
  /* Initialize USBX Memory */
  if (ux_system_initialize(pointer, USBX_HOST_MEMORY_STACK_SIZE, UX_NULL, 0) != UX_SUCCESS)
  {
    /* USER CODE BEGIN USBX_SYSTEM_INITIALIZE_ERROR */
    return UX_ERROR;
    /* USER CODE END USBX_SYSTEM_INITIALIZE_ERROR */
  }
  /* Install the host portion of USBX */
  if (ux_host_stack_initialize(ux_host_event_callback) != UX_SUCCESS)
  {
    /* USER CODE BEGIN USBX_HOST_INITIALIZE_ERROR */
    return UX_ERROR;
    /* USER CODE END USBX_HOST_INITIALIZE_ERROR */
  }

  /* Register a callback error function */
  ux_utility_error_callback_register(&ux_host_error_callback);
  /* Initialize the host hub class */
  if (ux_host_stack_class_register(_ux_system_host_class_hub_name,
                                   ux_host_class_hub_entry) != UX_SUCCESS)
  {
    /* USER CODE BEGIN USBX_HOST_HUB_REGISTER_ERROR */
    return UX_ERROR;
    /* USER CODE END USBX_HOST_HUB_REGISTER_ERROR */
  }

  /* Allocate the stack for host application main thread */
  if (tx_byte_allocate(byte_pool, (VOID **) &pointer, UX_HOST_APP_THREAD_STACK_SIZE,
                       TX_NO_WAIT) != TX_SUCCESS)
  {
    /* USER CODE BEGIN MAIN_THREAD_ALLOCATE_STACK_ERROR */
    return TX_POOL_ERROR;
    /* USER CODE END MAIN_THREAD_ALLOCATE_STACK_ERROR */
  }

  /* Create the host application main thread */
  if (tx_thread_create(&ux_host_app_thread, UX_HOST_APP_THREAD_NAME, app_ux_host_thread_entry,
                       0, pointer, UX_HOST_APP_THREAD_STACK_SIZE, UX_HOST_APP_THREAD_PRIO,
                       UX_HOST_APP_THREAD_PREEMPTION_THRESHOLD, UX_HOST_APP_THREAD_TIME_SLICE,
                       UX_HOST_APP_THREAD_START_OPTION) != TX_SUCCESS)
  {
    /* USER CODE BEGIN MAIN_THREAD_CREATE_ERROR */
    return TX_THREAD_ERROR;
    /* USER CODE END MAIN_THREAD_CREATE_ERROR */
  }

  /* USER CODE BEGIN MX_USBX_Host_Init1 */

  /* USER CODE END MX_USBX_Host_Init1 */

  return ret;
}

/**
  * @brief  Function implementing app_ux_host_thread_entry.
  * @param  thread_input: User thread input parameter.
  * @retval none
  */
static VOID app_ux_host_thread_entry(ULONG thread_input)
{
  /* USER CODE BEGIN app_ux_host_thread_entry */
  TX_PARAMETER_NOT_USED(thread_input);
  /* USER CODE END app_ux_host_thread_entry */
}

/**
  * @brief  ux_host_event_callback
  *         This callback is invoked to notify application of instance changes.
  * @param  event: event code.
  * @param  current_class: Pointer to class.
  * @param  current_instance: Pointer to class instance.
  * @retval status
  */
UINT ux_host_event_callback(ULONG event, UX_HOST_CLASS *current_class, VOID *current_instance)
{
  UINT status = UX_SUCCESS;

  /* USER CODE BEGIN ux_host_event_callback0 */
  UX_PARAMETER_NOT_USED(current_class);
  UX_PARAMETER_NOT_USED(current_instance);
  /* USER CODE END ux_host_event_callback0 */

  switch (event)
  {
    case UX_DEVICE_INSERTION:

      /* USER CODE BEGIN UX_DEVICE_INSERTION */
      printf("%s: UX_DEVICE_INSERTION\r\n", __func__);

      /* USER CODE END UX_DEVICE_INSERTION */

      break;

    case UX_DEVICE_REMOVAL:

      /* USER CODE BEGIN UX_DEVICE_REMOVAL */
      printf("%s: UX_DEVICE_REMOVAL\r\n", __func__);

      /* USER CODE END UX_DEVICE_REMOVAL */

      break;

    case UX_DEVICE_CONNECTION:

      /* USER CODE BEGIN UX_DEVICE_CONNECTION */
      printf("%s: UX_DEVICE_CONNECTION\r\n", __func__);
      print_device_desc((UX_DEVICE *)current_instance);
      msc_test_notify(current_instance);

      /* USER CODE END UX_DEVICE_CONNECTION */

      break;

    case UX_DEVICE_DISCONNECTION:

      /* USER CODE BEGIN UX_DEVICE_DISCONNECTION */
      printf("%s: UX_DEVICE_DISCONNECTION\r\n", __func__);
      msc_test_notify(NULL);

      /* USER CODE END UX_DEVICE_DISCONNECTION */

      break;

    default:

      /* USER CODE BEGIN EVENT_DEFAULT */
      printf("%s: unknown event\r\n", __func__);

      /* USER CODE END EVENT_DEFAULT */

      break;
  }

  /* USER CODE BEGIN ux_host_event_callback1 */

  /* USER CODE END ux_host_event_callback1 */

  return status;
}

/**
  * @brief ux_host_error_callback
  *         This callback is invoked to notify application of error changes.
  * @param  system_level: system level parameter.
  * @param  system_context: system context code.
  * @param  error_code: error event code.
  * @retval Status
  */
VOID ux_host_error_callback(UINT system_level, UINT system_context, UINT error_code)
{
  /* USER CODE BEGIN ux_host_error_callback0 */
  UX_PARAMETER_NOT_USED(system_level);
  UX_PARAMETER_NOT_USED(system_context);
  /* USER CODE END ux_host_error_callback0 */

  switch (error_code)
  {
    case UX_DEVICE_ENUMERATION_FAILURE:

      /* USER CODE BEGIN UX_DEVICE_ENUMERATION_FAILURE */
      printf("%s: UX_DEVICE_ENUMERATION_FAILURE\r\n", __func__);

      /* USER CODE END UX_DEVICE_ENUMERATION_FAILURE */

      break;

    case  UX_NO_DEVICE_CONNECTED:

      /* USER CODE BEGIN UX_NO_DEVICE_CONNECTED */
      printf("%s: UX_NO_DEVICE_CONNECTED\r\n", __func__);

      /* USER CODE END UX_NO_DEVICE_CONNECTED */

      break;

    default:

      /* USER CODE BEGIN ERROR_DEFAULT */

      /* USER CODE END ERROR_DEFAULT */

      break;
  }

  /* USER CODE BEGIN ux_host_error_callback1 */
  switch (error_code)
  {
    case UX_MEMORY_CORRUPTED:
      printf("%s: UX_MEMORY_CORRUPTED\r\n", __func__);
      break;
    case UX_MEMORY_INSUFFICIENT:
      printf("%s: UX_MEMORY_INSUFFICIENT\r\n", __func__);
      break;
    case TX_WAIT_ERROR:
      break;
    case UX_DEVICE_ENUMERATION_FAILURE:
      printf("%s: UX_DEVICE_ENUMERATION_FAILURE\r\n", __func__);
      break;
    case UX_ENDPOINT_HANDLE_UNKNOWN:
      printf("%s: UX_ENDPOINT_HANDLE_UNKNOWN\r\n", __func__);
      break;
    case UX_HOST_CLASS_PROTOCOL_ERROR:
      printf("%s: UX_HOST_CLASS_PROTOCOL_ERROR\r\n", __func__);
      break;
    default:
      printf("%s: unknown error\r\n", __func__);
      break;
  }
  /* USER CODE END ux_host_error_callback1 */
}
/* USER CODE BEGIN 1 */
static void print_device_desc(const UX_DEVICE *dev)
{
    const UX_DEVICE_DESCRIPTOR *d = &dev->ux_device_descriptor;

    printf("Device Descriptor:\r\n");
    printf("  VID: %04x\r\n", d->idVendor);
    printf("  PID: %04x\r\n", d->idProduct);
    printf("  bcdUSB: %04x\r\n", d->bcdUSB);
    printf("  bDeviceClass/Sub/Prot: %02x/%02x/%02x\r\n",
           d->bDeviceClass, d->bDeviceSubClass, d->bDeviceProtocol);
    printf("  bMaxPacketSize0: %u\r\n", d->bMaxPacketSize0);
    printf("  iMfr/iProd/iSN: %u/%u/%u\r\n",
           d->iManufacturer, d->iProduct, d->iSerialNumber);
}

/* USER CODE END 1 */
