/* Copyright (c) 2017-2018, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 */

#include <linux/module.h>
#include <cam_sensor_cmn_header.h>
#include "cam_sensor_core.h"
#include "cam_sensor_util.h"
#include "cam_soc_util.h"
#include "cam_trace.h"

#ifdef VENDOR_EDIT
/*Jinshui.Liu@Camera.Driver, 2018/06/23, add for [tof watchdog]*/
int tof_watchdog_goio = -1;
struct hrtimer tof_watchdog_timer;

/*Jindian.Guan@Camera.Driver, 2019/01/04, add for [malloc imx586 qsc memory early]*/
#define IMX586QSC_SIZE 2304
struct i2c_settings_list *i2c_settings_list_vendor = NULL;
struct cam_sensor_i2c_reg_array *reg_setting_vendor = NULL;
uint32_t vendor_size = 0;
#endif


/* add by likelong@camera 2017.12.12 for product information */
#ifdef VENDOR_EDIT
//#include <linux/project_info.h>
static struct cam_sensor_i2c_reg_array lotid_on_setting[2] = {
	{
		.reg_addr = 0x0A02,
		.reg_data = 0x27,
		.delay = 0x01,
		.data_mask = 0x00
	},
	{
		.reg_addr = 0x0A00,
		.reg_data = 0x01,
		.delay = 0x01,
		.data_mask = 0x00
	},
};

static struct cam_sensor_i2c_reg_array lotid_off_setting = {
	.reg_addr = 0x0A00,
	.reg_data = 0x00,
	.delay = 0x01,
	.data_mask = 0x00
};

static struct cam_sensor_i2c_reg_setting lotid_on = {
	.reg_setting = lotid_on_setting,
	.size = 2,
	.addr_type = CAMERA_SENSOR_I2C_TYPE_WORD,
	.data_type = CAMERA_SENSOR_I2C_TYPE_BYTE,
	.delay = 5,
};

static struct cam_sensor_i2c_reg_setting lotid_off = {
	.reg_setting = &lotid_off_setting,
	.size = 1,
	.addr_type = CAMERA_SENSOR_I2C_TYPE_WORD,
	.data_type = CAMERA_SENSOR_I2C_TYPE_BYTE,
	.delay = 5,
};

/* wanghaoran@camera.driver. 2019/02/16, add for read sensor gc5035 of camera dpc and reg update */
struct cam_sensor_dpc_reg_setting_array {
	struct cam_sensor_i2c_reg_array reg_setting[25];
	unsigned short size;
	enum camera_sensor_i2c_type addr_type;
	enum camera_sensor_i2c_type data_type;
	unsigned short delay;
};

struct cam_sensor_dpc_reg_setting_array gc5035OTPWrite_setting[7] = {
#include "CAM_GC5035_SPC_SENSOR_SETTINGS.h"
};

uint32_t totalDpcNum = 0;
uint32_t totalDpcFlag = 0;
uint32_t gc5035_chipversion_buffer[26]={0};

#define LOTID_START_ADDR 0x0A20
#define LOTID_LENGTH 8
#define EEPROM_MODE_ADDR 0x00

static char fuse_id[64] = {'\0'};
/*add by hongbo.dai@20180831, for support multi camera resource*/
static uint16_t  rear_main_vendor = 0;
static uint16_t  front_main_vendor = 0;

static int sensor_get_fuseid(struct cam_sensor_ctrl_t *s_ctrl)
{
	int rc = 0;
	uint16_t lotid_addr = LOTID_START_ADDR;
	struct cam_camera_slave_info *slave_info;

	int i = 0;
	uint32_t check_reg_val = 0;
	int retry_cnt = 5;
	char str_tmp[6] = {'\0'};

	slave_info = &(s_ctrl->sensordata->slave_info);
	if (!slave_info) {
		CAM_ERR(CAM_SENSOR, "slave_info is NULL: %pK",
			slave_info);
		return -EINVAL;
	}

	rc = camera_io_dev_read(
		&(s_ctrl->io_master_info),
		0x0A01,
		&check_reg_val, CAMERA_SENSOR_I2C_TYPE_WORD,
		CAMERA_SENSOR_I2C_TYPE_BYTE);

	//enable read lot id
	rc = camera_io_dev_write(
		&(s_ctrl->io_master_info),
		&lotid_on);

	//verify lot id availability
	for (i = 0; i < retry_cnt; i++) {
		rc = camera_io_dev_read(
			&(s_ctrl->io_master_info),
			0x0A01,
			&check_reg_val, CAMERA_SENSOR_I2C_TYPE_WORD,
			CAMERA_SENSOR_I2C_TYPE_BYTE);
		if (check_reg_val & (0x1))
			break;
	}

	if (i == retry_cnt) {
		CAM_ERR(CAM_SENSOR, "lot id not available");
		return -EINVAL;
	}

	//read lot id
	for (i = 0; i < LOTID_LENGTH; i++) {
		rc = camera_io_dev_read(
			&(s_ctrl->io_master_info),
			lotid_addr+i,
			&check_reg_val, CAMERA_SENSOR_I2C_TYPE_WORD,
			CAMERA_SENSOR_I2C_TYPE_BYTE);
		snprintf(str_tmp, sizeof(str_tmp), "%02x",
			(check_reg_val&0x00FF));
		strlcat(fuse_id, str_tmp, sizeof(fuse_id));
	}

	//disable read lot id
	rc = camera_io_dev_write(
		&(s_ctrl->io_master_info),
		&lotid_off);

	return 0;
}
/*Jindian.Guan@Camera.Drv, 20181207, add for imx471 DFCT info*/
//imx471 DFCT info
#define FD_DFCT_NUM_ADDR 0x7678
#define SG_DFCT_NUM_ADDR 0x767A
#define FD_DFCT_ADDR 0x8B00
#define SG_DFCT_ADDR 0x8B10

#define V_ADDR_SHIFT 12
#define H_DATA_MASK 0xFFF80000
#define V_DATA_MASK 0x0007FF80

struct sony_dfct_tbl_t imx471_dfct_tbl;

static int sensor_imx471_get_dpc_data(struct cam_sensor_ctrl_t *s_ctrl)
{
    int i = 0, j = 0;
    int rc = 0;
    int check_reg_val, dfct_data_h, dfct_data_l;
    int dfct_data = 0;
    int fd_dfct_num = 0, sg_dfct_num = 0;
    int retry_cnt = 5;
    int data_h = 0, data_v = 0;
    int fd_dfct_addr = FD_DFCT_ADDR;
    int sg_dfct_addr = SG_DFCT_ADDR;

    CAM_INFO(CAM_SENSOR, "sensor_imx471_get_dpc_data enter");
    if (s_ctrl == NULL) {
        CAM_ERR(CAM_SENSOR, "Invalid Args");
        return -EINVAL;
    }

    memset(&imx471_dfct_tbl, 0, sizeof(struct sony_dfct_tbl_t));

    for (i = 0; i < retry_cnt; i++) {
        check_reg_val = 0;
        rc = camera_io_dev_read(&(s_ctrl->io_master_info),
            FD_DFCT_NUM_ADDR, &check_reg_val,
            CAMERA_SENSOR_I2C_TYPE_WORD,
            CAMERA_SENSOR_I2C_TYPE_BYTE);

        if (0 == rc) {
            fd_dfct_num = check_reg_val & 0x07;
            if (fd_dfct_num > FD_DFCT_MAX_NUM)
                fd_dfct_num = FD_DFCT_MAX_NUM;
            break;
        }
    }

    for (i = 0; i < retry_cnt; i++) {
        check_reg_val = 0;
        rc = camera_io_dev_read(&(s_ctrl->io_master_info),
            SG_DFCT_NUM_ADDR, &check_reg_val,
            CAMERA_SENSOR_I2C_TYPE_WORD,
            CAMERA_SENSOR_I2C_TYPE_WORD);

        if (0 == rc) {
            sg_dfct_num = check_reg_val & 0x01FF;
            if (sg_dfct_num > SG_DFCT_MAX_NUM)
                sg_dfct_num = SG_DFCT_MAX_NUM;
            break;
        }
    }

    CAM_INFO(CAM_SENSOR, " fd_dfct_num = %d, sg_dfct_num = %d", fd_dfct_num, sg_dfct_num);
    imx471_dfct_tbl.fd_dfct_num = fd_dfct_num;
    imx471_dfct_tbl.sg_dfct_num = sg_dfct_num;

    if (fd_dfct_num > 0) {
        for (j = 0; j < fd_dfct_num; j++) {
            dfct_data = 0;
            for (i = 0; i < retry_cnt; i++) {
                dfct_data_h = 0;
                rc = camera_io_dev_read(&(s_ctrl->io_master_info),
                        fd_dfct_addr, &dfct_data_h,
                        CAMERA_SENSOR_I2C_TYPE_WORD,
                        CAMERA_SENSOR_I2C_TYPE_WORD);
                if (0 == rc) {
                    break;
                }
            }
            for (i = 0; i < retry_cnt; i++) {
                dfct_data_l = 0;
                rc = camera_io_dev_read(&(s_ctrl->io_master_info),
                        fd_dfct_addr+2, &dfct_data_l,
                        CAMERA_SENSOR_I2C_TYPE_WORD,
                        CAMERA_SENSOR_I2C_TYPE_WORD);
                if (0 == rc) {
                    break;
                }
            }
            CAM_DBG(CAM_SENSOR, " dfct_data_h = 0x%x, dfct_data_l = 0x%x", dfct_data_h, dfct_data_l);
            dfct_data = (dfct_data_h << 16) | dfct_data_l;
            data_h = 0;
            data_v = 0;
            data_h = (dfct_data & (H_DATA_MASK >> j%8)) >> (19 - j%8); //19 = 32 -13;
            data_v = (dfct_data & (V_DATA_MASK >> j%8)) >> (7 - j%8);  // 7 = 32 -13 -12;
            CAM_DBG(CAM_SENSOR, "j = %d, H = %d, V = %d", j, data_h, data_v);
            imx471_dfct_tbl.fd_dfct_addr[j] = ((data_h & 0x1FFF) << V_ADDR_SHIFT) | (data_v & 0x0FFF);
            CAM_DBG(CAM_SENSOR, "fd_dfct_data[%d] = 0x%08x", j, imx471_dfct_tbl.fd_dfct_addr[j]);
            fd_dfct_addr = fd_dfct_addr + 3 + ((j+1)%8 == 0);
        }
    }
    if (sg_dfct_num > 0) {
        for (j = 0; j < sg_dfct_num; j++) {
            dfct_data = 0;
            for (i = 0; i < retry_cnt; i++) {
                dfct_data_h = 0;
                rc = camera_io_dev_read(&(s_ctrl->io_master_info),
                        sg_dfct_addr, &dfct_data_h,
                        CAMERA_SENSOR_I2C_TYPE_WORD,
                        CAMERA_SENSOR_I2C_TYPE_WORD);
                if (0 == rc) {
                    break;
                }
            }
            for (i = 0; i < retry_cnt; i++) {
                dfct_data_l = 0;
                rc = camera_io_dev_read(&(s_ctrl->io_master_info),
                        sg_dfct_addr+2, &dfct_data_l,
                        CAMERA_SENSOR_I2C_TYPE_WORD,
                        CAMERA_SENSOR_I2C_TYPE_WORD);
                if (0 == rc) {
                    break;
                }
            }
            CAM_DBG(CAM_SENSOR, " dfct_data_h = 0x%x, dfct_data_l = 0x%x", dfct_data_h, dfct_data_l);
            dfct_data = (dfct_data_h << 16) | dfct_data_l;
            data_h = 0;
            data_v = 0;
            data_h = (dfct_data & (H_DATA_MASK >> j%8)) >> (19 - j%8); //19 = 32 -13;
            data_v = (dfct_data & (V_DATA_MASK >> j%8)) >> (7 - j%8);  // 7 = 32 -13 -12;
            CAM_DBG(CAM_SENSOR, "j = %d, H = %d, V = %d", j, data_h, data_v);
            imx471_dfct_tbl.sg_dfct_addr[j] = ((data_h & 0x1FFF) << V_ADDR_SHIFT) | (data_v & 0x0FFF);
            CAM_DBG(CAM_SENSOR, "sg_dfct_data[%d] = 0x%08x", j, imx471_dfct_tbl.sg_dfct_addr[j]);
            sg_dfct_addr = sg_dfct_addr + 3 + ((j+1)%8 == 0);
        }
    }

    CAM_INFO(CAM_SENSOR, "exit");
    return rc;
}

