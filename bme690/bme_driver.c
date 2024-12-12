/**
 * Copyright (C) 2024 Bosch Sensortec GmbH. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

/*PSOC platform includes*/
#include "cy_pdl.h"
#include "cyhal.h"
#include "cybsp.h"

extern cyhal_i2c_t I2C_scb3;

#include "bme69x.h"
#include "bme_driver.h"

/******************************************************************************/
/*!                 Macro definitions                                         */
/*! BME69X shuttle board ID */
#define BME69X_SHUTTLE_ID  0x93

/******************************************************************************/
/*!                Static variable definition                                 */
static uint8_t dev_addr;

/******************************************************************************/
/*!                User interface functions                                   */

/*!
 * I2C read function map to RDK2 platform
 */
BME69X_INTF_RET_TYPE bme69x_i2c_read(uint8_t reg_addr, uint8_t *reg_data, uint32_t len, void *intf_ptr)
{
	cy_rslt_t result;
    uint8_t device_addr = *(uint8_t*)intf_ptr;

    (void)intf_ptr;

	result = cyhal_i2c_master_write( &I2C_scb3, (uint16_t)device_addr, &reg_addr, 1, 100, false );
    if (result != CY_RSLT_SUCCESS)
    {
    	return BME69X_E_COM_FAIL;
    }

	result = (int8_t)cyhal_i2c_master_read(&I2C_scb3,(uint16_t)device_addr, reg_data, (uint16_t)len, 100, true);
	if (result != CY_RSLT_SUCCESS)
	{
		 return BME69X_E_COM_FAIL;
	}

	return BME69X_OK;
}

/*!
 * I2C write function map to RDK2 platform
 */
BME69X_INTF_RET_TYPE bme69x_i2c_write(uint8_t reg_addr, const uint8_t *reg_data, uint32_t len, void *intf_ptr)
{
	cy_rslt_t result;
    uint8_t device_addr = *(uint8_t*)intf_ptr;
    uint8_t* i2c_data = NULL;

	/*Allocate buffer for transmission*/
    i2c_data = malloc(len+1);
    if(i2c_data == NULL)
    {
    	return BME69X_E_NULL_PTR;
    }
    /*Copy register address and all the data*/
    i2c_data[0] = reg_addr;
    memcpy(&i2c_data[1], reg_data, len);

    /*Write data to I2C*/
	result = cyhal_i2c_master_write( &I2C_scb3, (uint16_t)device_addr, i2c_data, len+1, 100, true);
    if (result != CY_RSLT_SUCCESS)
    {
    	free(i2c_data);
    	return BME69X_E_COM_FAIL;
    }

    free(i2c_data);
    return BME69X_OK;
}

/*!
 * SPI read function map to RDK2 platform
 */
BME69X_INTF_RET_TYPE bme69x_spi_read(uint8_t reg_addr, uint8_t *reg_data, uint32_t len, void *intf_ptr)
{
	return BME69X_E_COM_FAIL;
}

/*!
 * SPI write function map to RDK2 platform
 */
BME69X_INTF_RET_TYPE bme69x_spi_write(uint8_t reg_addr, const uint8_t *reg_data, uint32_t len, void *intf_ptr)
{
	return BME69X_E_COM_FAIL;
}

/*!
 * Delay function map to RDK2 platform
 */
void bme69x_delay_us(uint32_t period, void *intf_ptr)
{
	if(period <= 0xFFFF)
	{
		CyDelayUs(period);
	}
	else
	{
		CyDelay(period/1000);
	}
}

void bme69x_check_rslt(const char api_name[], int8_t rslt)
{
    switch (rslt)
    {
        case BME69X_OK:

            /* Do nothing */
            break;
        case BME69X_E_NULL_PTR:
            printf("API name [%s]  Error [%d] : Null pointer\r\n", api_name, rslt);
            break;
        case BME69X_E_COM_FAIL:
            printf("API name [%s]  Error [%d] : Communication failure\r\n", api_name, rslt);
            break;
        case BME69X_E_INVALID_LENGTH:
            printf("API name [%s]  Error [%d] : Incorrect length parameter\r\n", api_name, rslt);
            break;
        case BME69X_E_DEV_NOT_FOUND:
            printf("API name [%s]  Error [%d] : Device not found\r\n", api_name, rslt);
            break;
        case BME69X_E_SELF_TEST:
            printf("API name [%s]  Error [%d] : Self test error\r\n", api_name, rslt);
            break;
        case BME69X_W_NO_NEW_DATA:
            printf("API name [%s]  Warning [%d] : No new data found\r\n", api_name, rslt);
            break;
        default:
            printf("API name [%s]  Error [%d] : Unknown error code\r\n", api_name, rslt);
            break;
    }
}

int8_t bme69x_interface_init(struct bme69x_dev *bme, uint8_t intf)
{
    if (bme != NULL)
    {
        /* Bus configuration : I2C */
        if (intf == BME69X_I2C_INTF)
        {
        	printf("BME69x I2C Interface Initialized.\r\n");
            dev_addr = BME69X_I2C_ADDR_LOW;
            bme->read = bme69x_i2c_read;
            bme->write = bme69x_i2c_write;
            bme->intf = BME69X_I2C_INTF;
        }
        /* Bus configuration : SPI */
        else if (intf == BME69X_SPI_INTF)
        {
	        printf("SPI Interface not supported.\r\n");
	        return BME69X_E_DEV_NOT_FOUND;
        }

        bme->delay_us = bme69x_delay_us;
        bme->intf_ptr = &dev_addr;
        bme->amb_temp = 25; /* The ambient temperature in deg C is used for defining the heater temperature */
    }
    else
    {
        return BME69X_E_NULL_PTR;
    }

    return BME69X_OK;
}

void bme69x_coines_deinit(void)
{
	return;
}
