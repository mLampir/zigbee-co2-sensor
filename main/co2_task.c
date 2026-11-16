#include <inttypes.h>
#include "esp_check.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_attr.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "zigbee.h"
#include "scd4x_i2c.h"
#include "sensirion_i2c_hal.h"
#include "config.h"
#include "tasks.h"


static const char *TAG = "sdc41_task";

// Diagnostic counters. RTC_NOINIT keeps them across software resets (watchdog,
// panic, esp_restart) so a restart can be counted; they are cleared on power-on.
#define DIAGNOSTICS_MAGIC 0x434f3221
static RTC_NOINIT_ATTR uint32_t diagnostics_magic;
static RTC_NOINIT_ATTR uint32_t error_count;
static RTC_NOINIT_ATTR uint32_t restart_count;
// Set by the Zigbee task once the counters have reporting info (see zigbee_task.c)
bool diagnostics_reporting_ok = false;

void diagnostics_init(void)
{
    esp_reset_reason_t reason = esp_reset_reason();
    if (reason == ESP_RST_POWERON || diagnostics_magic != DIAGNOSTICS_MAGIC) {
        diagnostics_magic = DIAGNOSTICS_MAGIC;
        error_count = 0;
        restart_count = 0;
    }
    switch (reason) {
        case ESP_RST_SW:
        case ESP_RST_PANIC:
        case ESP_RST_INT_WDT:
        case ESP_RST_TASK_WDT:
        case ESP_RST_WDT:
        case ESP_RST_BROWNOUT:
            restart_count++;
            break;
        default:
            break;
    }
    ESP_LOGI(TAG, "Reset reason %d, errors %" PRIu32 ", restarts %" PRIu32, reason, error_count, restart_count);
}

static void report_diagnostics(void)
{
    ESP_LOGI(TAG, "Diagnostics: errors %" PRIu32 ", restarts %" PRIu32, error_count, restart_count);
    if (!diagnostics_reporting_ok) {
        return;
    }
    float_t errors = error_count;
    float_t restarts = restart_count;
    reportAttribute(HA_ESP_ERRORS_ENDPOINT, ESP_ZB_ZCL_CLUSTER_ID_ANALOG_INPUT, ESP_ZB_ZCL_ATTR_ANALOG_INPUT_PRESENT_VALUE_ID, &errors);
    reportAttribute(HA_ESP_RESTARTS_ENDPOINT, ESP_ZB_ZCL_CLUSTER_ID_ANALOG_INPUT, ESP_ZB_ZCL_ATTR_ANALOG_INPUT_PRESENT_VALUE_ID, &restarts);
}

static void reset_sensor(void)
{
    // Clean up potential SCD4x states (e.g. a periodic measurement left running).
    // wake_up is never acknowledged by the SCD41, so errors here are expected.
    ESP_LOGI(TAG, "Resetting SCD4x sensor state, some i2c errors are expected");
    scd4x_wake_up();
    scd4x_stop_periodic_measurement();
    scd4x_reinit();

    uint16_t serial_0;
    uint16_t serial_1;
    uint16_t serial_2;
    int16_t error = scd4x_get_serial_number(&serial_0, &serial_1, &serial_2);
    if (error) {
        ESP_LOGE(TAG, "Error executing scd4x_get_serial_number(): %i", error);
    } else {
        ESP_LOGI(TAG, "serial: 0x%04x%04x%04x", serial_0, serial_1, serial_2);
    }
}

// One single shot measurement. Returns false on any error or timeout.
static bool measure(uint16_t *co2, int32_t *temperature, int32_t *humidity)
{
    // Blocks for the 5 s the SCD4x needs for a single shot measurement.
    int16_t error = scd4x_measure_single_shot();
    if (error) {
        ESP_LOGE(TAG, "Error executing scd4x_measure_single_shot(): %i", error);
        return false;
    }
    bool data_ready = false;
    for (int i = 0; i < DATA_READY_TRIES && !data_ready; i++) {
        error = scd4x_get_data_ready_flag(&data_ready);
        if (error) {
            ESP_LOGE(TAG, "Error executing scd4x_get_data_ready_flag(): %i", error);
            return false;
        }
        if (!data_ready) {
            vTaskDelay(pdMS_TO_TICKS(DATA_READY_POLL_MS));
        }
    }
    if (!data_ready) {
        ESP_LOGE(TAG, "SCD4x measurement did not finish in time");
        return false;
    }
    error = scd4x_read_measurement(co2, temperature, humidity);
    if (error) {
        ESP_LOGE(TAG, "Error executing scd4x_read_measurement(): %i", error);
        return false;
    }
    return true;
}