/* wanghaoran@camera.driver. 2019/02/16, add for read sensor gc5035 of camera dpc and reg update */
static int sensor_gc5035_get_dpc_data(struct cam_sensor_ctrl_t * s_ctrl)
{
	int rc = 0;
	uint32_t gc5035_dpcinfo[3] = {0};
	uint32_t i;
	uint32_t dpcinfoOffet = 0xcd;
	uint32_t chipPage8Offet = 0xd0;
	uint32_t chipPage9Offet = 0xc0;

	struct cam_sensor_i2c_reg_setting sensor_setting;
	/*write otp read init settings*/
	sensor_setting.reg_setting = gc5035OTPWrite_setting[0].reg_setting;
	sensor_setting.addr_type = gc5035OTPWrite_setting[0].addr_type;
	sensor_setting.data_type = gc5035OTPWrite_setting[0].data_type;
	sensor_setting.size = gc5035OTPWrite_setting[0].size;
	sensor_setting.delay = gc5035OTPWrite_setting[0].delay;

	rc = camera_io_dev_write(&(s_ctrl->io_master_info), &sensor_setting);

	if (rc < 0) {
	    CAM_ERR(CAM_SENSOR, "gc5035SpcWrite_setting Failed to write sensor setting");
	    return rc;
	}
	/*write dpc page0 setting*/
	sensor_setting.reg_setting = gc5035OTPWrite_setting[1].reg_setting;
	sensor_setting.addr_type = gc5035OTPWrite_setting[1].addr_type;
	sensor_setting.data_type = gc5035OTPWrite_setting[1].data_type;
	sensor_setting.size = gc5035OTPWrite_setting[1].size;
	sensor_setting.delay = gc5035OTPWrite_setting[1].delay;

	rc = camera_io_dev_write(&(s_ctrl->io_master_info), &sensor_setting);

	if (rc < 0) {
	    CAM_ERR(CAM_SENSOR, "gc5035SpcWrite_setting Failed to write sensor setting");
	    return rc;
	}
	/*read dpc data*/
	for (i = 0; i < 3; i++) {
	    rc = camera_io_dev_read(
	         &(s_ctrl->io_master_info),
	         dpcinfoOffet + i,
	         &gc5035_dpcinfo[i], CAMERA_SENSOR_I2C_TYPE_BYTE,
	         CAMERA_SENSOR_I2C_TYPE_BYTE);
	    if (rc < 0) {
	        CAM_ERR(CAM_SENSOR, "gc5035SpcWrite_setting Failed to read dpc info sensor setting");
	        break;
	    }
	}

	if (rc < 0)
	   return rc;
	/*close read data*/
	sensor_setting.reg_setting = gc5035OTPWrite_setting[2].reg_setting;
	sensor_setting.addr_type = gc5035OTPWrite_setting[2].addr_type;
	sensor_setting.data_type = gc5035OTPWrite_setting[2].data_type;
	sensor_setting.size = gc5035OTPWrite_setting[2].size;
	sensor_setting.delay = gc5035OTPWrite_setting[2].delay;

	rc = camera_io_dev_write(&(s_ctrl->io_master_info), &sensor_setting);
	if (rc < 0) {
	    CAM_ERR(CAM_SENSOR, "gc5035SpcWrite_setting Failed to write sensor setting");
	    return rc;
	}
	/*
	for (i = 0; i < 19; i++) {
	    CAM_ERR(CAM_SENSOR, "gc5035SpcWrite_setting gc5035_dpcinfo[0] = %x",gc5035_dpcinfo[i]);
	}*/
	if (gc5035_dpcinfo[0] == 1) {
	    totalDpcFlag = 1;
	    totalDpcNum = gc5035_dpcinfo[1] + gc5035_dpcinfo[2] ;
	    CAM_ERR(CAM_SENSOR, "gc5035SpcWrite_setting gc5035_dpcinfo[1] = %d",gc5035_dpcinfo[1]);
	    CAM_ERR(CAM_SENSOR, "gc5035SpcWrite_setting gc5035_dpcinfo[2] = %d",gc5035_dpcinfo[2]);
	    CAM_ERR(CAM_SENSOR, "gc5035SpcWrite_setting totalDpcNum = %d",totalDpcNum);

	}
	//write for update reg for page 8
	sensor_setting.reg_setting = gc5035OTPWrite_setting[5].reg_setting;
	sensor_setting.addr_type = gc5035OTPWrite_setting[5].addr_type;
	sensor_setting.data_type = gc5035OTPWrite_setting[5].data_type;
	sensor_setting.size = gc5035OTPWrite_setting[5].size;
	sensor_setting.delay = gc5035OTPWrite_setting[5].delay;

	rc = camera_io_dev_write(&(s_ctrl->io_master_info), &sensor_setting);
	if (rc < 0) {
	    CAM_ERR(CAM_SENSOR, "gc5035SpcWrite_setting Failed to write sensor setting");
	    return rc;
	}
	for (i = 0; i < 0x10; i++) {
	    rc = camera_io_dev_read(
	         &(s_ctrl->io_master_info),
	         chipPage8Offet + i,
	         &gc5035_chipversion_buffer[i], CAMERA_SENSOR_I2C_TYPE_BYTE,
	         CAMERA_SENSOR_I2C_TYPE_BYTE);
	    if (rc < 0) {
	        CAM_ERR(CAM_SENSOR, "gc5035SpcWrite_setting Failed to read dpc info sensor setting");
	        break;
	    }
	}
	/*close read data*/
	sensor_setting.reg_setting = gc5035OTPWrite_setting[2].reg_setting;
	sensor_setting.addr_type = gc5035OTPWrite_setting[2].addr_type;
	sensor_setting.data_type = gc5035OTPWrite_setting[2].data_type;
	sensor_setting.size = gc5035OTPWrite_setting[2].size;
	sensor_setting.delay = gc5035OTPWrite_setting[2].delay;

	rc = camera_io_dev_write(&(s_ctrl->io_master_info), &sensor_setting);
	if (rc < 0) {
	    CAM_ERR(CAM_SENSOR, "gc5035SpcWrite_setting Failed to write sensor setting");
	    return rc;
	}
	//write for update reg for page 9
	sensor_setting.reg_setting = gc5035OTPWrite_setting[6].reg_setting;
	sensor_setting.addr_type = gc5035OTPWrite_setting[6].addr_type;
	sensor_setting.data_type = gc5035OTPWrite_setting[6].data_type;
	sensor_setting.size = gc5035OTPWrite_setting[6].size;
	sensor_setting.delay = gc5035OTPWrite_setting[6].delay;

	rc = camera_io_dev_write(&(s_ctrl->io_master_info), &sensor_setting);
	if (rc < 0) {
	    CAM_ERR(CAM_SENSOR, "gc5035SpcWrite_setting Failed to write sensor setting");
	    return rc;
	}
	for (i = 0x00; i < 0x0a; i++) {
	    rc = camera_io_dev_read(
	          &(s_ctrl->io_master_info),
	          chipPage9Offet + i,
	          &gc5035_chipversion_buffer[0x10+i], CAMERA_SENSOR_I2C_TYPE_BYTE,
	          CAMERA_SENSOR_I2C_TYPE_BYTE);
	    if (rc < 0) {
	        CAM_ERR(CAM_SENSOR, "gc5035SpcWrite_setting Failed to read dpc info sensor setting");
	        break;
	    }
	}
	/*close read data*/
	sensor_setting.reg_setting = gc5035OTPWrite_setting[2].reg_setting;
	sensor_setting.addr_type = gc5035OTPWrite_setting[2].addr_type;
	sensor_setting.data_type = gc5035OTPWrite_setting[2].data_type;
	sensor_setting.size = gc5035OTPWrite_setting[2].size;
	sensor_setting.delay = gc5035OTPWrite_setting[2].delay;

	rc = camera_io_dev_write(&(s_ctrl->io_master_info), &sensor_setting);
	if (rc < 0) {
	    CAM_ERR(CAM_SENSOR, "gc5035SpcWrite_setting Failed to write sensor setting");
	    return rc;
	}
	return rc;

}

