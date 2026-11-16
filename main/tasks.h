#pragma once

#include <stdbool.h>

typedef enum {
    CO2_MEASUREMENT_PENDING,
    CO2_MEASUREMENT_DONE
} SensorState;

// globally tracked task handles
extern TaskHandle_t xSdc41Task;
extern TaskHandle_t xZigbeeTask;

void sdc41_task(void *pvParameters);
void diagnostics_init(void);
extern bool diagnostics_reporting_ok;
void esp_zb_task(void *pvParameters);