// Sends all plausible values; returns false if any value was implausible.
static bool report_measurement(uint16_t co2, int32_t temperature, int32_t humidity)
{
    bool plausible = true;
    int16_t zb_temperature = temperature / 10;
    int16_t zb_humidity = humidity / 10;
    float_t zb_co2 = (float_t)co2 / 1000000.0f;
    float_t sanity_temperature = zb_temperature / 100.0;
    float_t sanity_humidity = zb_humidity / 100.0;
    ESP_LOGI(TAG, "Hum: %.1f %%; Tmp: %.1f °C; CO2: %d ppm", sanity_humidity, sanity_temperature, co2);
    if (sanity_temperature >= TEMP_MIN && sanity_temperature <= TEMP_MAX) {
        reportAttribute(HA_ESP_CO2_ENDPOINT, ESP_ZB_ZCL_CLUSTER_ID_TEMP_MEASUREMENT, ESP_ZB_ZCL_ATTR_TEMP_MEASUREMENT_VALUE_ID, &zb_temperature);
    } else {
        ESP_LOGW(TAG, "Temperature value is implausible and no Zigbee update will be sent");
        plausible = false;
    }
    if (sanity_humidity >= HUMID_MIN && sanity_humidity <= HUMID_MAX) {
        reportAttribute(HA_ESP_CO2_ENDPOINT, ESP_ZB_ZCL_CLUSTER_ID_REL_HUMIDITY_MEASUREMENT, ESP_ZB_ZCL_ATTR_REL_HUMIDITY_MEASUREMENT_VALUE_ID, &zb_humidity);
    } else {
        ESP_LOGW(TAG, "Humidity value is implausible and no Zigbee update will be sent");
        plausible = false;
    }
    if (co2 >= CO2_MIN && co2 <= CO2_MAX) {
        reportAttribute(HA_ESP_CO2_ENDPOINT, ESP_ZB_ZCL_CLUSTER_ID_CARBON_DIOXIDE_MEASUREMENT, ESP_ZB_ZCL_ATTR_CARBON_DIOXIDE_MEASUREMENT_MEASURED_VALUE_ID, &zb_co2);
    } else {
        ESP_LOGW(TAG, "CO2 value is implausible and no Zigbee update will be sent");
        plausible = false;
    }
    return plausible;
}

// Diagnosis of the I2C wiring: release the line (open drain, no internal
// pull-up) and read it back, then drive it low and read it back.
// Released = 1: the pull-up of the sensor board reaches the line.
// Driven low = 0: the ESP pin can pull the line down.
static void pin_selftest(gpio_num_t pin, const char *name)
{
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << pin,
        .mode = GPIO_MODE_INPUT_OUTPUT_OD,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);
    gpio_set_level(pin, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
    int released = gpio_get_level(pin);
    gpio_set_level(pin, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    int driven_low = gpio_get_level(pin);
    gpio_set_level(pin, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_LOGW(TAG, "Pin test %s (GPIO%d): released=%d driven_low=%d (ok: 1/0)", name, pin, released, driven_low);
    gpio_reset_pin(pin);
}

void sdc41_task(void *pvParameters)
{
    pin_selftest(SDC4X_SDA_PIN, "SDA");
    pin_selftest(SDC4X_SCL_PIN, "SCL");
#if I2C_WIRING_DIAG
    gpio_config_t diag = {
        .pin_bit_mask = (1ULL << SDC4X_SDA_PIN) | (1ULL << SDC4X_SCL_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&diag);
    while (1) {
        ESP_LOGW(TAG, "Wiring diag: SDA (GPIO%d) = %d, SCL (GPIO%d) = %d (no internal pull-ups)",
                 SDC4X_SDA_PIN, gpio_get_level(SDC4X_SDA_PIN), SDC4X_SCL_PIN, gpio_get_level(SDC4X_SCL_PIN));
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
#endif
    sensirion_i2c_hal_init(SDC4X_SDA_PIN, SDC4X_SCL_PIN);
    // Always reset the sensor on start: after a watchdog restart it may be in any state.
    reset_sensor();
    report_diagnostics();

    uint16_t co2;
    int32_t temperature;
    int32_t humidity;
    int64_t last_success_us = esp_timer_get_time();

    while (1)
    {
        ESP_LOGI(TAG, "Triggering SCD4x single shot measurement...");
        xTaskNotify(xZigbeeTask, CO2_MEASUREMENT_PENDING, eSetValueWithOverwrite);
        if (measure(&co2, &temperature, &humidity) && report_measurement(co2, temperature, humidity)) {
            last_success_us = esp_timer_get_time();
            report_diagnostics();
            xTaskNotify(xZigbeeTask, CO2_MEASUREMENT_DONE, eSetValueWithOverwrite);
            vTaskDelay(pdMS_TO_TICKS(MEASURE_INTERVAL_MS));
            continue;
        }

        error_count++;
        report_diagnostics();
        if (esp_timer_get_time() - last_success_us > (int64_t)NO_MEASUREMENT_RESTART_MS * 1000) {
            ESP_LOGE(TAG, "No valid measurement for %d minutes, restarting", NO_MEASUREMENT_RESTART_MS / 60000);
            esp_restart();
        }
        vTaskDelay(pdMS_TO_TICKS(RETRY_DELAY_MS));
    }
}