static int sensor_gc5035_write_dpc_data(struct cam_sensor_ctrl_t * s_ctrl)
{
    int rc = 0;
    struct cam_sensor_i2c_reg_array gc5035SpcTotalNum_setting[2];
    struct cam_sensor_i2c_reg_setting sensor_setting;
    //for test
    /*struct cam_sensor_i2c_reg_array gc5035SRAM_setting;
    uint32_t temp_val[4];
    int j,i; */

    if (totalDpcFlag == 0)
        return 0;

	sensor_setting.reg_setting = gc5035OTPWrite_setting[3].reg_setting;
	sensor_setting.addr_type = gc5035OTPWrite_setting[3].addr_type;
	sensor_setting.data_type = gc5035OTPWrite_setting[3].data_type;
	sensor_setting.size = gc5035OTPWrite_setting[3].size;
	sensor_setting.delay = gc5035OTPWrite_setting[3].delay;

	rc = camera_io_dev_write(&(s_ctrl->io_master_info), &sensor_setting);

	if (rc < 0) {
	    CAM_ERR(CAM_SENSOR, "gc5035SpcWrite_setting Failed to write sensor setting");
	    return rc;
	}
	gc5035SpcTotalNum_setting[0].reg_addr = 0x01;
	gc5035SpcTotalNum_setting[0].reg_data = (totalDpcNum >> 8) & 0x07;
	gc5035SpcTotalNum_setting[0].delay = gc5035SpcTotalNum_setting[0].data_mask = 0;

	gc5035SpcTotalNum_setting[1].reg_addr = 0x02;
	gc5035SpcTotalNum_setting[1].reg_data = totalDpcNum & 0xff;
	gc5035SpcTotalNum_setting[1].delay = gc5035SpcTotalNum_setting[1].data_mask = 0;

	sensor_setting.reg_setting = gc5035SpcTotalNum_setting;
	sensor_setting.addr_type = CAMERA_SENSOR_I2C_TYPE_BYTE;
	sensor_setting.data_type = CAMERA_SENSOR_I2C_TYPE_BYTE;
	sensor_setting.size = 2;
	sensor_setting.delay = 0;

	rc = camera_io_dev_write(&(s_ctrl->io_master_info), &sensor_setting);

	if (rc < 0) {
	    CAM_ERR(CAM_SENSOR, "gc5035SpcWrite_setting Failed to write sensor setting");
	    return rc;
	}

	sensor_setting.reg_setting = gc5035OTPWrite_setting[4].reg_setting;
	sensor_setting.addr_type = gc5035OTPWrite_setting[4].addr_type;
	sensor_setting.data_type = gc5035OTPWrite_setting[4].data_type;
	sensor_setting.size = gc5035OTPWrite_setting[4].size;
	sensor_setting.delay = gc5035OTPWrite_setting[4].delay;

	rc = camera_io_dev_write(&(s_ctrl->io_master_info), &sensor_setting);

	if (rc < 0) {
	    CAM_ERR(CAM_SENSOR, "gc5035SpcWrite_setting Failed to write sensor setting");
	    return rc;
	}
//for test
   /*gc5035SpcTotalNum_setting[0].reg_addr = 0xfe;
	gc5035SpcTotalNum_setting[0].reg_data = 0x02;
	gc5035SpcTotalNum_setting[0].delay = gc5035SpcTotalNum_setting[0].data_mask = 0;

	gc5035SpcTotalNum_setting[1].reg_addr = 0xbe;
	gc5035SpcTotalNum_setting[1].reg_data = 0x00;
	gc5035SpcTotalNum_setting[1].delay = gc5035SpcTotalNum_setting[1].data_mask = 0;

	sensor_setting.reg_setting = gc5035SpcTotalNum_setting;
	sensor_setting.addr_type = CAMERA_SENSOR_I2C_TYPE_BYTE;
	sensor_setting.data_type = CAMERA_SENSOR_I2C_TYPE_BYTE;
	sensor_setting.size = 2;
	sensor_setting.delay = 0;
	rc = camera_io_dev_write(&(s_ctrl->io_master_info), &sensor_setting);

	if (rc < 0) {
	    CAM_ERR(CAM_SENSOR, "gc5035SpcWrite_setting Failed to write sensor setting");
	    return rc;
	}
	for (i=0; i<totalDpcNum*4; i++) {
	gc5035SRAM_setting.reg_addr = 0xaa;
	gc5035SRAM_setting.reg_data = i;
	gc5035SRAM_setting.delay = gc5035SRAM_setting.data_mask = 0;
	sensor_setting.reg_setting = &gc5035SRAM_setting;
	sensor_setting.addr_type = CAMERA_SENSOR_I2C_TYPE_BYTE;
	sensor_setting.data_type = CAMERA_SENSOR_I2C_TYPE_BYTE;
	sensor_setting.size = 1;
	sensor_setting.delay = 0;
	rc = camera_io_dev_write(&(s_ctrl->io_master_info), &sensor_setting);
	for (j=0; j<4; j++) {
	    rc = camera_io_dev_read(
	         &(s_ctrl->io_master_info),
	         0xac,
	         &temp_val[j], CAMERA_SENSOR_I2C_TYPE_BYTE,
	         CAMERA_SENSOR_I2C_TYPE_BYTE);
	    if (rc < 0) {
	       CAM_ERR(CAM_SENSOR, "gc5035SpcWrite_setting Failed to read dpc info sensor setting");
	       break;
	    }
	}
	 CAM_ERR(CAM_SENSOR,"GC5035_OTP_GC val0 = 0x%x , val1 = 0x%x , val2 = 0x%x,val3 = 0x%x \n",
	 temp_val[0],temp_val[1],temp_val[2],temp_val[3]);
	 CAM_ERR(CAM_SENSOR,"GC5035_OTP_GC x = %d , y = %d ,type = %d \n",
	        ((temp_val[1]&0x0f)<<8) + temp_val[0],((temp_val[2]&0x7f)<<4) + ((temp_val[1]&0xf0)>>4),(((temp_val[3]&0x01)<<1)+((temp_val[2]&0x80)>>7)));
	}

	gc5035SpcTotalNum_setting[0].reg_addr = 0xbe;
	gc5035SpcTotalNum_setting[0].reg_data = 0x01;
	gc5035SpcTotalNum_setting[0].delay = gc5035SpcTotalNum_setting[0].data_mask = 0;

	gc5035SpcTotalNum_setting[1].reg_addr = 0xfe;
	gc5035SpcTotalNum_setting[1].reg_data = 0x00;
	gc5035SpcTotalNum_setting[1].delay = gc5035SpcTotalNum_setting[1].data_mask = 0;

	sensor_setting.reg_setting = gc5035SpcTotalNum_setting;
	sensor_setting.addr_type = CAMERA_SENSOR_I2C_TYPE_BYTE;
	sensor_setting.data_type = CAMERA_SENSOR_I2C_TYPE_BYTE;
	sensor_setting.size = 2;
	sensor_setting.delay = 0;
	rc = camera_io_dev_write(&(s_ctrl->io_master_info), &sensor_setting);

	if (rc < 0) {
	   CAM_ERR(CAM_SENSOR, "gc5035SpcWrite_setting Failed to write sensor setting");
	   return rc;
	}*/
	return rc;
}

static int sensor_gc5035_update_reg(struct cam_sensor_ctrl_t * s_ctrl)
{
	int rc = -1;
	uint8_t flag_chipv = 0;
	int i = 0;
	uint8_t VALID_FLAG = 0x01;
	uint8_t CHIPV_FLAG_OFFSET = 0x0;
	uint8_t CHIPV_OFFSET = 0x01;
	uint8_t reg_setting_size = 0;
	struct cam_sensor_i2c_reg_array gc5035_update_reg_setting[20];
	struct cam_sensor_i2c_reg_setting sensor_setting;
	CAM_DBG(CAM_SENSOR,"Enter");

	flag_chipv = gc5035_chipversion_buffer[CHIPV_FLAG_OFFSET];
	CAM_DBG(CAM_SENSOR,"gc5035 otp chipv flag_chipv: 0x%x", flag_chipv);
	if (VALID_FLAG != (flag_chipv & 0x03)) {
	    CAM_ERR(CAM_SENSOR,"gc5035 otp chip regs data is Empty/Invalid!");
	    return rc;
	}

	for (i = 0; i < 5; i++) {
	    if (VALID_FLAG == ((gc5035_chipversion_buffer[CHIPV_OFFSET + 5 * i] >> 3) & 0x01)) {
	        gc5035_update_reg_setting[reg_setting_size].reg_addr = 0xfe;
	        gc5035_update_reg_setting[reg_setting_size].reg_data = gc5035_chipversion_buffer[CHIPV_OFFSET + 5 * i] & 0x07;
	        gc5035_update_reg_setting[reg_setting_size].delay = gc5035_update_reg_setting[reg_setting_size].data_mask = 0;
	        reg_setting_size++;
	        gc5035_update_reg_setting[reg_setting_size].reg_addr = gc5035_chipversion_buffer[CHIPV_OFFSET + 5 * i + 1];
	        gc5035_update_reg_setting[reg_setting_size].reg_data = gc5035_chipversion_buffer[CHIPV_OFFSET + 5 * i + 2];
	        gc5035_update_reg_setting[reg_setting_size].delay = gc5035_update_reg_setting[reg_setting_size].data_mask = 0;
	        reg_setting_size++;

	        CAM_DBG(CAM_SENSOR,"gc5035 otp chipv : 0xfe=0x%x, addr[%d]=0x%x, value[%d]=0x%x", gc5035_chipversion_buffer[CHIPV_OFFSET +  5 * i] & 0x07,i*2,
	                gc5035_chipversion_buffer[CHIPV_OFFSET + 5 * i + 1],i*2,gc5035_chipversion_buffer[CHIPV_OFFSET + 5 * i + 2]);
	    }
	    if (VALID_FLAG == ((gc5035_chipversion_buffer[CHIPV_OFFSET + 5 * i] >> 7) & 0x01)) {
	        gc5035_update_reg_setting[reg_setting_size].reg_addr = 0xfe;
	        gc5035_update_reg_setting[reg_setting_size].reg_data = (gc5035_chipversion_buffer[CHIPV_OFFSET + 5 * i] & 0x70) >> 4;
	        gc5035_update_reg_setting[reg_setting_size].delay = gc5035_update_reg_setting[reg_setting_size].data_mask = 0;
	        reg_setting_size++;
	        gc5035_update_reg_setting[reg_setting_size].reg_addr = gc5035_chipversion_buffer[CHIPV_OFFSET + 5 * i + 3];
	        gc5035_update_reg_setting[reg_setting_size].reg_data = gc5035_chipversion_buffer[CHIPV_OFFSET + 5 * i + 4];
	        gc5035_update_reg_setting[reg_setting_size].delay = gc5035_update_reg_setting[reg_setting_size].data_mask = 0;
	        reg_setting_size++;

	        CAM_DBG(CAM_SENSOR,"gc5035 otp chipv : 0xfe=0x%x, addr[%d]=0x%x, value[%d]=0x%x", (gc5035_chipversion_buffer[CHIPV_OFFSET + 5 * i] & 0x70) >> 4,i*2+1,
	                gc5035_chipversion_buffer[CHIPV_OFFSET + 5 * i + 3],i*2+1,gc5035_chipversion_buffer[CHIPV_OFFSET + 5 * i + 4]);
	    }
	}
	sensor_setting.reg_setting = gc5035_update_reg_setting;
	sensor_setting.addr_type = CAMERA_SENSOR_I2C_TYPE_BYTE;
	sensor_setting.data_type = CAMERA_SENSOR_I2C_TYPE_BYTE;
	sensor_setting.size = reg_setting_size;
	sensor_setting.delay = 0;

	rc = camera_io_dev_write(&(s_ctrl->io_master_info), &sensor_setting);

	if (rc < 0) {
	    CAM_ERR(CAM_SENSOR, "gc5035SpcWrite_setting Failed to write sensor setting");
	    return rc;
	}
	rc = 0;
	CAM_DBG(CAM_SENSOR,"Exit");
	return rc;

}
#endif


static void cam_sensor_update_req_mgr(
	struct cam_sensor_ctrl_t *s_ctrl,
	struct cam_packet *csl_packet)
{
	struct cam_req_mgr_add_request add_req;

	add_req.link_hdl = s_ctrl->bridge_intf.link_hdl;
	add_req.req_id = csl_packet->header.request_id;
	CAM_DBG(CAM_SENSOR, " Rxed Req Id: %lld",
		csl_packet->header.request_id);
	add_req.dev_hdl = s_ctrl->bridge_intf.device_hdl;
	add_req.skip_before_applying = 0;
	if (s_ctrl->bridge_intf.crm_cb &&
		s_ctrl->bridge_intf.crm_cb->add_req)
		s_ctrl->bridge_intf.crm_cb->add_req(&add_req);

	CAM_DBG(CAM_SENSOR, " add req to req mgr: %lld",
			add_req.req_id);
}

static void cam_sensor_release_stream_rsc(
	struct cam_sensor_ctrl_t *s_ctrl)
{
	struct i2c_settings_array *i2c_set = NULL;
	int rc;

	i2c_set = &(s_ctrl->i2c_data.streamoff_settings);
	if (i2c_set->is_settings_valid == 1) {
		i2c_set->is_settings_valid = -1;
		rc = delete_request(i2c_set);
		if (rc < 0)
			CAM_ERR(CAM_SENSOR,
				"failed while deleting Streamoff settings");
	}

	i2c_set = &(s_ctrl->i2c_data.streamon_settings);
	if (i2c_set->is_settings_valid == 1) {
		i2c_set->is_settings_valid = -1;
		rc = delete_request(i2c_set);
		if (rc < 0)
			CAM_ERR(CAM_SENSOR,
				"failed while deleting Streamon settings");
	}
}

static void cam_sensor_release_resource(
	struct cam_sensor_ctrl_t *s_ctrl)
{
	struct i2c_settings_array *i2c_set = NULL;
	int i, rc;

	i2c_set = &(s_ctrl->i2c_data.init_settings);
	if (i2c_set->is_settings_valid == 1) {
		i2c_set->is_settings_valid = -1;
		rc = delete_request(i2c_set);
		if (rc < 0)
			CAM_ERR(CAM_SENSOR,
				"failed while deleting Init settings");
	}

	i2c_set = &(s_ctrl->i2c_data.config_settings);
	if (i2c_set->is_settings_valid == 1) {
		i2c_set->is_settings_valid = -1;
		rc = delete_request(i2c_set);
		if (rc < 0)
			CAM_ERR(CAM_SENSOR,
				"failed while deleting Res settings");
	}

	if (s_ctrl->i2c_data.per_frame != NULL) {
		for (i = 0; i < MAX_PER_FRAME_ARRAY; i++) {
			i2c_set = &(s_ctrl->i2c_data.per_frame[i]);
			if (i2c_set->is_settings_valid == 1) {
				i2c_set->is_settings_valid = -1;
				rc = delete_request(i2c_set);
				if (rc < 0)
					CAM_ERR(CAM_SENSOR,
						"delete request: %lld rc: %d",
						i2c_set->request_id, rc);
			}
		}
	}
}

