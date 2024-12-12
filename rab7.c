/*
 * rab7.c
 *
 *  Created on: 2024-11-18
 *      Author: GDR
 */

#include "rab7.h"
#include "sys_timer.h"
#include "sht4x.h"
#include "sgp41.h"
#include "sensirion_gas_index_algorithm.h"
#include "xensiv_dps3xx.h"
#include "xensiv_dps3xx_mtb.h"
#include "bme69x.h"
#include "bme_driver.h"
#include "bmi3_defs.h"
#include "bmi323_defs.h"
#include "bmi_driver.h"
#include "bmi323.h"
#include "bmm350_defs.h"
#include "bmm_driver.h"
#include "bmm350.h"
#include "bmp5_defs.h"
#include "bmp_driver.h"
#include "bmp5.h"


/*Priorities for sensor interrupts*/
#define BMP_IRQ_PRIORITY		6
#define BMI_IRQ_PRIORITY		5
#define BMM_IRQ_PRIORITY		4

#define SHT_DATA_RDY_MS		10
#define SGP_COND_TIME_MS	10000
#define SGP_COND_CMD_MS		50
#define SGP_DATA_RDY_MS		50
#define SGP_READ_MS			1000
#define SGP_DEFAULT_RH		0x8000
#define SGP_DEFAULT_T		0x6666
#define DPS_DATA_RDY_MS		250
#define BME69X_VALID_DATA  	UINT8_C(0xB0)

static int8_t set_accel_gyro_config(struct bmi3_dev *bmi3_dev);

/*I2C Device Global Variables*/
cyhal_i2c_t I2C_scb3;
cyhal_i2c_cfg_t i2c_scb3_cfg =
{
		.is_slave = false,
	    .address = 0,
	    .frequencyhal_hz = 400000UL,
};

/*A structure holding all the data gathered from the various sensors*/
sensor_data_t sensor_data_storage = {0};

/*Time stamps */
static uint32_t sht_process_timestamp = 0;
static uint32_t sgp_cond_cmd_timestamp = 0;
static uint32_t sgp_cond_remaining = SGP_COND_TIME_MS;
static uint32_t sgp_process_timestamp = 0;
static uint32_t dps_process_timestamp = 0;
static uint32_t bme_process_timestamp = 0;

/*Gas index parameters*/
static GasIndexAlgorithmParams voc_params;
static GasIndexAlgorithmParams nox_params;

xensiv_dps3xx_t dps368_sensor = {0};
xensiv_dps3xx_config_t dps368_config =
{
    .dev_mode               = XENSIV_DPS3XX_MODE_BACKGROUND_ALL, 	// Set device to read temp & pressure
    .pressure_rate          = XENSIV_DPS3XX_RATE_4,        			// 1x sample rate for pressure
    .temperature_rate       = XENSIV_DPS3XX_RATE_4,        			// 1x sample rate for data
    .pressure_oversample    = XENSIV_DPS3XX_OVERSAMPLE_1,  			// 1x oversample for pressure
    .temperature_oversample = XENSIV_DPS3XX_OVERSAMPLE_1,  			// 1x oversample for temp
    .data_timeout           = 1,                      				// Wait up to 500ms for measurement data
    .i2c_timeout            = 5,                       				// Wait up to 10ms for i2c operations
};

/* Sensor Fusion Interrupts */
void bmi_interrupt_handler(void *handler_arg, cyhal_gpio_event_t event);
cyhal_gpio_callback_data_t bmi_int_data =
{
		.callback = bmi_interrupt_handler,
		.callback_arg = NULL,

};
_Bool bmi_int_flag = false;
void bmm_interrupt_handler(void *handler_arg, cyhal_gpio_event_t event);
cyhal_gpio_callback_data_t bmm_int_data =
{
		.callback = bmm_interrupt_handler,
		.callback_arg = NULL,

};
_Bool bmm_int_flag = false;
void bmp_interrupt_handler(void *handler_arg, cyhal_gpio_event_t event);
cyhal_gpio_callback_data_t bmp_int_data =
{
		.callback = bmp_interrupt_handler,
		.callback_arg = NULL,

};
_Bool bmp_int_flag = false;

