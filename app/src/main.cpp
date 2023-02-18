/*
 * Copyright (c) The Libre Solar Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef UNIT_TEST

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/can.h>

#include <stdio.h>

#include "bms.h"
#include "button.h"
#include "data_objects.h"
#include "helper.h"
#include "leds.h"
#include "thingset.h"

/* Devicetree */
#define CANBUS_NODE DT_CHOSEN(zephyr_canbus)
#define BMU_TIME_TRANS_MS   1000
#define CONFIG_SAMPLE_CAN_TX_QUEUE_SIZE 3
#define FRAME_ID            0

LOG_MODULE_REGISTER(bms_main, CONFIG_LOG_DEFAULT_LEVEL);
Bms bms;

const struct device *dev = DEVICE_DT_GET(CANBUS_NODE);
struct k_sem tx_queue_sem;
int err;

/* CAN frame to be sent */
/* CAN frame to be sent */
struct can_frame frame = {  FRAME_ID,					/*id*/
							0,							/*FD*/
							CAN_DATAFRAME,				/*RTR*/
							CAN_EXTENDED_IDENTIFIER,	/*id_type*/
							0,							/*dcl*/
							0,							/*brs*/
							7,							/*res*/
							0,							/*res0*/
							0,							/*res1*/
							{0x0000,0x0000}
};



static void can_tx_callback(const struct device *dev, int error, void *user_data)
{
	struct k_sem* tx_queue_sem = (k_sem*) user_data;

	k_sem_give(tx_queue_sem);
}

void can_trans_timer_handler(struct k_timer *timer_info) {
    if (k_sem_take(&tx_queue_sem, K_MSEC(1)) == 0) {		// frame has been sent

        frame.id = FRAME_ID;
        frame.data_32[0] = bms.status.pack_voltage;
        frame.data_32[1] = bms.status.pack_current;
        if (can_send(dev, &frame, K_NO_WAIT, NULL, NULL) != 0) {
            printk("failed to enqueue 1st CAN frame (err %d)\n", err);
        }

        frame.id = FRAME_ID + 16;
        frame.data_32[0] = bms.status.soc;
        frame.data_32[1] = bms.status.bat_temp_avg;
        if (can_send(dev, &frame, K_NO_WAIT, NULL, NULL) != 0) {
            printk("failed to enqueue 2nd CAN frame (err %d)\n", err);
        }

        frame.id = FRAME_ID + 32;
        frame.data_32[0] = bms.status.error_flags;
        frame.data_32[1] = bms.status.balancing_status;
        if (can_send(dev, &frame, K_NO_WAIT, can_tx_callback, &tx_queue_sem) != 0) {
            printk("failed to enqueue 3rd CAN frame (err %d)\n", err);
        }
    }
    else {					// frame has not been sent
        
    }    
}

K_TIMER_DEFINE(can_trans_timer, can_trans_timer_handler, NULL);




int main(void)
{

    k_sem_init(&tx_queue_sem, CONFIG_SAMPLE_CAN_TX_QUEUE_SIZE, CONFIG_SAMPLE_CAN_TX_QUEUE_SIZE);

    if (!device_is_ready(dev)) {
		printk("CAN device not ready");
		return 0;
	}
	err = can_start(dev);
	if (err != 0) {
		printk("Error starting CAN controller [%d]", err);
		return 0;
	}


    k_timer_start(&can_trans_timer, K_MSEC(BMU_TIME_TRANS_MS), K_MSEC(BMU_TIME_TRANS_MS));


    printf("Hardware:  Libre Solar %s (%s)\n", DT_PROP(DT_PATH(pcb), type),
           DT_PROP(DT_PATH(pcb), version_str));
    printf("Firmware: %s\n", FIRMWARE_VERSION_ID);

    while (bms_init_hardware(&bms) != 0) {
        LOG_ERR("BMS hardware initialization failed, retrying in 10s");
        k_sleep(K_MSEC(10000));
    }

    bms_configure(&bms);

    bms_update(&bms);
    bms_soc_reset(&bms, -1);

    button_init();

    int64_t t_start = k_uptime_get();
    while (true) {

        bms_update(&bms);
        bms_state_machine(&bms);

        if (button_pressed_for_3s()) {
            printf("Button pressed for 3s: shutdown...\n");
            bms_shutdown(&bms);
            k_sleep(K_MSEC(10000));
        }

        // bms_print_registers();

        t_start += 100;
        k_sleep(K_TIMEOUT_ABS_MS(t_start));
    }

    return 0;
}

static int init_config(const struct device *dev)
{
    bms_init_status(&bms);
    bms_init_config(&bms, CONFIG_CELL_TYPE, CONFIG_BAT_CAPACITY_AH);

    return 0;
}

/*
 * The default configuration must be initialized before the ThingSet storage backend reads data
 * from EEPROM or flash (with THINGSET_INIT_PRIORITY_STORAGE = 30).
 */
SYS_INIT(init_config, APPLICATION, 0);

#endif /* UNIT_TEST */