static int32_t cam_sensor_i2c_pkt_parse(struct cam_sensor_ctrl_t *s_ctrl,
	void *arg)
{
	int32_t rc = 0;
	uint64_t generic_ptr;
	struct cam_control *ioctl_ctrl = NULL;
	struct cam_packet *csl_packet = NULL;
	struct cam_cmd_buf_desc *cmd_desc = NULL;
	struct i2c_settings_array *i2c_reg_settings = NULL;
	size_t len_of_buff = 0;
	uint32_t *offset = NULL;
	struct cam_config_dev_cmd config;
	struct i2c_data_settings *i2c_data = NULL;

	ioctl_ctrl = (struct cam_control *)arg;

	if (ioctl_ctrl->handle_type != CAM_HANDLE_USER_POINTER) {
		CAM_ERR(CAM_SENSOR, "Invalid Handle Type");
		return -EINVAL;
	}

	if (copy_from_user(&config, (void __user *) ioctl_ctrl->handle,
		sizeof(config)))
		return -EFAULT;

	rc = cam_mem_get_cpu_buf(
		config.packet_handle,
		(uint64_t *)&generic_ptr,
		&len_of_buff);
	if (rc < 0) {
		CAM_ERR(CAM_SENSOR, "Failed in getting the buffer: %d", rc);
		return rc;
	}

	csl_packet = (struct cam_packet *)(generic_ptr +
		config.offset);
	if (config.offset > len_of_buff) {
		CAM_ERR(CAM_SENSOR,
			"offset is out of bounds: off: %lld len: %zu",
			 config.offset, len_of_buff);
		return -EINVAL;
	}

	i2c_data = &(s_ctrl->i2c_data);
	CAM_DBG(CAM_SENSOR, "Header OpCode: %d", csl_packet->header.op_code);
	switch (csl_packet->header.op_code & 0xFFFFFF) {
	case CAM_SENSOR_PACKET_OPCODE_SENSOR_INITIAL_CONFIG: {
		i2c_reg_settings = &i2c_data->init_settings;
		i2c_reg_settings->request_id = 0;
		i2c_reg_settings->is_settings_valid = 1;
		break;
	}
	case CAM_SENSOR_PACKET_OPCODE_SENSOR_CONFIG: {
		i2c_reg_settings = &i2c_data->config_settings;
		i2c_reg_settings->request_id = 0;
		i2c_reg_settings->is_settings_valid = 1;
		break;
	}
	case CAM_SENSOR_PACKET_OPCODE_SENSOR_STREAMON: {
		if (s_ctrl->streamon_count > 0)
			return 0;

		s_ctrl->streamon_count = s_ctrl->streamon_count + 1;
		i2c_reg_settings = &i2c_data->streamon_settings;
		i2c_reg_settings->request_id = 0;
		i2c_reg_settings->is_settings_valid = 1;
		break;
	}
	case CAM_SENSOR_PACKET_OPCODE_SENSOR_STREAMOFF: {
		if (s_ctrl->streamoff_count > 0)
			return 0;

		s_ctrl->streamoff_count = s_ctrl->streamoff_count + 1;
		i2c_reg_settings = &i2c_data->streamoff_settings;
		i2c_reg_settings->request_id = 0;
		i2c_reg_settings->is_settings_valid = 1;
		break;
	}

	case CAM_SENSOR_PACKET_OPCODE_SENSOR_UPDATE: {
		if ((s_ctrl->sensor_state == CAM_SENSOR_INIT) ||
			(s_ctrl->sensor_state == CAM_SENSOR_ACQUIRE)) {
			CAM_WARN(CAM_SENSOR,
				"Rxed Update packets without linking");
			return 0;
		}

		i2c_reg_settings =
			&i2c_data->per_frame[csl_packet->header.request_id %
				MAX_PER_FRAME_ARRAY];
		CAM_DBG(CAM_SENSOR, "Received Packet: %lld req: %lld",
			csl_packet->header.request_id % MAX_PER_FRAME_ARRAY,
			csl_packet->header.request_id);
		if (i2c_reg_settings->is_settings_valid == 1) {
			CAM_ERR(CAM_SENSOR,
				"Already some pkt in offset req : %lld",
				csl_packet->header.request_id);
			/*
			 * Update req mgr even in case of failure.
			 * This will help not to wait indefinitely
			 * and freeze. If this log is triggered then
			 * fix it.
			 */
			cam_sensor_update_req_mgr(s_ctrl, csl_packet);
			return 0;
		}
		break;
	}
	case CAM_SENSOR_PACKET_OPCODE_SENSOR_NOP: {
		if ((s_ctrl->sensor_state == CAM_SENSOR_INIT) ||
			(s_ctrl->sensor_state == CAM_SENSOR_ACQUIRE)) {
			CAM_WARN(CAM_SENSOR,
				"Rxed NOP packets without linking");
			return 0;
		}

		cam_sensor_update_req_mgr(s_ctrl, csl_packet);
		return 0;
	}
	default:
		CAM_ERR(CAM_SENSOR, "Invalid Packet Header");
		return -EINVAL;
	}

	offset = (uint32_t *)&csl_packet->payload;
	offset += csl_packet->cmd_buf_offset / 4;
	cmd_desc = (struct cam_cmd_buf_desc *)(offset);

	#ifdef VENDOR_EDIT
	/*Jindian.Guan@Camera.Driver, 2019/01/04, modify for [malloc imx586 qsc memory early]*/
	rc = cam_sensor_i2c_command_parser_vendor(&s_ctrl->io_master_info,
			i2c_reg_settings, cmd_desc, 1, csl_packet->header.vendor_mode);
	#else
	rc = cam_sensor_i2c_command_parser(&s_ctrl->io_master_info,
			i2c_reg_settings, cmd_desc, 1);
	#endif
	if (rc < 0) {
		CAM_ERR(CAM_SENSOR, "Fail parsing I2C Pkt: %d", rc);
		return rc;
	}

	if ((csl_packet->header.op_code & 0xFFFFFF) ==
		CAM_SENSOR_PACKET_OPCODE_SENSOR_UPDATE) {
		i2c_reg_settings->request_id =
			csl_packet->header.request_id;
		cam_sensor_update_req_mgr(s_ctrl, csl_packet);
	}

	return rc;
}

static int32_t cam_sensor_i2c_modes_util(
	struct camera_io_master *io_master_info,
	struct i2c_settings_list *i2c_list)
{
	int32_t rc = 0;
	uint32_t i, size;

	if (i2c_list->op_code == CAM_SENSOR_I2C_WRITE_RANDOM) {
		rc = camera_io_dev_write(io_master_info,
			&(i2c_list->i2c_settings));
		if (rc < 0) {
			CAM_ERR(CAM_SENSOR,
				"Failed to random write I2C settings: %d",
				rc);
			return rc;
		}
	} else if (i2c_list->op_code == CAM_SENSOR_I2C_WRITE_SEQ) {
		rc = camera_io_dev_write_continuous(
			io_master_info,
			&(i2c_list->i2c_settings),
			0);
		if (rc < 0) {
			CAM_ERR(CAM_SENSOR,
				"Failed to seq write I2C settings: %d",
				rc);
			return rc;
		}
	} else if (i2c_list->op_code == CAM_SENSOR_I2C_WRITE_BURST) {
		rc = camera_io_dev_write_continuous(
			io_master_info,
			&(i2c_list->i2c_settings),
			1);
		if (rc < 0) {
			CAM_ERR(CAM_SENSOR,
				"Failed to burst write I2C settings: %d",
				rc);
			return rc;
		}
	} else if (i2c_list->op_code == CAM_SENSOR_I2C_POLL) {
		size = i2c_list->i2c_settings.size;
		for (i = 0; i < size; i++) {
			rc = camera_io_dev_poll(
			io_master_info,
			i2c_list->i2c_settings.reg_setting[i].reg_addr,
			i2c_list->i2c_settings.reg_setting[i].reg_data,
			i2c_list->i2c_settings.reg_setting[i].data_mask,
			i2c_list->i2c_settings.addr_type,
			i2c_list->i2c_settings.data_type,
			i2c_list->i2c_settings.reg_setting[i].delay);
			if (rc < 0) {
				CAM_ERR(CAM_SENSOR,
					"i2c poll apply setting Fail: %d", rc);
				return rc;
			}
		}
	}

	return rc;
}

int32_t cam_sensor_update_i2c_info(struct cam_cmd_i2c_info *i2c_info,
	struct cam_sensor_ctrl_t *s_ctrl)
{
	int32_t rc = 0;
	struct cam_sensor_cci_client   *cci_client = NULL;

	if (s_ctrl->io_master_info.master_type == CCI_MASTER) {
		cci_client = s_ctrl->io_master_info.cci_client;
		if (!cci_client) {
			CAM_ERR(CAM_SENSOR, "failed: cci_client %pK",
				cci_client);
			return -EINVAL;
		}
		cci_client->cci_i2c_master = s_ctrl->cci_i2c_master;
		cci_client->sid = i2c_info->slave_addr >> 1;
		cci_client->retries = 3;
		cci_client->id_map = 0;
		cci_client->i2c_freq_mode = i2c_info->i2c_freq_mode;
		CAM_DBG(CAM_SENSOR, " Master: %d sid: %d freq_mode: %d",
			cci_client->cci_i2c_master, i2c_info->slave_addr,
			i2c_info->i2c_freq_mode);
	}

	s_ctrl->sensordata->slave_info.sensor_slave_addr =
		i2c_info->slave_addr;
	return rc;
}

int32_t cam_sensor_update_slave_info(struct cam_cmd_probe *probe_info,
	struct cam_sensor_ctrl_t *s_ctrl)
{
	int32_t rc = 0;

	s_ctrl->sensordata->slave_info.sensor_id_reg_addr =
		probe_info->reg_addr;
	s_ctrl->sensordata->slave_info.sensor_id =
		probe_info->expected_data;
	s_ctrl->sensordata->slave_info.sensor_id_mask =
		probe_info->data_mask;

	s_ctrl->sensor_probe_addr_type =  probe_info->addr_type;
	s_ctrl->sensor_probe_data_type =  probe_info->data_type;
	#ifdef VENDOR_EDIT
	/*add by hongbo.dai@camera 20180831, for support multi camera resource*/
	s_ctrl->sensordata->slave_info.eeprom_slave_addr =
		(probe_info->reserved >> 8) & (0xFF);
	s_ctrl->sensordata->slave_info.vendor_id =
		(probe_info->reserved & 0xFF);
	s_ctrl->sensordata->slave_info.camera_id=
		probe_info->camera_id;

	CAM_DBG(CAM_SENSOR,
		"Sensor Addr: 0x%x sensor_id: 0x%x sensor_mask: 0x%x eeprom_addr:0x%0x  vendor_id:0x%0x",
		s_ctrl->sensordata->slave_info.sensor_id_reg_addr,
		s_ctrl->sensordata->slave_info.sensor_id,
		s_ctrl->sensordata->slave_info.sensor_id_mask,
		s_ctrl->sensordata->slave_info.eeprom_slave_addr,
        s_ctrl->sensordata->slave_info.vendor_id);
	#else
	CAM_DBG(CAM_SENSOR,
		"Sensor Addr: 0x%x sensor_id: 0x%x sensor_mask: 0x%x",
		s_ctrl->sensordata->slave_info.sensor_id_reg_addr,
		s_ctrl->sensordata->slave_info.sensor_id,
		s_ctrl->sensordata->slave_info.sensor_id_mask);
	#endif

	return rc;
}