/* BME690 */
struct bme69x_dev bme;
struct bme69x_conf conf;
struct bme69x_heatr_conf heatr_conf;
uint32_t bme_del_period;
struct bme69x_data data[3];
uint8_t n_fields;
/* Heater temperature in degree Celsius */
uint16_t temp_prof[10] = { 320, 100, 100, 100, 200, 200, 200, 320, 320, 320 };
/* Multiplier to the shared heater duration */
uint16_t mul_prof[10] = { 5, 2, 10, 30, 5, 5, 5, 5, 5, 5 };
/*BMI323*/
struct bmi3_dev bmi = { 0 };
struct bmi3_sens_config config[2];
/*Interrupt output pins configuration structure*/
const struct bmi3_int_pin_config int_config =
{
		.pin_type = BMI3_INT1,
		.int_latch = BMI3_INT_LATCH_EN,
		.pin_cfg[0].lvl = BMI3_INT_ACTIVE_HIGH,
		.pin_cfg[0].od = BMI3_INT_PUSH_PULL,
		.pin_cfg[0].output_en = BMI3_INT_OUTPUT_ENABLE,
		.pin_cfg[1].lvl = BMI3_INT_ACTIVE_LOW,
		.pin_cfg[1].od = BMI3_INT_PUSH_PULL,
		.pin_cfg[1].output_en = BMI3_INT_OUTPUT_DISABLE,
};
struct bmi3_sensor_data bmi_sensor_data[2] = { 0 };
/*BMM350*/
struct bmm350_dev bmm = { 0 };
struct bmm350_mag_temp_data bmm_data;
struct bmm350_pmu_cmd_status_0 pmu_cmd_stat_0;
uint8_t bmm_set_int_ctrl, bmm_int_status, bmm_int_ctrl, bmm_err_reg_data = 0;
/*BMP585*/
struct bmp5_dev bmp = { 0 };
struct bmp5_osr_odr_press_config osr_odr_press_cfg = { 0 };
struct bmp5_iir_config set_iir_cfg;
struct bmp5_int_source_select int_source_select;
uint8_t bmp_int_status;
struct bmp5_sensor_data bmp_data;

