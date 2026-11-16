#pragma once

#include <stdint.h>
#include "driver/gpio.h"


#define SDC4X_SDA_PIN GPIO_NUM_22
#define SDC4X_SCL_PIN  GPIO_NUM_23
// Recommened measurement interval from sensirion is 5 minutes as the default ASC settings are assuming 5 minutes
#define MEASURE_INTERVAL_MS (60 * 5 * 1000)
// After the 5 s single shot wait, poll the data ready flag at most this long
#define DATA_READY_TRIES 50
#define DATA_READY_POLL_MS 100
// Wait between attempts after a failed measurement
#define RETRY_DELAY_MS (10 * 1000)
// Restart the ESP if no valid measurement succeeded for this long
#define NO_MEASUREMENT_RESTART_MS (15 * 60 * 1000)


// Sanity values for measurement bounds
#define TEMP_MIN -10
#define TEMP_MAX 60

#define HUMID_MIN 0
#define HUMID_MAX 95

#define CO2_MIN 0.0
#define CO2_MAX 5000.0

// Light sleep between measurements. Off: the sensor runs on USB power anyway,
// and I2C to the SCD4x failed again and again after sleeping.
#define ENABLE_LIGHT_SLEEP 0

// Misc settings, for very fine tuning
// time in ms before the zigbee stack sends a CAN_SLEEP signal
#define ZIGBEE_SLEEP_THRESHOLD 20