int32_t cam_handle_cmd_buffers_for_probe(void *cmd_buf,
	struct cam_sensor_ctrl_t *s_ctrl,
	int32_t cmd_buf_num, int cmd_buf_length)
{
	int32_t rc = 0;

	switch (cmd_buf_num) {
	case 0: {
		struct cam_cmd_i2c_info *i2c_info = NULL;
		struct cam_cmd_probe *probe_info;

		i2c_info = (struct cam_cmd_i2c_info *)cmd_buf;
		rc = cam_sensor_update_i2c_info(i2c_info, s_ctrl);
		if (rc < 0) {
			CAM_ERR(CAM_SENSOR, "Failed in Updating the i2c Info");
			return rc;
		}
		probe_info = (struct cam_cmd_probe *)
			(cmd_buf + sizeof(struct cam_cmd_i2c_info));
		rc = cam_sensor_update_slave_info(probe_info, s_ctrl);
		if (rc < 0) {
			CAM_ERR(CAM_SENSOR, "Updating the slave Info");
			return rc;
		}
		cmd_buf = probe_info;
	}
		break;
	case 1: {
		rc = cam_sensor_update_power_settings(cmd_buf,
			cmd_buf_length, &s_ctrl->sensordata->power_info);
		if (rc < 0) {
			CAM_ERR(CAM_SENSOR,
				"Failed in updating power settings");
			return rc;
		}
	}
		break;
	default:
		CAM_ERR(CAM_SENSOR, "Invalid command buffer");
		break;
	}
	return rc;
}

int32_t cam_handle_mem_ptr(uint64_t handle, struct cam_sensor_ctrl_t *s_ctrl)
{
	int rc = 0, i;
	void *packet = NULL, *cmd_buf1 = NULL;
	uint32_t *cmd_buf;
	void *ptr;
	size_t len;
	struct cam_packet *pkt;
	struct cam_cmd_buf_desc *cmd_desc;

	rc = cam_mem_get_cpu_buf(handle,
		(uint64_t *)&packet, &len);
	if (rc < 0) {
		CAM_ERR(CAM_SENSOR, "Failed to get the command Buffer");
		return -EINVAL;
	}
	pkt = (struct cam_packet *)packet;
	cmd_desc = (struct cam_cmd_buf_desc *)
		((uint32_t *)&pkt->payload + pkt->cmd_buf_offset/4);
	if (cmd_desc == NULL) {
		CAM_ERR(CAM_SENSOR, "command descriptor pos is invalid");
		return -EINVAL;
	}
	if (pkt->num_cmd_buf != 2) {
		CAM_ERR(CAM_SENSOR, "Expected More Command Buffers : %d",
			 pkt->num_cmd_buf);
		return -EINVAL;
	}
	for (i = 0; i < pkt->num_cmd_buf; i++) {
		if (!(cmd_desc[i].length))
			continue;
		rc = cam_mem_get_cpu_buf(cmd_desc[i].mem_handle,
			(uint64_t *)&cmd_buf1, &len);
		if (rc < 0) {
			CAM_ERR(CAM_SENSOR,
				"Failed to parse the command Buffer Header");
			return -EINVAL;
		}
		cmd_buf = (uint32_t *)cmd_buf1;
		cmd_buf += cmd_desc[i].offset/4;
		ptr = (void *) cmd_buf;

		rc = cam_handle_cmd_buffers_for_probe(ptr, s_ctrl,
			i, cmd_desc[i].length);
		if (rc < 0) {
			CAM_ERR(CAM_SENSOR,
				"Failed to parse the command Buffer Header");
			return -EINVAL;
		}
	}
	return rc;
}

void cam_sensor_query_cap(struct cam_sensor_ctrl_t *s_ctrl,
	struct  cam_sensor_query_cap *query_cap)
{
	query_cap->pos_roll = s_ctrl->sensordata->pos_roll;
	query_cap->pos_pitch = s_ctrl->sensordata->pos_pitch;
	query_cap->pos_yaw = s_ctrl->sensordata->pos_yaw;
	query_cap->secure_camera = 0;
	query_cap->actuator_slot_id =
		s_ctrl->sensordata->subdev_id[SUB_MODULE_ACTUATOR];
	query_cap->csiphy_slot_id =
		s_ctrl->sensordata->subdev_id[SUB_MODULE_CSIPHY];
	query_cap->eeprom_slot_id =
		s_ctrl->sensordata->subdev_id[SUB_MODULE_EEPROM];
	query_cap->flash_slot_id =
		s_ctrl->sensordata->subdev_id[SUB_MODULE_LED_FLASH];
	query_cap->ois_slot_id =
		s_ctrl->sensordata->subdev_id[SUB_MODULE_OIS];
	query_cap->slot_info =
		s_ctrl->soc_info.index;
}

static uint16_t cam_sensor_id_by_mask(struct cam_sensor_ctrl_t *s_ctrl,
	uint32_t chipid)
{
	uint16_t sensor_id = (uint16_t)(chipid & 0xFFFF);
	int16_t sensor_id_mask = s_ctrl->sensordata->slave_info.sensor_id_mask;

	if (!sensor_id_mask)
		sensor_id_mask = ~sensor_id_mask;

	sensor_id &= sensor_id_mask;
	sensor_id_mask &= -sensor_id_mask;
	sensor_id_mask -= 1;
	while (sensor_id_mask) {
		sensor_id_mask >>= 1;
		sensor_id >>= 1;
	}
	return sensor_id;
}

void cam_sensor_shutdown(struct cam_sensor_ctrl_t *s_ctrl)
{
	struct cam_sensor_power_ctrl_t *power_info =
		&s_ctrl->sensordata->power_info;
	int rc = 0;

	s_ctrl->is_probe_succeed = 0;
	if (s_ctrl->sensor_state == CAM_SENSOR_INIT)
		return;

#ifdef VENDOR_EDIT
     CAM_INFO(CAM_SENSOR, "streamoff Sensor");
	rc = cam_sensor_apply_settings(s_ctrl, 0,
		CAM_SENSOR_PACKET_OPCODE_SENSOR_STREAMOFF);
	if (rc < 0) {
		CAM_INFO(CAM_SENSOR, "non-fatal cannot apply streamoff settings");
	}
#endif

	cam_sensor_release_resource(s_ctrl);
	cam_sensor_release_stream_rsc(s_ctrl);
	if (s_ctrl->sensor_state >= CAM_SENSOR_ACQUIRE)
		cam_sensor_power_down(s_ctrl);

	rc = cam_destroy_device_hdl(s_ctrl->bridge_intf.device_hdl);
	if (rc < 0)
		CAM_ERR(CAM_SENSOR, " failed destroying dhdl");
	s_ctrl->bridge_intf.device_hdl = -1;
	s_ctrl->bridge_intf.link_hdl = -1;
	s_ctrl->bridge_intf.session_hdl = -1;

	kfree(power_info->power_setting);
	kfree(power_info->power_down_setting);

	s_ctrl->streamon_count = 0;
	s_ctrl->streamoff_count = 0;
	s_ctrl->sensor_state = CAM_SENSOR_INIT;
}


#ifdef VENDOR_EDIT
/*add by yufeng@camera, 20181222 for get sensor version*/
#define SONY_SENSOR_MP0 (0x02)  //imx586 cut0.9(0X00)\cut0.91(0X01\0x02)
#define SONY_SENSOR_MP1 (0x03)  //imx586 cut1.0(0x03\0x04\0x10)\MP (0x1X)
#endif
int cam_sensor_match_id(struct cam_sensor_ctrl_t *s_ctrl)
{
	int rc = 0;
	uint32_t chipid = 0;
#ifdef VENDOR_EDIT
	uint32_t sensor_version = 0;
	uint16_t sensor_version_reg = 0x0018;
	uint32_t module_id =0;
	uint16_t tmp_slave_addr;
/* wanghaoran@camera.driver. 2018/11/2, add for read sensor gc5035 of camera */
    uint32_t gc5035_high = 0;
    uint32_t gc5035_low = 0;
    uint32_t chipid_high = 0;
    uint32_t chipid_low = 0;

#endif
	struct cam_camera_slave_info *slave_info;

	slave_info = &(s_ctrl->sensordata->slave_info);

	if (!slave_info) {
		CAM_ERR(CAM_SENSOR, " failed: %pK",
			 slave_info);
		return -EINVAL;
	}

	rc = camera_io_dev_read(
		&(s_ctrl->io_master_info),
		slave_info->sensor_id_reg_addr,
		&chipid, CAMERA_SENSOR_I2C_TYPE_WORD,
		CAMERA_SENSOR_I2C_TYPE_WORD);
#ifdef VENDOR_EDIT
/* wanghaoran@camera.driver. 2018/11/2, add for read sensor gc5035 of camera */
    if (slave_info->sensor_id == 0x5035
         || slave_info->sensor_id == 0x2375
         || slave_info->sensor_id == 0x2509) {
         gc5035_high = slave_info->sensor_id_reg_addr & 0xff00;
         gc5035_high = gc5035_high >> 8;
         gc5035_low = slave_info->sensor_id_reg_addr & 0x00ff;
         rc = camera_io_dev_read(
              &(s_ctrl->io_master_info),
              gc5035_high,
              &chipid_high, CAMERA_SENSOR_I2C_TYPE_BYTE,
              CAMERA_SENSOR_I2C_TYPE_BYTE);

         CAM_ERR(CAM_SENSOR, "gc5035_high: 0x%x chipid_high id 0x%x:",
                 gc5035_high, chipid_high);

         rc = camera_io_dev_read(
              &(s_ctrl->io_master_info),
              gc5035_low,
              &chipid_low, CAMERA_SENSOR_I2C_TYPE_BYTE,
              CAMERA_SENSOR_I2C_TYPE_BYTE);

         CAM_ERR(CAM_SENSOR, "gc5035_low: 0x%x chipid_low id 0x%x:",
                gc5035_low, chipid_low);

         chipid = ((chipid_high << 8) & 0xff00) | (chipid_low & 0x00ff);

     }

#endif

	CAM_DBG(CAM_SENSOR, "read id: 0x%x expected id 0x%x:",
			 chipid, slave_info->sensor_id);

	if (cam_sensor_id_by_mask(s_ctrl, chipid) != slave_info->sensor_id) {
		CAM_ERR(CAM_SENSOR, "chip id %x does not match %x",
				chipid, slave_info->sensor_id);
		return -ENODEV;
	}

#ifdef VENDOR_EDIT
	/*add by yufeng@camera, 20181222 for multi sensor version*/
	if (chipid == 0x586) {
		rc = camera_io_dev_read(
			&(s_ctrl->io_master_info),
			sensor_version_reg,
			&sensor_version, CAMERA_SENSOR_I2C_TYPE_WORD,
			CAMERA_SENSOR_I2C_TYPE_WORD);

			CAM_INFO(CAM_SENSOR, "imx586 sensor_version: 0x%x",
				sensor_version >> 8);
		if ((sensor_version >> 8) >= SONY_SENSOR_MP1) {
			s_ctrl->sensordata->slave_info.sensor_version = 1;
		} else {
			s_ctrl->sensordata->slave_info.sensor_version = 0;
		}
		CAM_INFO(CAM_SENSOR, "imx586 slave_info.sensor_version: %d:",
				s_ctrl->sensordata->slave_info.sensor_version );
	}
	if (chipid == 0x519) {
		rc = camera_io_dev_read(
			&(s_ctrl->io_master_info),
			sensor_version_reg,
			&sensor_version, CAMERA_SENSOR_I2C_TYPE_WORD,
			CAMERA_SENSOR_I2C_TYPE_WORD);

			CAM_INFO(CAM_SENSOR, "imx519 sensor_version: 0x%x",
				sensor_version >> 8);
	}
	if (chipid == 0x576) {
		rc = camera_io_dev_read(
			&(s_ctrl->io_master_info),
			sensor_version_reg,
			&sensor_version, CAMERA_SENSOR_I2C_TYPE_WORD,
			CAMERA_SENSOR_I2C_TYPE_WORD);

			CAM_INFO(CAM_SENSOR, "imx576 sensor_version: 0x%x",
				sensor_version >> 8);
	}

	if (chipid == 0x559B) {
		rc = camera_io_dev_read(
			&(s_ctrl->io_master_info),
			0x0002,
			&sensor_version, CAMERA_SENSOR_I2C_TYPE_WORD,
			CAMERA_SENSOR_I2C_TYPE_WORD);

			CAM_INFO(CAM_SENSOR, "S5k5e9 sensor_version: 0x%x",
				sensor_version >> 8);
	}
	/*add by hongbo.dai@camera 20180831, for support multi camera resource*/
	if (chipid == 0x519 || chipid == 0x576 || chipid == 0x559B || chipid == 0x471) {
		if (slave_info->eeprom_slave_addr != 0) {
			//change to eeprom slave address for read vendor ID
			tmp_slave_addr = s_ctrl->io_master_info.cci_client->sid;
			s_ctrl->io_master_info.cci_client->sid = (slave_info->eeprom_slave_addr >> 1);
			rc = camera_io_dev_read(
				&(s_ctrl->io_master_info),
				EEPROM_MODE_ADDR,
				&module_id, CAMERA_SENSOR_I2C_TYPE_WORD,
				CAMERA_SENSOR_I2C_TYPE_BYTE);
			//back to sensor slave address
			s_ctrl->io_master_info.cci_client->sid = (tmp_slave_addr);
			if (module_id != 0xff && slave_info->vendor_id != module_id) {//no eeprom for imx519
				CAM_ERR(CAM_SENSOR, "eeprom module id %x does not match %x, sensor:%x: match eeprom module id failed",
					module_id, slave_info->vendor_id,chipid);
				return -ENODEV;
			}

			CAM_ERR(CAM_SENSOR, "slave addr: 0x%x eeprom module id: 0x%x",
				slave_info->eeprom_slave_addr,
				module_id);
			//no eeprom for imx519 added by wanghaoran at 20190107
			if (module_id != 0xff) {
			   slave_info->vendor_id = (uint16_t)module_id;
			   if (slave_info->camera_id == 0)
			      rear_main_vendor = slave_info->vendor_id;
			   else if (slave_info->camera_id == 1)
				  front_main_vendor = slave_info->vendor_id;
			}
		} else { //no eeprom
			if (slave_info->camera_id == 2) {
				if ((rear_main_vendor > 0) && (rear_main_vendor != slave_info->vendor_id)) {
				    CAM_ERR(CAM_SENSOR, "eeprom module id %x does not match %x",
					    rear_main_vendor, slave_info->vendor_id);
				    return -ENODEV;
				}
			} else if (slave_info->camera_id == 3) {
				if ((front_main_vendor > 0) && (front_main_vendor != slave_info->vendor_id)) {
				    CAM_ERR(CAM_SENSOR, "eeprom module id %x does not match %x",
					    front_main_vendor, slave_info->vendor_id);
				    return -ENODEV;
				}
			}
		}
	}

	if (slave_info->sensor_id == 0x519 && fuse_id[0] == '\0') {
		sensor_get_fuseid(s_ctrl);
		CAM_ERR(CAM_SENSOR,
			"sensor_id: 0x%x, fuse_id:%s",
			slave_info->sensor_id,
			fuse_id);
	}

	/*Jindian.Guan@Camera.Drv, 20181207, add for imx471 DFCT info*/
	if (slave_info->sensor_id == 0x0471) {
		sensor_imx471_get_dpc_data(s_ctrl);
	}

	/*Jindian.Guan@Camera.Driver, 2019/01/04, add for [malloc imx586 qsc memory early]*/
	if (slave_info->sensor_id == 0x0586) {
		if (i2c_settings_list_vendor == NULL) {
			i2c_settings_list_vendor = (struct i2c_settings_list *)
				kzalloc(sizeof(struct i2c_settings_list), GFP_KERNEL);
			if (i2c_settings_list_vendor)
				CAM_DBG(CAM_SENSOR,"imx586 probe spc malloc list sucess,list %p",i2c_settings_list_vendor);
		}

		if (reg_setting_vendor == NULL) {
			vendor_size = IMX586QSC_SIZE;
			reg_setting_vendor = (struct cam_sensor_i2c_reg_array *)
				kzalloc(sizeof(struct cam_sensor_i2c_reg_array) *
				vendor_size, GFP_KERNEL);
			if (reg_setting_vendor)
				CAM_DBG(CAM_SENSOR,"imx586  probe spc malloc reg sucess reg %p",reg_setting_vendor);
		}
	}
	/*wanghaoran@Camera.Drv, 20190216, add for gc5035 DFCT and reg update info*/
	if (slave_info->sensor_id == 0x5035) {
	    sensor_gc5035_get_dpc_data(s_ctrl);
	}

#endif
	return rc;
}