void poll_sensors(void)
{
	int8_t rslt;
    uint16_t int_status = 0;

	/*Initialize current time*/
	uint32_t currentTime = get_system_time_ms();

	/*Process the SHT41*/
	if((currentTime - sht_process_timestamp) >  SHT_DATA_RDY_MS)
	{
		sht4x_read(&sensor_data_storage.sht_temperature, &sensor_data_storage.sht_humidity);
		sht4x_measure();
		sht_process_timestamp = get_system_time_ms();
	}

	/*Execute the SGP41 conditioning*/
	if(sgp_cond_remaining)
	{
		/*Send the command every 50 milliseconds until conditioning is over*/
		if((currentTime - sgp_cond_cmd_timestamp) >  SGP_COND_CMD_MS)
		{
			sgp41_conditioning_cmd(SGP_DEFAULT_RH, SGP_DEFAULT_T);
			if(sgp_cond_remaining >= SGP_COND_CMD_MS)
			{
				sgp_cond_remaining = sgp_cond_remaining - SGP_COND_CMD_MS;
			}
			sgp_cond_cmd_timestamp = get_system_time_ms();
		}
	}
	/*After the SGP41 conditioning do the measurements*/
	else
	{
		if((currentTime - sgp_process_timestamp) > SGP_READ_MS && (currentTime - sgp_process_timestamp) > SGP_DATA_RDY_MS)
		{
			sgp41_read_raw_cmd(&sensor_data_storage.sgp_sraw_voc, &sensor_data_storage.sgp_sraw_nox);
	        uint16 comp_rh = (uint16_t)sensor_data_storage.sht_humidity * 65535 / 100;
	        uint16_t comp_t = (uint16_t)(sensor_data_storage.sht_temperature + 45) * 65535 / 175;
	        sgp41_measure_raw_cmd(comp_rh, comp_t);
			//sgp41_measure_raw_cmd(SGP_DEFAULT_RH, SGP_DEFAULT_T);
            GasIndexAlgorithm_process(&voc_params, sensor_data_storage.sgp_sraw_voc, &sensor_data_storage.sgp_voc_index);
            GasIndexAlgorithm_process(&nox_params, sensor_data_storage.sgp_sraw_nox, &sensor_data_storage.sgp_nox_index);
			sgp_process_timestamp = get_system_time_ms();
		}
	}

	/*Process the DSP368*/
	if((currentTime - dps_process_timestamp) >  DPS_DATA_RDY_MS)
	{
		xensiv_dps3xx_read(&dps368_sensor, &sensor_data_storage.dps_pressure, &sensor_data_storage.dps_temperature);
		dps_process_timestamp = get_system_time_ms();
	}

	/*Process the BME690*/
	if((currentTime - bme_process_timestamp) >  bme_del_period)
	{
        bme69x_get_data(BME69X_PARALLEL_MODE, data, &n_fields, &bme);
        for (uint8_t i = 0; i < n_fields; i++)
        {
            if (data[i].status == BME69X_VALID_DATA)
            {
            	sensor_data_storage.bme_temperature = data[i].temperature;
            	sensor_data_storage.bme_pressure = data[i].pressure;
            	sensor_data_storage.bme_humidity = data[i].humidity;
            	sensor_data_storage.bme_gas_resistance = data[i].gas_resistance;
            	sensor_data_storage.bme_status = data[i].status;
            	sensor_data_storage.bme_gas_index =  data[i].gas_index;
            	sensor_data_storage.bme_meas_index = data[i].meas_index;
            }
        }
		bme_process_timestamp = get_system_time_ms();
	}

	/*Process the BMI323*/
	if(bmi_int_flag)
	{
        rslt = bmi323_get_int1_status(&int_status, &bmi);
        bmi_int_flag = false;
        bmi3_error_codes_print_result("bmi323_get_int1_status", rslt);

        if(rslt == BMI323_OK)
        {
        	if (int_status & BMI3_INT_STATUS_ACC_DRDY)
            {
            	 rslt = bmi323_get_sensor_data(&bmi_sensor_data[0], 1, &bmi);
            	 bmi3_error_codes_print_result("Get accelerometer data", rslt);

            	 sensor_data_storage.bmi_acc_x = bmi_sensor_data[0].sens_data.acc.x;
            	 sensor_data_storage.bmi_acc_y = bmi_sensor_data[0].sens_data.acc.y;
            	 sensor_data_storage.bmi_acc_z = bmi_sensor_data[0].sens_data.acc.z;
            }
            if (int_status & BMI3_INT_STATUS_GYR_DRDY)
            {
            	rslt = bmi323_get_sensor_data(&bmi_sensor_data[1], 1, &bmi);
            	bmi3_error_codes_print_result("Get gyroscope data", rslt);

            	sensor_data_storage.bmi_gyr_x = bmi_sensor_data[1].sens_data.gyr.x;
            	sensor_data_storage.bmi_gyr_y = bmi_sensor_data[1].sens_data.gyr.y;
            	sensor_data_storage.bmi_gyr_z = bmi_sensor_data[1].sens_data.gyr.z;
            }
        }
	}

	/*Process the BMM350*/
	if(bmm_int_flag)
	{
		bmm_int_flag = false;
		bmm_int_status = 0;

        rslt = bmm350_get_regs(BMM350_REG_INT_STATUS, &bmm_int_status, 1, &bmm);
        bmm350_error_codes_print_result("bmm350_get_regs", rslt);

        /* Check if data ready interrupt occurred */
        if (bmm_int_status & BMM350_DRDY_DATA_REG_MSK)
        {
            rslt = bmm350_get_compensated_mag_xyz_temp_data(&bmm_data, &bmm);
            bmm350_error_codes_print_result("bmm350_get_compensated_mag_xyz_temp_data", rslt);

            sensor_data_storage.bmm_temperature = bmm_data.temperature;
            sensor_data_storage.bmm_mag_x = bmm_data.x;
            sensor_data_storage.bmm_mag_y = bmm_data.y;
            sensor_data_storage.bmm_mag_z = bmm_data.z;
        }
	}

	/*Process the BMP585*/
	if(bmp_int_flag)
	{
		bmp_int_flag = false;
		bmp_int_status = 0;

        rslt = bmp5_get_interrupt_status(&bmp_int_status, &bmp);
        bmp5_error_codes_print_result("bmp5_get_interrupt_status", rslt);
        if (bmp_int_status & BMP5_INT_ASSERTED_DRDY)
        {
            rslt = bmp5_get_sensor_data(&bmp_data, &osr_odr_press_cfg, &bmp);
            bmp5_error_codes_print_result("bmp5_get_sensor_data", rslt);
            if (rslt == BMP5_OK)
            {
            	sensor_data_storage.bmp_pressure = bmp_data.pressure;
            	sensor_data_storage.bmp_temperature = bmp_data.temperature;
            }
        }
	}
}

