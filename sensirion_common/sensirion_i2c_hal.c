/*
 * Copyright (c) 2018, Sensirion AG
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * * Redistributions of source code must retain the above copyright notice, this
 *   list of conditions and the following disclaimer.
 *
 * * Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 *
 * * Neither the name of Sensirion AG nor the names of its
 *   contributors may be used to endorse or promote products derived from
 *   this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "sensirion_i2c_hal.h"
#include "sensirion_common.h"
#include "sensirion_config.h"

/*PSoC62 Header Files*/
#include "cyhal.h"

#ifdef FREERTOS_APP
/*FreeRTOS Header Files*/
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "sensors_task.h"
#endif

extern cyhal_i2c_t I2C_scb3;

/*
 * INSTRUCTIONS
 * ============
 *
 * Implement all functions where they are marked as IMPLEMENT.
 * Follow the function specification in the comments.
 */

/**
 * Select the current i2c bus by index.
 * All following i2c operations will be directed at that bus.
 *
 * THE IMPLEMENTATION IS OPTIONAL ON SINGLE-BUS SETUPS (all sensors on the same
 * bus)
 *
 * @param bus_idx   Bus index to select
 * @returns         0 on success, an error code otherwise
 */
int16_t sensirion_i2c_hal_select_bus(uint8_t bus_idx) {
    /* TODO:IMPLEMENT or leave empty if all sensors are located on one single
     * bus
     */
    return NOT_IMPLEMENTED_ERROR;
}

/**
 * Initialize all hard- and software components that are needed for the I2C
 * communication.
 */
void sensirion_i2c_hal_init(void) {
    /* TODO:IMPLEMENT */
}

/**
 * Release all resources initialized by sensirion_i2c_hal_init().
 */
void sensirion_i2c_hal_free(void) {
    /* TODO:IMPLEMENT or leave empty if no resources need to be freed */
}

/**
 * Execute one read transaction on the I2C bus, reading a given number of bytes.
 * If the device does not acknowledge the read command, an error shall be
 * returned.
 *
 * @param address 7-bit I2C address to read from
 * @param data    pointer to the buffer where the data is to be stored
 * @param count   number of bytes to read from I2C and store in the buffer
 * @returns 0 on success, error code otherwise
 */
int8_t sensirion_i2c_hal_read(uint8_t address, uint8_t* data, uint16_t count)
{
	cy_rslt_t result;

#ifndef FREERTOS_APP
	result = cyhal_i2c_master_read(&I2C_scb3,(uint16_t)address, data, count, 10, true);
    if (result != CY_RSLT_SUCCESS)
    {
    	return -99;
    }
#else
	xSemaphoreTake(i2c_mutex, portMAX_DELAY);
	result = cyhal_i2c_master_read(&I2C_scb3,(uint16_t)address, data, count, 10, true);
    if (result != CY_RSLT_SUCCESS)
    {
    	xSemaphoreGive(i2c_mutex);
    	return -99;
    }
	xSemaphoreGive(i2c_mutex);
	return (int8_t)result;
#endif

	return (int8_t)result;
}

/**
 * Execute one write transaction on the I2C bus, sending a given number of
 * bytes. The bytes in the supplied buffer must be sent to the given address. If
 * the slave device does not acknowledge any of the bytes, an error shall be
 * returned.
 *
 * @param address 7-bit I2C address to write to
 * @param data    pointer to the buffer containing the data to write
 * @param count   number of bytes to read from the buffer and send over I2C
 * @returns 0 on success, error code otherwise
 */
int8_t sensirion_i2c_hal_write(uint8_t address, const uint8_t* data, uint16_t count)
{
	cy_rslt_t result;

#ifndef FREERTOS_APP
	result = cyhal_i2c_master_write(&I2C_scb3,(uint16_t)address, data, count, 10, true);
    if (result != CY_RSLT_SUCCESS)
    {
    	return -99;
    }
#else
    xSemaphoreTake(i2c_mutex, portMAX_DELAY);
    result = cyhal_i2c_master_write(&I2C_scb3,(uint16_t)address, data, count, 10, true);
    if (result != CY_RSLT_SUCCESS)
    {
    	xSemaphoreGive(i2c_mutex);
    	return -99;
    }
	xSemaphoreGive(i2c_mutex);
	return (int8_t)result;
#endif

	return (int8_t)result;
}

/**
 * Sleep for a given number of microseconds. The function should delay the
 * execution for at least the given time, but may also sleep longer.
 *
 * Despite the unit, a <10 millisecond precision is sufficient.
 *
 * @param useconds the sleep time in microseconds
 */
void sensirion_i2c_hal_sleep_usec(uint32_t useconds)
{
#ifndef FREERTOS_APP
	if(useconds <= 0xFFFF)
	{
		CyDelayUs((uint16_t)useconds);
	}
	else
	{
		CyDelay(useconds/1000);
	}
#else
	if(useconds <= 1000)
	{
		CyDelayUs((uint16_t)useconds);
	}
	else
	{
		vTaskDelay(pdMS_TO_TICKS(useconds/1000));
	}
#endif
}