int32_t cam_sensor_driver_cmd(struct cam_sensor_ctrl_t *s_ctrl,
	void *arg)
{
	int rc = 0;
	struct cam_control *cmd = (struct cam_control *)arg;
	struct cam_sensor_power_setting *pu = NULL;
	struct cam_sensor_power_setting *pd = NULL;

	struct cam_sensor_power_ctrl_t *power_info =
		&s_ctrl->sensordata->power_info;
	if (!s_ctrl || !arg) {
		CAM_ERR(CAM_SENSOR, "s_ctrl is NULL");
		return -EINVAL;
	}

	if (cmd->op_code != CAM_SENSOR_PROBE_CMD) {
		if (cmd->handle_type != CAM_HANDLE_USER_POINTER) {
			CAM_ERR(CAM_SENSOR, "Invalid handle type: %d",
				cmd->handle_type);
			return -EINVAL;
		}
	}

	mutex_lock(&(s_ctrl->cam_sensor_mutex));
	switch (cmd->op_code) {
	case CAM_SENSOR_PROBE_CMD: {
		if (s_ctrl->is_probe_succeed == 1) {
			CAM_ERR(CAM_SENSOR,
				"Already Sensor Probed in the slot");
			break;
		}

		/* Allocate memory for power up setting */
		pu = kzalloc(sizeof(struct cam_sensor_power_setting) *
			MAX_POWER_CONFIG, GFP_KERNEL);
		if (!pu) {
			rc = -ENOMEM;
			goto release_mutex;
		}

		pd = kzalloc(sizeof(struct cam_sensor_power_setting) *
			MAX_POWER_CONFIG, GFP_KERNEL);
		if (!pd) {
			kfree(pu);
			rc = -ENOMEM;
			goto release_mutex;
		}

		power_info->power_setting = pu;
		power_info->power_down_setting = pd;

		if (cmd->handle_type ==
			CAM_HANDLE_MEM_HANDLE) {
			rc = cam_handle_mem_ptr(cmd->handle, s_ctrl);
			if (rc < 0) {
				CAM_ERR(CAM_SENSOR, "Get Buffer Handle Failed");
				kfree(pu);
				kfree(pd);
				goto release_mutex;
			}
		} else {
			CAM_ERR(CAM_SENSOR, "Invalid Command Type: %d",
				 cmd->handle_type);
			return -EINVAL;
		}

		/* Parse and fill vreg params for powerup settings */
		rc = msm_camera_fill_vreg_params(
			&s_ctrl->soc_info,
			s_ctrl->sensordata->power_info.power_setting,
			s_ctrl->sensordata->power_info.power_setting_size);
		if (rc < 0) {
			CAM_ERR(CAM_SENSOR,
				"Fail in filling vreg params for PUP rc %d",
				 rc);
			kfree(pu);
			kfree(pd);
			goto release_mutex;
		}

		/* Parse and fill vreg params for powerdown settings*/
		rc = msm_camera_fill_vreg_params(
			&s_ctrl->soc_info,
			s_ctrl->sensordata->power_info.power_down_setting,
			s_ctrl->sensordata->power_info.power_down_setting_size);
		if (rc < 0) {
			CAM_ERR(CAM_SENSOR,
				"Fail in filling vreg params for PDOWN rc %d",
				 rc);
			kfree(pu);
			kfree(pd);
			goto release_mutex;
		}

		/* Power up and probe sensor */
		rc = cam_sensor_power_up(s_ctrl);
		if (rc < 0) {
			CAM_ERR(CAM_SENSOR, "power up failed");
			kfree(pu);
			kfree(pd);
			goto release_mutex;
		}

		/* Match sensor ID */
		rc = cam_sensor_match_id(s_ctrl);
		if (rc < 0) {
			cam_sensor_power_down(s_ctrl);
			msleep(20);
			kfree(pu);
			kfree(pd);
			goto release_mutex;
		}
		#ifdef VENDOR_EDIT
		/*add by yufeng@camera, 20181222 for get sensor version*/
		cmd->reserved = s_ctrl->sensordata->slave_info.sensor_version;
		#endif

		CAM_INFO(CAM_SENSOR,
			"Probe Succees,slot:%d,slave_addr:0x%x,sensor_id:0x%x",
			s_ctrl->soc_info.index,
			s_ctrl->sensordata->slave_info.sensor_slave_addr,
			s_ctrl->sensordata->slave_info.sensor_id);

		rc = cam_sensor_power_down(s_ctrl);
		if (rc < 0) {
			CAM_ERR(CAM_SENSOR, "fail in Sensor Power Down");
			kfree(pu);
			kfree(pd);
			goto release_mutex;
		}
		/*
		 * Set probe succeeded flag to 1 so that no other camera shall
		 * probed on this slot
		 */
		s_ctrl->is_probe_succeed = 1;
		s_ctrl->sensor_state = CAM_SENSOR_INIT;
	}
		break;
	case CAM_ACQUIRE_DEV: {
		struct cam_sensor_acquire_dev sensor_acq_dev;
		struct cam_create_dev_hdl bridge_params;

		if (s_ctrl->bridge_intf.device_hdl != -1) {
			CAM_ERR(CAM_SENSOR, "Device is already acquired");
			rc = -EINVAL;
			goto release_mutex;
		}
		rc = copy_from_user(&sensor_acq_dev,
			(void __user *) cmd->handle, sizeof(sensor_acq_dev));
		if (rc < 0) {
			CAM_ERR(CAM_SENSOR, "Failed Copying from user");
			goto release_mutex;
		}

		bridge_params.session_hdl = sensor_acq_dev.session_handle;
		bridge_params.ops = &s_ctrl->bridge_intf.ops;
		bridge_params.v4l2_sub_dev_flag = 0;
		bridge_params.media_entity_flag = 0;
		bridge_params.priv = s_ctrl;

		sensor_acq_dev.device_handle =
			cam_create_device_hdl(&bridge_params);
		s_ctrl->bridge_intf.device_hdl = sensor_acq_dev.device_handle;
		s_ctrl->bridge_intf.session_hdl = sensor_acq_dev.session_handle;

		CAM_DBG(CAM_SENSOR, "Device Handle: %d",
			sensor_acq_dev.device_handle);
		if (copy_to_user((void __user *) cmd->handle, &sensor_acq_dev,
			sizeof(struct cam_sensor_acquire_dev))) {
			CAM_ERR(CAM_SENSOR, "Failed Copy to User");
			rc = -EFAULT;
			goto release_mutex;
		}

		rc = cam_sensor_power_up(s_ctrl);
		if (rc < 0) {
			CAM_ERR(CAM_SENSOR, "Sensor Power up failed");
			goto release_mutex;
		}

		s_ctrl->sensor_state = CAM_SENSOR_ACQUIRE;
		CAM_INFO(CAM_SENSOR,
			"CAM_ACQUIRE_DEV Success, sensor_id:0x%x,sensor_slave_addr:0x%x",
			s_ctrl->sensordata->slave_info.sensor_id,
			s_ctrl->sensordata->slave_info.sensor_slave_addr);
	}
		break;
	case CAM_RELEASE_DEV: {
		if ((s_ctrl->sensor_state == CAM_SENSOR_INIT) ||
			(s_ctrl->sensor_state == CAM_SENSOR_START)) {
			rc = -EINVAL;
			CAM_WARN(CAM_SENSOR,
			"Not in right state to release : %d",
			s_ctrl->sensor_state);
			goto release_mutex;
		}

		rc = cam_sensor_power_down(s_ctrl);
		if (rc < 0) {
			CAM_ERR(CAM_SENSOR, "Sensor Power Down failed");
			goto release_mutex;
		}

		cam_sensor_release_resource(s_ctrl);
		cam_sensor_release_stream_rsc(s_ctrl);
		if (s_ctrl->bridge_intf.device_hdl == -1) {
			CAM_ERR(CAM_SENSOR,
				"Invalid Handles: link hdl: %d device hdl: %d",
				s_ctrl->bridge_intf.device_hdl,
				s_ctrl->bridge_intf.link_hdl);
			rc = -EINVAL;
			goto release_mutex;
		}
		rc = cam_destroy_device_hdl(s_ctrl->bridge_intf.device_hdl);
		if (rc < 0)
			CAM_ERR(CAM_SENSOR,
				"failed in destroying the device hdl");
		s_ctrl->bridge_intf.device_hdl = -1;
		s_ctrl->bridge_intf.link_hdl = -1;
		s_ctrl->bridge_intf.session_hdl = -1;

		s_ctrl->sensor_state = CAM_SENSOR_INIT;
		CAM_INFO(CAM_SENSOR,
			"CAM_RELEASE_DEV Success, sensor_id:0x%x,sensor_slave_addr:0x%x",
			s_ctrl->sensordata->slave_info.sensor_id,
			s_ctrl->sensordata->slave_info.sensor_slave_addr);
		s_ctrl->streamon_count = 0;
		s_ctrl->streamoff_count = 0;
	}
		break;
	case CAM_QUERY_CAP: {
		struct  cam_sensor_query_cap sensor_cap;

		cam_sensor_query_cap(s_ctrl, &sensor_cap);
		if (copy_to_user((void __user *) cmd->handle, &sensor_cap,
			sizeof(struct  cam_sensor_query_cap))) {
			CAM_ERR(CAM_SENSOR, "Failed Copy to User");
			rc = -EFAULT;
			goto release_mutex;
		}
		break;
	}
	case CAM_START_DEV: {
		if ((s_ctrl->sensor_state == CAM_SENSOR_INIT) ||
			(s_ctrl->sensor_state == CAM_SENSOR_START)) {
			rc = -EINVAL;
			CAM_WARN(CAM_SENSOR,
			"Not in right state to start : %d",
			s_ctrl->sensor_state);
			goto release_mutex;
		}

		if (s_ctrl->i2c_data.streamon_settings.is_settings_valid &&
			(s_ctrl->i2c_data.streamon_settings.request_id == 0)) {
			rc = cam_sensor_apply_settings(s_ctrl, 0,
				CAM_SENSOR_PACKET_OPCODE_SENSOR_STREAMON);
			if (rc < 0) {
				CAM_ERR(CAM_SENSOR,
					"cannot apply streamon settings");
				goto release_mutex;
			}
		}
		s_ctrl->sensor_state = CAM_SENSOR_START;
		CAM_INFO(CAM_SENSOR,
			"CAM_START_DEV Success, sensor_id:0x%x,sensor_slave_addr:0x%x",
			s_ctrl->sensordata->slave_info.sensor_id,
			s_ctrl->sensordata->slave_info.sensor_slave_addr);
	}
		break;
	case CAM_STOP_DEV: {
#ifndef VENDOR_EDIT
		/*Jinshui.Liu@Camera.Driver, 2019/02/22, modify for [allow stop in config state]*/
		if (s_ctrl->sensor_state != CAM_SENSOR_START) {
			rc = -EINVAL;
			CAM_WARN(CAM_SENSOR,
			"Not in right state to stop : %d",
			s_ctrl->sensor_state);
			goto release_mutex;
		}
#else
		if (s_ctrl->sensor_state != CAM_SENSOR_START
			&& s_ctrl->sensor_state != CAM_SENSOR_CONFIG) {
			rc = -EINVAL;
			CAM_WARN(CAM_SENSOR,
			"Not in right state to stop : %d",
			s_ctrl->sensor_state);
			goto release_mutex;
		}
#endif

		if (s_ctrl->i2c_data.streamoff_settings.is_settings_valid &&
			(s_ctrl->i2c_data.streamoff_settings.request_id == 0)) {
			rc = cam_sensor_apply_settings(s_ctrl, 0,
				CAM_SENSOR_PACKET_OPCODE_SENSOR_STREAMOFF);
			if (rc < 0) {
				CAM_ERR(CAM_SENSOR,
				"cannot apply streamoff settings");
			}
		}

		cam_sensor_release_resource(s_ctrl);
		s_ctrl->sensor_state = CAM_SENSOR_ACQUIRE;
		CAM_INFO(CAM_SENSOR,
			"CAM_STOP_DEV Success, sensor_id:0x%x,sensor_slave_addr:0x%x",
			s_ctrl->sensordata->slave_info.sensor_id,
			s_ctrl->sensordata->slave_info.sensor_slave_addr);
	}
		break;
	case CAM_CONFIG_DEV: {
		rc = cam_sensor_i2c_pkt_parse(s_ctrl, arg);
		if (rc < 0) {
			CAM_ERR(CAM_SENSOR, "Failed CCI Config: %d", rc);
			goto release_mutex;
		}
		if (s_ctrl->i2c_data.init_settings.is_settings_valid &&
			(s_ctrl->i2c_data.init_settings.request_id == 0)) {

			rc = cam_sensor_apply_settings(s_ctrl, 0,
				CAM_SENSOR_PACKET_OPCODE_SENSOR_INITIAL_CONFIG);
			if (rc < 0) {
				CAM_ERR(CAM_SENSOR,
					"cannot apply init settings");
				goto release_mutex;
			}
			rc = delete_request(&s_ctrl->i2c_data.init_settings);
			if (rc < 0) {
				CAM_ERR(CAM_SENSOR,
					"Fail in deleting the Init settings");
				goto release_mutex;
			}
			s_ctrl->i2c_data.init_settings.request_id = -1;
		}

		if (s_ctrl->i2c_data.config_settings.is_settings_valid &&
			(s_ctrl->i2c_data.config_settings.request_id == 0)) {
			rc = cam_sensor_apply_settings(s_ctrl, 0,
				CAM_SENSOR_PACKET_OPCODE_SENSOR_CONFIG);
			if (rc < 0) {
				CAM_ERR(CAM_SENSOR,
					"cannot apply config settings");
				goto release_mutex;
			}
			rc = delete_request(&s_ctrl->i2c_data.config_settings);
			if (rc < 0) {
				CAM_ERR(CAM_SENSOR,
					"Fail in deleting the config settings");
				goto release_mutex;
			}
			s_ctrl->sensor_state = CAM_SENSOR_CONFIG;
			s_ctrl->i2c_data.config_settings.request_id = -1;
		}
	}
		break;
#ifdef VENDOR_EDIT
	case CAM_GET_FUSE_ID: {
		CAM_ERR(CAM_SENSOR, "fuse_id:%s", fuse_id);
		if (fuse_id[0] == '\0') {
			CAM_ERR(CAM_SENSOR, "fuse_id is empty");
			rc = -EFAULT;
			goto release_mutex;
		} else if (copy_to_user((void __user *) cmd->handle, &fuse_id,
			sizeof(fuse_id))) {
			CAM_ERR(CAM_SENSOR, "Failed Copy to User");
			rc = -EFAULT;
			goto release_mutex;
		}
		break;
	}

    /*Jindian.Guan@Camera.Drv, 20181207, add for imx471 DFCT info*/
	case CAM_GET_DPC_DATA: {
		if (0x0471 != s_ctrl->sensordata->slave_info.sensor_id) {
			rc = -EFAULT;
			goto release_mutex;
		}
		CAM_INFO(CAM_SENSOR, "imx471_dfct_tbl: fd_dfct_num=%d, sg_dfct_num=%d",
			imx471_dfct_tbl.fd_dfct_num, imx471_dfct_tbl.sg_dfct_num);
		if (copy_to_user((void __user *) cmd->handle, &imx471_dfct_tbl,
			sizeof(struct  sony_dfct_tbl_t))) {
			CAM_ERR(CAM_SENSOR, "Failed Copy to User");
			rc = -EFAULT;
			goto release_mutex;
		}
	}
		break;
#endif
	default:
		CAM_ERR(CAM_SENSOR, "Invalid Opcode: %d", cmd->op_code);
		rc = -EINVAL;
		goto release_mutex;
	}

release_mutex:
	mutex_unlock(&(s_ctrl->cam_sensor_mutex));
	return rc;
}