cy_rslt_t sensorfusion_init(void)
{
	cy_rslt_t result = CY_RSLT_SUCCESS;
	int16_t error = 0;
	int8_t rslt;

	/*Force I2C to stop in case we have a sensor hanging*/
    result = cyhal_gpio_init(ARDU_SDA, CYHAL_GPIO_DIR_OUTPUT, CYHAL_GPIO_DRIVE_STRONG, true);
    if (result != CY_RSLT_SUCCESS)
    {CY_ASSERT(0);}
    result = cyhal_gpio_init(ARDU_SCL, CYHAL_GPIO_DIR_OUTPUT, CYHAL_GPIO_DRIVE_STRONG, true);
    if (result != CY_RSLT_SUCCESS)
    {CY_ASSERT(0);}
    cyhal_gpio_write(ARDU_SDA,false);
    CyDelay(1);
    cyhal_gpio_write(ARDU_SCL,false);
    CyDelay(1);
    cyhal_gpio_free(ARDU_SDA);
    cyhal_gpio_free(ARDU_SCL);

    /*Initialise I2C Master*/
    result = cyhal_i2c_init(&I2C_scb3, ARDU_SDA, ARDU_SCL, NULL);
    if (result != CY_RSLT_SUCCESS)
    {
    	return result;
    }
    result = cyhal_i2c_configure(&I2C_scb3, &i2c_scb3_cfg);
    if (result != CY_RSLT_SUCCESS)
    {
    	return result;
    }

    /*Initialise Sensor Fusion Board Interrupts*/
    result = cyhal_gpio_init(ARDU_IO4, CYHAL_GPIO_DIR_INPUT, CYHAL_GPIO_DRIVE_NONE, false);
    if (result != CY_RSLT_SUCCESS)
    {CY_ASSERT(0);}
    /*Register callback function */
    cyhal_gpio_register_callback(ARDU_IO4, &bmi_int_data);
    /* Enable rising edge interrupt events */
    cyhal_gpio_enable_event(ARDU_IO4, CYHAL_GPIO_IRQ_RISE, BMI_IRQ_PRIORITY, true);

    result = cyhal_gpio_init(ARDU_IO6, CYHAL_GPIO_DIR_INPUT, CYHAL_GPIO_DRIVE_NONE, false);
    if (result != CY_RSLT_SUCCESS)
    {CY_ASSERT(0);}
    /*Register callback function */
    cyhal_gpio_register_callback(ARDU_IO6, &bmm_int_data);
    /* Enable rising edge interrupt events */
    cyhal_gpio_enable_event(ARDU_IO6, CYHAL_GPIO_IRQ_RISE, BMM_IRQ_PRIORITY, true);

    result = cyhal_gpio_init(ARDU_IO7, CYHAL_GPIO_DIR_INPUT, CYHAL_GPIO_DRIVE_NONE, false);
    if (result != CY_RSLT_SUCCESS)
    {CY_ASSERT(0);}
    /*Register callback function */
    cyhal_gpio_register_callback(ARDU_IO7, &bmp_int_data);
    /* Enable rising edge interrupt events */
    cyhal_gpio_enable_event(ARDU_IO7, CYHAL_GPIO_IRQ_RISE, BMP_IRQ_PRIORITY, true);

    /*Check the SHT4x sensor*/
    if(sht4x_probe() != SHT4X_STATUS_OK)
    {
    	return 1;
    }

    uint16_t serial_number[3] = {0};
    error = sgp41_get_serial_number(serial_number);
    if (error)
    {
    	return 1;
    }

    /*Initialise gas index parameters*/
    GasIndexAlgorithm_init(&voc_params, GasIndexAlgorithm_ALGORITHM_TYPE_VOC);
    GasIndexAlgorithm_init(&nox_params, GasIndexAlgorithm_ALGORITHM_TYPE_NOX);

    /* Initialise DPS368 sensor */
    result = xensiv_dps3xx_mtb_init_i2c(&dps368_sensor, &I2C_scb3, XENSIV_DPS3XX_I2C_ADDR_DEFAULT);
    if(result != CY_RSLT_SUCCESS)
    {
    	return 1;
    }
    result = xensiv_dps3xx_set_config(&dps368_sensor, &dps368_config);
    if(result != CY_RSLT_SUCCESS)
    {
    	return 1;
    }

    /*BME690 Initialisations*/
    rslt = bme69x_interface_init(&bme, BME69X_I2C_INTF);
    bme69x_check_rslt("bme69x_interface_init", rslt);

    rslt = bme69x_init(&bme);
    bme69x_check_rslt("bme69x_init", rslt);

    rslt = bme69x_get_conf(&conf, &bme);
    bme69x_check_rslt("bme69x_get_conf", rslt);

    conf.filter = BME69X_FILTER_OFF;
    conf.odr = BME69X_ODR_NONE;
    conf.os_hum = BME69X_OS_1X;
    conf.os_pres = BME69X_OS_16X;
    conf.os_temp = BME69X_OS_2X;
    rslt = bme69x_set_conf(&conf, &bme);
    bme69x_check_rslt("bme69x_set_conf", rslt);

    heatr_conf.enable = BME69X_ENABLE;
    heatr_conf.heatr_temp_prof = temp_prof;
    heatr_conf.heatr_dur_prof = mul_prof;

    /* Shared heating duration in milliseconds */
    heatr_conf.shared_heatr_dur = (uint16_t)(140 - (bme69x_get_meas_dur(BME69X_PARALLEL_MODE, &conf, &bme) / 1000));

    heatr_conf.profile_len = 10;
    rslt = bme69x_set_heatr_conf(BME69X_PARALLEL_MODE, &heatr_conf, &bme);
    bme69x_check_rslt("bme69x_set_heatr_conf", rslt);

    /* Check if rslt == BME69X_OK, report or handle if otherwise */
    rslt = bme69x_set_op_mode(BME69X_PARALLEL_MODE, &bme);
    bme69x_check_rslt("bme69x_set_op_mode", rslt);

    /* Calculate delay period in milliseconds */
    bme_del_period = bme69x_get_meas_dur(BME69X_PARALLEL_MODE, &conf, &bme) + (heatr_conf.shared_heatr_dur * 1000);
    bme_del_period = bme_del_period/1000;

    /*BMI323*/
    rslt = bmi3_interface_init(&bmi, BMI3_I2C_INTF);
    if(rslt != BMI3_OK)
    {
    	bmi3_error_codes_print_result("bmi3_interface_init", rslt);
    	return 1;
    }

    /* Initialize BMI323. */
    rslt = bmi323_init(&bmi);
    if(rslt != BMI3_OK)
    {
    	bmi3_error_codes_print_result("bmi323_init", rslt);
    	return 1;
    }

    /* Accel and gyro configuration settings. */
    rslt = set_accel_gyro_config(&bmi);
    if(rslt != BMI3_OK)
    {
    	bmi3_error_codes_print_result("set_accel_gyro_config", rslt);
    	return 1;
    }

    /*BMM350*/
    rslt = bmm350_interface_init(&bmm);
    bmm350_error_codes_print_result("bmm350_interface_selection", rslt);

    rslt = bmm350_init(&bmm);
    bmm350_error_codes_print_result("bmm350_init", rslt);

    rslt = bmm350_get_pmu_cmd_status_0(&pmu_cmd_stat_0, &bmm);
    bmm350_error_codes_print_result("bmm350_get_pmu_cmd_status_0", rslt);

    rslt = bmm350_get_regs(BMM350_REG_ERR_REG, &bmm_err_reg_data, 1, &bmm);
    bmm350_error_codes_print_result("bmm350_get_error_reg_data", rslt);

    rslt = bmm350_configure_interrupt(BMM350_PULSED, BMM350_ACTIVE_HIGH, BMM350_INTR_PUSH_PULL, BMM350_MAP_TO_PIN, &bmm);
    bmm350_error_codes_print_result("bmm350_configure_interrupt", rslt);

    rslt = bmm350_enable_interrupt(BMM350_ENABLE_INTERRUPT, &bmm);
    bmm350_error_codes_print_result("bmm350_enable_interrupt", rslt);

    rslt = bmm350_get_regs(BMM350_REG_INT_CTRL, &bmm_int_ctrl, 1, &bmm);
    bmm350_error_codes_print_result("bmm350_get_regs", rslt);

    bmm_set_int_ctrl = ((BMM350_INT_POL_ACTIVE_HIGH << 1) | (BMM350_INT_OD_PUSHPULL << 2) | (BMM350_ENABLE << 3) | BMM350_ENABLE << 7);

    rslt = bmm350_set_odr_performance(BMM350_DATA_RATE_100HZ, BMM350_AVERAGING_4, &bmm);
    bmm350_error_codes_print_result("bmm350_set_odr_performance", rslt);

    rslt = bmm350_delay_us(10000, &bmm);
    bmm350_error_codes_print_result("bmm350_delay_us", rslt);

    rslt = bmm350_enable_axes(BMM350_X_EN, BMM350_Y_EN, BMM350_Z_EN, &bmm);
    bmm350_error_codes_print_result("bmm350_enable_axes", rslt);

    if (rslt == BMM350_OK)
    {
        rslt = bmm350_set_pad_drive(BMM350_PAD_DRIVE_STRONGEST, &bmm);
        bmm350_error_codes_print_result("bmm350_set_pad_drive", rslt);

        rslt = bmm350_set_powermode(BMM350_NORMAL_MODE, &bmm);
        bmm350_error_codes_print_result("bmm350_set_powermode", rslt);
    }

    /*BMP585*/
    rslt = bmp5_interface_init(&bmp, BMP5_I2C_INTF);
    bmp5_error_codes_print_result("bmp5_interface_init", rslt);

    rslt = bmp5_soft_reset(&bmp);
    bmp5_error_codes_print_result("bmp5_soft_reset", rslt);
    CyDelay(5);

    rslt = bmp5_init(&bmp);
    bmp5_error_codes_print_result("bmp5_init", rslt);

    rslt = bmp5_set_power_mode(BMP5_POWERMODE_STANDBY, &bmp);
    bmp5_error_codes_print_result("bmp5_set_power_mode1", rslt);

    rslt = bmp5_get_osr_odr_press_config(&osr_odr_press_cfg, &bmp);
    bmp5_error_codes_print_result("bmp5_get_osr_odr_press_config", rslt);

    /* Set ODR as 50Hz */
    osr_odr_press_cfg.odr = BMP5_ODR_50_HZ;

    /* Enable pressure */
    osr_odr_press_cfg.press_en = BMP5_ENABLE;

    /* Set Over-sampling rate with respect to odr */
    osr_odr_press_cfg.osr_t = BMP5_OVERSAMPLING_64X;
    osr_odr_press_cfg.osr_p = BMP5_OVERSAMPLING_4X;

    rslt = bmp5_set_osr_odr_press_config(&osr_odr_press_cfg, &bmp);
    bmp5_error_codes_print_result("bmp5_set_osr_odr_press_config", rslt);

    set_iir_cfg.set_iir_t = BMP5_IIR_FILTER_COEFF_1;
    set_iir_cfg.set_iir_p = BMP5_IIR_FILTER_COEFF_1;
    set_iir_cfg.shdw_set_iir_t = BMP5_ENABLE;
    set_iir_cfg.shdw_set_iir_p = BMP5_ENABLE;

    rslt = bmp5_set_iir_config(&set_iir_cfg, &bmp);
    bmp5_error_codes_print_result("bmp5_set_iir_config", rslt);

    rslt = bmp5_configure_interrupt(BMP5_PULSED, BMP5_ACTIVE_HIGH, BMP5_INTR_PUSH_PULL, BMP5_INTR_ENABLE, &bmp);
    bmp5_error_codes_print_result("bmp5_configure_interrupt", rslt);

    int_source_select.drdy_en = BMP5_ENABLE;
    rslt = bmp5_int_source_select(&int_source_select, &bmp);
    bmp5_error_codes_print_result("bmp5_int_source_select", rslt);

    rslt = bmp5_set_power_mode(BMP5_POWERMODE_NORMAL, &bmp);
    bmp5_error_codes_print_result("bmp5_set_power_mode", rslt);

	return result;
}

