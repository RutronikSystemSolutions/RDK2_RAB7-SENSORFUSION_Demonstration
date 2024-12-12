/*******************************************************************************
* File Name:   main.c
*
* Description: This is the source code for the RDK2 RAB7-SENSORFUSION
*              Application for ModusToolbox.
*
* Related Document: See README.md
*
*  Created on: 2024-11-27
*  Company: Rutronik Elektronische Bauelemente GmbH
*  Address: Jonavos g. 30, Kaunas 44262, Lithuania
*  Author: GDR
*
*
*******************************************************************************
* Copyright 2021-2024, Cypress Semiconductor Corporation (an Infineon company) or
* an affiliate of Cypress Semiconductor Corporation.  All rights reserved.
*
* This software, including source code, documentation and related
* materials ("Software") is owned by Cypress Semiconductor Corporation
* or one of its affiliates ("Cypress") and is protected by and subject to
* worldwide patent protection (United States and foreign),
* United States copyright laws and international treaty provisions.
* Therefore, you may use this Software only as provided in the license
* agreement accompanying the software package from which you
* obtained this Software ("EULA").
* If no EULA applies, Cypress hereby grants you a personal, non-exclusive,
* non-transferable license to copy, modify, and compile the Software
* source code solely for use in connection with Cypress's
* integrated circuit products.  Any reproduction, modification, translation,
* compilation, or representation of this Software except as specified
* above is prohibited without the express written permission of Cypress.
*
* Disclaimer: THIS SOFTWARE IS PROVIDED AS-IS, WITH NO WARRANTY OF ANY KIND,
* EXPRESS OR IMPLIED, INCLUDING, BUT NOT LIMITED TO, NONINFRINGEMENT, IMPLIED
* WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE. Cypress
* reserves the right to make changes to the Software without notice. Cypress
* does not assume any liability arising out of the application or use of the
* Software or any product or circuit described in the Software. Cypress does
* not authorize its products for use in any products where a malfunction or
* failure of the Cypress product may reasonably be expected to result in
* significant property damage, injury or death ("High Risk Product"). By
* including Cypress's product in a High Risk Product, the manufacturer
* of such system or application assumes all risk of such use and in doing
* so agrees to indemnify Cypress against all liability.
*
* Rutronik Elektronische Bauelemente GmbH Disclaimer: The evaluation board
* including the software is for testing purposes only and,
* because it has limited functions and limited resilience, is not suitable
* for permanent use under real conditions. If the evaluation board is
* nevertheless used under real conditions, this is done at one’s responsibility;
* any liability of Rutronik is insofar excluded
*******************************************************************************/

/*******************************************************************************
* Header Files
*******************************************************************************/
#include "cyhal.h"
#include "cybsp.h"
#include "cy_retarget_io.h"
#include "sys_timer.h"
#include "rab7.h"

#define PRINT_DATA_MS		1000
static uint32_t print_timestamp = 0;

int main(void)
{
    cy_rslt_t result;

    /* Initialize the device and board peripherals */
    result = cybsp_init();

    /* Board init failed. Stop program execution */
    if (result != CY_RSLT_SUCCESS)
    {
        CY_ASSERT(0);
    }

    /*Enable system time counter*/
	if (!sys_timer_init())
	{
		CY_ASSERT(0);
	}

    /* Enable global interrupts */
    __enable_irq();

    /*Initialise LEDs*/
    result = cyhal_gpio_init( LED1, CYHAL_GPIO_DIR_OUTPUT, CYHAL_GPIO_DRIVE_STRONG, CYBSP_LED_STATE_OFF);
    if (result != CY_RSLT_SUCCESS)
    {CY_ASSERT(0);}
    result = cyhal_gpio_init( LED2, CYHAL_GPIO_DIR_OUTPUT, CYHAL_GPIO_DRIVE_STRONG, CYBSP_LED_STATE_OFF);
    if (result != CY_RSLT_SUCCESS)
    {CY_ASSERT(0);}

    /*Enable debug output via KitProg UART*/
    result = cy_retarget_io_init( KITPROG_TX, KITPROG_RX, CY_RETARGET_IO_BAUDRATE);
    if (result != CY_RSLT_SUCCESS)
    {CY_ASSERT(0);}

    /*Initialise RAB7-SENSORFUSION adapter*/
    result = sensorfusion_init();

    /* RAB7-SENSORFUSION initialisation failed. Stop program execution */
    if (result != CY_RSLT_SUCCESS)
    {
    	printf("RAB7-SENSORFUSION initialisation failure\r\n");
        CY_ASSERT(0);
    }

	/*Initialize current time*/
	uint32_t currentTime = get_system_time_ms();

    for (;;)
    {
    	poll_sensors();

    	/*Print the data every PRINT_DATA_MS*/
    	currentTime = get_system_time_ms();
    	if((currentTime - print_timestamp) >  PRINT_DATA_MS)
    	{
    		cyhal_gpio_toggle(LED1);
    		print_timestamp = get_system_time_ms();

        	printf("\x1b[2J\x1b[;H");
        	printf("   [ Sensor Fusion Adapter Board Data Output ]  \r\n");
        	printf("DPS368 --> T: %.2f deg C, P: %.2f Pa\r\n",
        			sensor_data_storage.dps_temperature,
					sensor_data_storage.dps_pressure*100
					);
        	printf("BMP585 --> T: %.2f deg C, P: %.2f Pa\r\n",
        			sensor_data_storage.bmp_temperature,
					sensor_data_storage.bmp_pressure
					);
        	printf("BME690 --> T: %.2f deg C, H: %.2f %%, P: %.2f Pa, Gas: %d, R: %.2f Ohm\r\n",
        			sensor_data_storage.bme_temperature,
					sensor_data_storage.bme_humidity,
					sensor_data_storage.bme_pressure,
					sensor_data_storage.bme_gas_index,
					sensor_data_storage.bme_gas_resistance);
        	printf("BMI323 --> accx: %d accy: %d accz: %d gyrx: %d gyry: %d gyrz: %d\r\n",
        			(unsigned int)sensor_data_storage.bmi_acc_x,
					(unsigned int)sensor_data_storage.bmi_acc_y,
					(unsigned int)sensor_data_storage.bmi_acc_z,
					(unsigned int)sensor_data_storage.bmi_gyr_x,
					(unsigned int)sensor_data_storage.bmi_gyr_y,
					(unsigned int)sensor_data_storage.bmi_gyr_z
					);
        	printf("SHT41  --> T: %.2f deg C, H: %.2f %%\r\n",
        			(float)sensor_data_storage.sht_temperature/1000,
					(float)sensor_data_storage.sht_humidity/1000
					);
        	printf("SGP41  --> VOC: %d raw, VOC INDEX: %d, NOX: %d raw, NOX INDEX: %d \r\n",
        			(int)sensor_data_storage.sgp_sraw_voc,
					(int)sensor_data_storage.sgp_voc_index,
					(int)sensor_data_storage.sgp_sraw_nox,
					(int)sensor_data_storage.sgp_nox_index
					);
        	printf("BMM350 --> magx: %.2f uT, magy: %.2f uT, magz: %.2f uT, T: %.2f deg C\r\n",
        			sensor_data_storage.bmm_mag_x,
					sensor_data_storage.bmm_mag_y,
					sensor_data_storage.bmm_mag_z,
					sensor_data_storage.bmm_temperature
					);
    	}
    }
}
/* [] END OF FILE */
