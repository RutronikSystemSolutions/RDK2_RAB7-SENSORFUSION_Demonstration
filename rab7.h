/*
 * rab7.h
 *
 *  Created on: 2024-11-18
 *      Author: GDR
 */

#ifndef RAB7_H_
#define RAB7_H_

/*PSOC includes*/
#include "cy_pdl.h"
#include "cyhal.h"
#include "cybsp.h"

typedef struct app_sensor_data
{
	float dps_temperature;
	float dps_pressure;

	float bmp_temperature;
	float bmp_pressure;

	int16_t bmi_acc_x;
	int16_t bmi_acc_y;
	int16_t bmi_acc_z;

	int16_t bmi_gyr_x;
	int16_t bmi_gyr_y;
	int16_t bmi_gyr_z;

	float bme_temperature;
	float bme_pressure;
	float bme_humidity;
	float bme_gas_resistance;
	uint8_t bme_gas_index;
	uint8_t bme_status;
	uint8_t bme_meas_index;

	uint16_t sgp_sraw_voc;
	uint16_t sgp_sraw_nox;
	int32_t sgp_voc_index;
	int32_t sgp_nox_index;

	int32_t sht_temperature;
	int32_t sht_humidity;

	float bmm_temperature;
	float bmm_mag_x;
	float bmm_mag_y;
	float bmm_mag_z;

}sensor_data_t;

extern cyhal_i2c_t I2C_scb3;
extern sensor_data_t sensor_data_storage;

/*******************************************************************************
* Function Prototypes
*******************************************************************************/
void poll_sensors(void);
cy_rslt_t sensorfusion_init(void);

#endif /* RAB7_H_ */