/* Interrupt handler callback function */
void bmi_interrupt_handler(void *handler_arg, cyhal_gpio_event_t event)
{
	CY_UNUSED_PARAMETER(handler_arg);
    CY_UNUSED_PARAMETER(event);

    /*Set the interrupt global flag*/
    bmi_int_flag = true;
}

/* Interrupt handler callback function */
void bmm_interrupt_handler(void *handler_arg, cyhal_gpio_event_t event)
{
	CY_UNUSED_PARAMETER(handler_arg);
    CY_UNUSED_PARAMETER(event);

    /*Set the interrupt global flag*/
    bmm_int_flag = true;
}
/* Interrupt handler callback function */
void bmp_interrupt_handler(void *handler_arg, cyhal_gpio_event_t event)
{
	CY_UNUSED_PARAMETER(handler_arg);
    CY_UNUSED_PARAMETER(event);

    /*Set the interrupt global flag*/
    bmp_int_flag = true;
}

/*!
 * @brief This internal API is used to set configurations for accel and gyro.
 */
static int8_t set_accel_gyro_config(struct bmi3_dev *bmi3_dev)
{
    /* Status of API are returned to this variable. */
    int8_t rslt;

    struct bmi3_map_int map_int = { 0 };

    /* Select accel and gyro sensor */
    bmi_sensor_data[0].type = BMI323_ACCEL;
    bmi_sensor_data[1].type = BMI323_GYRO;

    /* Configure the type of feature. */
    config[0].type = BMI323_ACCEL;
    config[1].type = BMI323_GYRO;

    /* NOTE: The user can change the following configuration parameters according to their requirement. */
    /* Accel configuration settings. */
    /* Output Data Rate. By default ODR is set as 100Hz for accelerometer. */
    config[0].cfg.acc.odr = BMI3_ACC_ODR_100HZ;

    /* The Accel bandwidth coefficient defines the 3 dB cutoff frequency in relation to the ODR. */
    config[0].cfg.acc.bwp = BMI3_ACC_BW_ODR_QUARTER;

    /* Set number of average samples for accel. */
    config[0].cfg.acc.avg_num = BMI3_ACC_AVG64;

    /* Gravity range of the sensor (+/- 2G, 4G, 8G, 16G). */
    config[0].cfg.acc.range = BMI3_ACC_RANGE_2G;

    /* To enable the accelerometer set the power mode to normal mode */
    config[0].cfg.acc.acc_mode = BMI3_ACC_MODE_NORMAL;

    /* Gyro configuration settings. */
    /* Output data Rate. Default ODR is 100Hz, setting to 100Hz. */
    config[1].cfg.gyr.odr = BMI3_GYR_ODR_100HZ;

    /*  The Gyroscope bandwidth coefficient defines the 3 dB cutoff frequency in relation to the ODR
     *  Value   Name      Description
     *    0   odr_half   BW = gyr_odr/2
     *    1  odr_quarter BW = gyr_odr/4
     */
    config[1].cfg.gyr.bwp = BMI3_GYR_BW_ODR_HALF;

    /* Value    Name    Description
     *  000     avg_1   No averaging; pass sample without filtering
     *  001     avg_2   Averaging of 2 samples
     *  010     avg_4   Averaging of 4 samples
     *  011     avg_8   Averaging of 8 samples
     *  100     avg_16  Averaging of 16 samples
     *  101     avg_32  Averaging of 32 samples
     *  110     avg_64  Averaging of 64 samples
     */
    config[1].cfg.gyr.avg_num = BMI3_GYR_AVG4;

    /* Gyroscope Angular Rate Measurement Range.By default the range is 2000dps. */
    config[1].cfg.gyr.range = BMI3_GYR_RANGE_125DPS;

    /* To enable the gyroscope set the power mode to normal mode */
    config[1].cfg.gyr.gyr_mode = BMI3_GYR_MODE_NORMAL;

    /* Set new configurations */
    rslt = bmi323_set_sensor_config(config, 2, bmi3_dev);
    bmi3_error_codes_print_result("bmi323_set_sensor_config", rslt);

    /* Map the FIFO full interrupt to INT1 */
    /* Note: User can map the interrupt to INT1 or INT2 */
    map_int.acc_drdy_int = BMI3_INT1;
    map_int.gyr_drdy_int = BMI3_INT1;

    /* Map the interrupt configuration */
    rslt = bmi323_map_interrupt(map_int, bmi3_dev);
    bmi3_error_codes_print_result("bmi323_map_interrupt", rslt);

    /*Configure the interrupt pin outputs*/
    rslt = bmi3_set_int_pin_config(&int_config, bmi3_dev);
    bmi3_error_codes_print_result("bmi3_set_int_pin_config", rslt);

    return rslt;
}