int cam_sensor_publish_dev_info(struct cam_req_mgr_device_info *info)
{
	int rc = 0;

	if (!info)
		return -EINVAL;

	info->dev_id = CAM_REQ_MGR_DEVICE_SENSOR;
	strlcpy(info->name, CAM_SENSOR_NAME, sizeof(info->name));
	info->p_delay = 2;
	info->trigger = CAM_TRIGGER_POINT_SOF;

	return rc;
}

int cam_sensor_establish_link(struct cam_req_mgr_core_dev_link_setup *link)
{
	struct cam_sensor_ctrl_t *s_ctrl = NULL;

	if (!link)
		return -EINVAL;

	s_ctrl = (struct cam_sensor_ctrl_t *)
		cam_get_device_priv(link->dev_hdl);
	if (!s_ctrl) {
		CAM_ERR(CAM_SENSOR, "Device data is NULL");
		return -EINVAL;
	}
	if (link->link_enable) {
		s_ctrl->bridge_intf.link_hdl = link->link_hdl;
		s_ctrl->bridge_intf.crm_cb = link->crm_cb;
	} else {
		s_ctrl->bridge_intf.link_hdl = -1;
		s_ctrl->bridge_intf.crm_cb = NULL;
	}

	return 0;
}

int cam_sensor_power(struct v4l2_subdev *sd, int on)
{
	struct cam_sensor_ctrl_t *s_ctrl = v4l2_get_subdevdata(sd);

	mutex_lock(&(s_ctrl->cam_sensor_mutex));
	if (!on && s_ctrl->sensor_state == CAM_SENSOR_START) {
		cam_sensor_power_down(s_ctrl);
		s_ctrl->sensor_state = CAM_SENSOR_ACQUIRE;
	}
	mutex_unlock(&(s_ctrl->cam_sensor_mutex));

	return 0;
}

#ifdef VENDOR_EDIT
/*Jinshui.Liu@Camera.Driver, 2018/06/23, add for [tof watchdog]*/
static int cam_sensor_wtd_trigger(int watchdog_gpio)
{
	int kick_config = 0;

	kick_config = gpio_get_value(watchdog_gpio);
	gpio_direction_output(watchdog_gpio, (kick_config == 1) ? 0: 1);

	return 0;
}

enum hrtimer_restart cam_sensor_hrtimer_callback(struct hrtimer *hrt_ptr)
{
	ktime_t ktime;

	cam_sensor_wtd_trigger(tof_watchdog_goio);

	ktime = ktime_set(0, (50000 % 1000000) * 1000);
	hrtimer_start(&tof_watchdog_timer, ktime, HRTIMER_MODE_REL);

	return HRTIMER_NORESTART;
}

static int cam_sensor_hrtimer_init(struct cam_sensor_board_info * sensordata)
{
	hrtimer_init(&tof_watchdog_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	tof_watchdog_timer.function = cam_sensor_hrtimer_callback;

	return 0;
}

static int cam_sensor_hrtimer_start(struct cam_sensor_board_info * sensordata)
{
	ktime_t ktime;

	cam_sensor_wtd_trigger(sensordata->watchdog_gpio);
	tof_watchdog_goio = sensordata->watchdog_gpio;

	ktime = ktime_set(0, (50000 % 1000000) * 1000);
	hrtimer_start(&tof_watchdog_timer, ktime, HRTIMER_MODE_REL);

	return 0;
}
#endif

int cam_sensor_power_up(struct cam_sensor_ctrl_t *s_ctrl)
{
	int rc;
	struct cam_sensor_power_ctrl_t *power_info;
	struct cam_camera_slave_info *slave_info;
	struct cam_hw_soc_info *soc_info =
		&s_ctrl->soc_info;

	if (!s_ctrl) {
		CAM_ERR(CAM_SENSOR, "failed: %pK", s_ctrl);
		return -EINVAL;
	}

	power_info = &s_ctrl->sensordata->power_info;
	slave_info = &(s_ctrl->sensordata->slave_info);

	if (!power_info || !slave_info) {
		CAM_ERR(CAM_SENSOR, "failed: %pK %pK", power_info, slave_info);
		return -EINVAL;
	}

#ifdef VENDOR_EDIT
	/*Jinshui.Liu@Camera.Driver, 2018/06/23, add for [tof watchdog]*/
	if (s_ctrl->sensordata->watchdog_gpio != -1) {
		cam_sensor_hrtimer_init(s_ctrl->sensordata);
		cam_sensor_hrtimer_start(s_ctrl->sensordata);
	}
#endif

	rc = cam_sensor_core_power_up(power_info, soc_info);
	if (rc < 0) {
		CAM_ERR(CAM_SENSOR, "power up the core is failed:%d", rc);
		return rc;
	}

	rc = camera_io_init(&(s_ctrl->io_master_info));
	if (rc < 0)
		CAM_ERR(CAM_SENSOR, "cci_init failed: rc: %d", rc);

	return rc;
}

int cam_sensor_power_down(struct cam_sensor_ctrl_t *s_ctrl)
{
	struct cam_sensor_power_ctrl_t *power_info;
	struct cam_hw_soc_info *soc_info;
	int rc = 0;

	if (!s_ctrl) {
		CAM_ERR(CAM_SENSOR, "failed: s_ctrl %pK", s_ctrl);
		return -EINVAL;
	}

	power_info = &s_ctrl->sensordata->power_info;
	soc_info = &s_ctrl->soc_info;

	if (!power_info) {
		CAM_ERR(CAM_SENSOR, "failed: power_info %pK", power_info);
		return -EINVAL;
	}
#ifdef VENDOR_EDIT
	/*Jinshui.Liu@Camera.Driver, 2018/06/23, add for [tof watchdog]*/
	if (s_ctrl->sensordata->watchdog_gpio != -1) {
		hrtimer_cancel(&tof_watchdog_timer);
	}
#endif
	rc = msm_camera_power_down(power_info, soc_info);
	if (rc < 0) {
		CAM_ERR(CAM_SENSOR, "power down the core is failed:%d", rc);
		return rc;
	}

	camera_io_release(&(s_ctrl->io_master_info));

	return rc;
}

int cam_sensor_apply_settings(struct cam_sensor_ctrl_t *s_ctrl,
	int64_t req_id, enum cam_sensor_packet_opcodes opcode)
{
	int rc = 0, offset, i;
	uint64_t top = 0, del_req_id = 0;
	struct i2c_settings_array *i2c_set = NULL;
	struct i2c_settings_list *i2c_list;

	if (req_id == 0) {
		switch (opcode) {
		case CAM_SENSOR_PACKET_OPCODE_SENSOR_STREAMON: {
			i2c_set = &s_ctrl->i2c_data.streamon_settings;
			break;
		}
		case CAM_SENSOR_PACKET_OPCODE_SENSOR_INITIAL_CONFIG: {
			i2c_set = &s_ctrl->i2c_data.init_settings;
			break;
		}
		case CAM_SENSOR_PACKET_OPCODE_SENSOR_CONFIG: {
			i2c_set = &s_ctrl->i2c_data.config_settings;
			break;
		}
		case CAM_SENSOR_PACKET_OPCODE_SENSOR_STREAMOFF: {
			i2c_set = &s_ctrl->i2c_data.streamoff_settings;
			break;
		}
		case CAM_SENSOR_PACKET_OPCODE_SENSOR_UPDATE:
		case CAM_SENSOR_PACKET_OPCODE_SENSOR_PROBE:
		default:
			return 0;
		}
		if (i2c_set->is_settings_valid == 1) {
			list_for_each_entry(i2c_list,
				&(i2c_set->list_head), list) {
#ifdef VENDOR_EDIT
                /* wanghaoran@camera.driver. 2018/11/2, add for read sensor gc5035 of camera */
                if (s_ctrl->sensordata->slave_info.sensor_id == 0x5035
                    || s_ctrl->sensordata->slave_info.sensor_id == 0x2375) {
                        i2c_list->i2c_settings.addr_type = CAMERA_SENSOR_I2C_TYPE_BYTE;
                        CAM_DBG(CAM_SENSOR,
                                "i2c_list->i2c_settings.addr_type: %d",
                                i2c_list->i2c_settings.addr_type);
                }
#endif

				rc = cam_sensor_i2c_modes_util(
					&(s_ctrl->io_master_info),
					i2c_list);
				if (rc < 0) {
					CAM_ERR(CAM_SENSOR,
						"Failed to apply settings: %d",
						rc);
					return rc;
				}
#ifdef VENDOR_EDIT
/* wanghaoran@camera.driver. 2019/02/16, add for read sensor gc5035 of camera dpc and reg update */
				if (s_ctrl->sensordata->slave_info.sensor_id == 0x5035
				    && opcode ==  CAM_SENSOR_PACKET_OPCODE_SENSOR_INITIAL_CONFIG) {
				    sensor_gc5035_write_dpc_data(s_ctrl);

				    sensor_gc5035_update_reg(s_ctrl);
				}
#endif
			}
		}
	} else {
		offset = req_id % MAX_PER_FRAME_ARRAY;
		i2c_set = &(s_ctrl->i2c_data.per_frame[offset]);
		if (i2c_set->is_settings_valid == 1 &&
			i2c_set->request_id == req_id) {
			list_for_each_entry(i2c_list,
				&(i2c_set->list_head), list) {
#ifdef VENDOR_EDIT
                /* wanghaoran@camera.driver. 2018/11/2, add for read sensor gc5035 of camera */
                if (s_ctrl->sensordata->slave_info.sensor_id == 0x5035
                    || s_ctrl->sensordata->slave_info.sensor_id == 0x2375) {
                   i2c_list->i2c_settings.addr_type = CAMERA_SENSOR_I2C_TYPE_BYTE;
                   CAM_DBG(CAM_SENSOR,
                                  "i2c_list->i2c_settings.addr_type: %d",
                                   i2c_list->i2c_settings.addr_type);
                }
#endif
				rc = cam_sensor_i2c_modes_util(
					&(s_ctrl->io_master_info),
					i2c_list);
				if (rc < 0) {
					CAM_ERR(CAM_SENSOR,
						"Failed to apply settings: %d",
						rc);
					return rc;
				}
			}
		} else {
			CAM_DBG(CAM_SENSOR,
				"Invalid/NOP request to apply: %lld", req_id);
		}

		/* Change the logic dynamically */
		for (i = 0; i < MAX_PER_FRAME_ARRAY; i++) {
			if ((req_id >=
				s_ctrl->i2c_data.per_frame[i].request_id) &&
				(top <
				s_ctrl->i2c_data.per_frame[i].request_id) &&
				(s_ctrl->i2c_data.per_frame[i].
				is_settings_valid == 1)) {
				del_req_id = top;
				top = s_ctrl->i2c_data.per_frame[i].request_id;
			}
		}

		if (top < req_id) {
			if ((((top % MAX_PER_FRAME_ARRAY) - (req_id %
				MAX_PER_FRAME_ARRAY)) >= BATCH_SIZE_MAX) ||
				(((top % MAX_PER_FRAME_ARRAY) - (req_id %
				MAX_PER_FRAME_ARRAY)) <= -BATCH_SIZE_MAX))
				del_req_id = req_id;
		}

		if (!del_req_id)
			return rc;

		CAM_DBG(CAM_SENSOR, "top: %llu, del_req_id:%llu",
			top, del_req_id);

		for (i = 0; i < MAX_PER_FRAME_ARRAY; i++) {
			if ((del_req_id >
				 s_ctrl->i2c_data.per_frame[i].request_id) && (
				 s_ctrl->i2c_data.per_frame[i].is_settings_valid
					== 1)) {
				s_ctrl->i2c_data.per_frame[i].request_id = 0;
				rc = delete_request(
					&(s_ctrl->i2c_data.per_frame[i]));
				if (rc < 0)
					CAM_ERR(CAM_SENSOR,
						"Delete request Fail:%lld rc:%d",
						del_req_id, rc);
			}
		}
	}

	return rc;
}

int32_t cam_sensor_apply_request(struct cam_req_mgr_apply_request *apply)
{
	int32_t rc = 0;
	struct cam_sensor_ctrl_t *s_ctrl = NULL;

	if (!apply)
		return -EINVAL;

	s_ctrl = (struct cam_sensor_ctrl_t *)
		cam_get_device_priv(apply->dev_hdl);
	if (!s_ctrl) {
		CAM_ERR(CAM_SENSOR, "Device data is NULL");
		return -EINVAL;
	}
	CAM_DBG(CAM_SENSOR, " Req Id: %lld", apply->request_id);
	trace_cam_apply_req("Sensor", apply->request_id);
#ifdef VENDOR_EDIT
	/* Tengfeng.Wang@camera.driver. 2019/2/18, add for Qualcomm patch */
	mutex_lock(&(s_ctrl->cam_sensor_mutex));
#endif
	rc = cam_sensor_apply_settings(s_ctrl, apply->request_id,
		CAM_SENSOR_PACKET_OPCODE_SENSOR_UPDATE);
#ifdef VENDOR_EDIT
	/* Tengfeng.Wang@camera.driver. 2019/2/18, add for Qualcomm patch */
	mutex_unlock(&(s_ctrl->cam_sensor_mutex));
#endif
	return rc;
}

int32_t cam_sensor_flush_request(struct cam_req_mgr_flush_request *flush_req)
{
	int32_t rc = 0, i;
	uint32_t cancel_req_id_found = 0;
	struct cam_sensor_ctrl_t *s_ctrl = NULL;
	struct i2c_settings_array *i2c_set = NULL;

	if (!flush_req)
		return -EINVAL;

	s_ctrl = (struct cam_sensor_ctrl_t *)
		cam_get_device_priv(flush_req->dev_hdl);
	if (!s_ctrl) {
		CAM_ERR(CAM_SENSOR, "Device data is NULL");
		return -EINVAL;
	}

	for (i = 0; i < MAX_PER_FRAME_ARRAY; i++) {
		i2c_set = &(s_ctrl->i2c_data.per_frame[i]);

		if ((flush_req->type == CAM_REQ_MGR_FLUSH_TYPE_CANCEL_REQ)
				&& (i2c_set->request_id != flush_req->req_id))
			continue;

		if (i2c_set->is_settings_valid == 1) {
			rc = delete_request(i2c_set);
			if (rc < 0)
				CAM_ERR(CAM_SENSOR,
					"delete request: %lld rc: %d",
					i2c_set->request_id, rc);

			if (flush_req->type ==
				CAM_REQ_MGR_FLUSH_TYPE_CANCEL_REQ) {
				cancel_req_id_found = 1;
				break;
			}
		}
	}

	if (flush_req->type == CAM_REQ_MGR_FLUSH_TYPE_CANCEL_REQ &&
		!cancel_req_id_found)
		CAM_DBG(CAM_SENSOR,
			"Flush request id:%lld not found in the pending list",
			flush_req->req_id);
	return rc;
}
