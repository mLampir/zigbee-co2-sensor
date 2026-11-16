#include <string.h>
#include "nvs_flash.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ha/esp_zigbee_ha_standard.h"
#include "zigbee.h"
#include "tasks.h"
#include "esp_pm.h"
#include "esp_private/esp_clk.h"
#include "esp_sleep.h"
#include "config.h"
#include "tasks.h"

#define DEFINE_PSTRING(var, str)   \
    const struct                   \
    {                              \
        unsigned char len;         \
        char content[sizeof(str)]; \
    }(var) = {sizeof(str) - 1, (str)}


static const char *TAG = "zigbee_task";

static esp_err_t zb_attribute_handler(const esp_zb_zcl_set_attr_value_message_t *message)
{
    esp_err_t ret = ESP_OK;
    ESP_RETURN_ON_FALSE(message, ESP_FAIL, TAG, "Empty message");
    ESP_RETURN_ON_FALSE(message->info.status == ESP_ZB_ZCL_STATUS_SUCCESS, ESP_ERR_INVALID_ARG, TAG, "Received message: error status(%d)",
                        message->info.status);
    ESP_LOGI(TAG, "Received message: endpoint(0x%x), cluster(0x%x), attribute(0x%x), data size(%d)", message->info.dst_endpoint, message->info.cluster,
             message->attribute.id, message->attribute.data.size);
    return ret;
}

static esp_err_t zb_action_handler(esp_zb_core_action_callback_id_t callback_id, const void *message)
{
    esp_err_t ret = ESP_OK;
    switch (callback_id)
    {
    case ESP_ZB_CORE_SET_ATTR_VALUE_CB_ID:
        ret = zb_attribute_handler((esp_zb_zcl_set_attr_value_message_t *)message);
        break;
    default:
        ESP_LOGW(TAG, "Receive Zigbee action(0x%x) callback", callback_id);
        break;
    }
    return ret;
}

/* initialize Zigbee stack with Zigbee end-device config */
void esp_zb_task(void *pvParameters)
{
    /* initialize Zigbee stack with Zigbee end-device config */
    esp_zb_cfg_t zb_nwk_cfg = ESP_ZB_ZED_CONFIG();
    /* Enable zigbee light sleep */
    esp_zb_sleep_enable(true);
    esp_zb_sleep_set_threshold(ZIGBEE_SLEEP_THRESHOLD);
    // zb_zdo_pim_set_long_poll_interval(ED_KEEP_ALIVE);
    esp_zb_init(&zb_nwk_cfg);

    // ------------------------------ Cluster BASIC ------------------------------
    esp_zb_basic_cluster_cfg_t basic_cluster_cfg = {
        .zcl_version = ESP_ZB_ZCL_BASIC_ZCL_VERSION_DEFAULT_VALUE,
        .power_source = 0x04, // mains powered
    };
    uint32_t ApplicationVersion = 0x0003;
    uint32_t StackVersion = 0x0002;
    uint32_t HWVersion = 0x0002;
    DEFINE_PSTRING(ManufacturerName, "FlorianL");
    DEFINE_PSTRING(ModelIdentifier, "CO2 Sensor");
    DEFINE_PSTRING(DateCode, "20261116");

    esp_zb_attribute_list_t *esp_zb_basic_cluster = esp_zb_basic_cluster_create(&basic_cluster_cfg);
    esp_zb_basic_cluster_add_attr(esp_zb_basic_cluster, ESP_ZB_ZCL_ATTR_BASIC_APPLICATION_VERSION_ID, &ApplicationVersion);
    esp_zb_basic_cluster_add_attr(esp_zb_basic_cluster, ESP_ZB_ZCL_ATTR_BASIC_STACK_VERSION_ID, &StackVersion);
    esp_zb_basic_cluster_add_attr(esp_zb_basic_cluster, ESP_ZB_ZCL_ATTR_BASIC_HW_VERSION_ID, &HWVersion);
    esp_zb_basic_cluster_add_attr(esp_zb_basic_cluster, ESP_ZB_ZCL_ATTR_BASIC_MANUFACTURER_NAME_ID, (void *)&ManufacturerName);
    esp_zb_basic_cluster_add_attr(esp_zb_basic_cluster, ESP_ZB_ZCL_ATTR_BASIC_MODEL_IDENTIFIER_ID, (void *)&ModelIdentifier);
    esp_zb_basic_cluster_add_attr(esp_zb_basic_cluster, ESP_ZB_ZCL_ATTR_BASIC_DATE_CODE_ID, (void *)&DateCode);

    // ------------------------------ Cluster IDENTIFY ------------------------------
    esp_zb_identify_cluster_cfg_t identify_cluster_cfg = {
        .identify_time = 0,
    };
    esp_zb_attribute_list_t *esp_zb_identify_cluster = esp_zb_identify_cluster_create(&identify_cluster_cfg);

    // ------------------------------ Cluster Temperature ------------------------------
    esp_zb_temperature_meas_cluster_cfg_t temperature_meas_cfg = {
        .measured_value = 0,
        // ZCL units: 0.01 °C. Values outside min/max are rejected (INVALID_VALUE)
        .min_value = TEMP_MIN * 100,
        .max_value = TEMP_MAX * 100,
    };
    esp_zb_attribute_list_t *esp_zb_temperature_meas_cluster = esp_zb_temperature_meas_cluster_create(&temperature_meas_cfg);

    // ------------------------------ Cluster Humidity ------------------------------
    esp_zb_humidity_meas_cluster_cfg_t humidity_meas_cfg = {
        .measured_value = 0,
        // ZCL units: 0.01 %
        .min_value = HUMID_MIN * 100,
        .max_value = HUMID_MAX * 100,
    };
    esp_zb_attribute_list_t *esp_zb_humidity_meas_cluster = esp_zb_humidity_meas_cluster_create(&humidity_meas_cfg);

    // ------------------------------ Cluster CO2 -----------------------------------
    esp_zb_carbon_dioxide_measurement_cluster_cfg_t carbon_dioxide_meas_cfg = {
        .measured_value = 0,
        // ZCL units: fraction (ppm / 1e6)
        .min_measured_value = CO2_MIN / 1000000.0f,
        .max_measured_value = CO2_MAX / 1000000.0f,
    };
    esp_zb_attribute_list_t *esp_zb_carbon_dioxide_meas_cluster = esp_zb_carbon_dioxide_measurement_cluster_create(&carbon_dioxide_meas_cfg);

    // ------------------------------ Create cluster list ------------------------------
    esp_zb_cluster_list_t *esp_zb_cluster_list = esp_zb_zcl_cluster_list_create();
    esp_zb_cluster_list_add_basic_cluster(esp_zb_cluster_list, esp_zb_basic_cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
    esp_zb_cluster_list_add_identify_cluster(esp_zb_cluster_list, esp_zb_identify_cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
    esp_zb_cluster_list_add_temperature_meas_cluster(esp_zb_cluster_list, esp_zb_temperature_meas_cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
    esp_zb_cluster_list_add_humidity_meas_cluster(esp_zb_cluster_list, esp_zb_humidity_meas_cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
    esp_zb_cluster_list_add_carbon_dioxide_measurement_cluster(esp_zb_cluster_list, esp_zb_carbon_dioxide_meas_cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

    // ------------------------------ Create endpoint list ------------------------------
    esp_zb_ep_list_t *esp_zb_ep_list = esp_zb_ep_list_create();
    esp_zb_endpoint_config_t endpoint_config = {
        .endpoint = HA_ESP_CO2_ENDPOINT,
        .app_profile_id = ESP_ZB_AF_HA_PROFILE_ID,
        .app_device_id = ESP_ZB_HA_ON_OFF_LIGHT_DEVICE_ID,
    };

    esp_zb_ep_list_add_ep(esp_zb_ep_list, esp_zb_cluster_list, endpoint_config);

    // ------------------------------ Diagnostic counters ------------------------------
    // One analog input per endpoint: failed measurements and restarts since power-on.
    const uint8_t counter_endpoints[] = {HA_ESP_ERRORS_ENDPOINT, HA_ESP_RESTARTS_ENDPOINT};
    for (size_t i = 0; i < sizeof(counter_endpoints); i++) {
        esp_zb_analog_input_cluster_cfg_t analog_input_cfg = {
            .out_of_service = false,
            .present_value = 0,
            .status_flags = 0,
        };
        esp_zb_cluster_list_t *counter_clusters = esp_zb_zcl_cluster_list_create();
        esp_zb_cluster_list_add_analog_input_cluster(counter_clusters, esp_zb_analog_input_cluster_create(&analog_input_cfg), ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
        esp_zb_endpoint_config_t counter_config = {
            .endpoint = counter_endpoints[i],
            .app_profile_id = ESP_ZB_AF_HA_PROFILE_ID,
            .app_device_id = ESP_ZB_HA_SIMPLE_SENSOR_DEVICE_ID,
        };
        esp_zb_ep_list_add_ep(esp_zb_ep_list, counter_clusters, counter_config);
    }

    // ------------------------------ Register Device ------------------------------
    esp_zb_device_register(esp_zb_ep_list);

    // esp_zb_zcl_report_attr_cmd_req() asserts (esp_zigbee_zcl_command.c:325) if
    // the endpoint has no reporting info for the attribute. The library creates
    // it for the measurement clusters, not for Analog Input, so add it here
    // (no periodic reports, huge delta: the firmware reports by itself).
    // Reporting the counters still hit the assertion above with reporting info
    // in place (esp-zigbee-lib 1.5.1), so they are only counted and logged for now.
    diagnostics_reporting_ok = false;
    for (size_t i = 0; i < sizeof(counter_endpoints); i++) {
        esp_zb_zcl_reporting_info_t reporting_info = {
            .direction = ESP_ZB_ZCL_CMD_DIRECTION_TO_SRV,
            .ep = counter_endpoints[i],
            .cluster_id = ESP_ZB_ZCL_CLUSTER_ID_ANALOG_INPUT,
            .cluster_role = ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
            .dst.profile_id = ESP_ZB_AF_HA_PROFILE_ID,
            .u.send_info.min_interval = 1,
            .u.send_info.max_interval = 0,
            .u.send_info.def_min_interval = 1,
            .u.send_info.def_max_interval = 0,
            .attr_id = ESP_ZB_ZCL_ATTR_ANALOG_INPUT_PRESENT_VALUE_ID,
            .manuf_code = ESP_ZB_ZCL_ATTR_NON_MANUFACTURER_SPECIFIC,
        };
        float_t delta = 1000000.0f;
        memcpy(reporting_info.u.send_info.delta.data_buf, &delta, sizeof(delta));
        esp_err_t err = esp_zb_zcl_update_reporting_info(&reporting_info);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "No reporting info for endpoint %d (%s)", counter_endpoints[i], esp_err_to_name(err));
        }
    }

    esp_zb_core_action_handler_register(zb_action_handler);
    esp_zb_set_primary_network_channel_set(ESP_ZB_PRIMARY_CHANNEL_MASK);

    ESP_ERROR_CHECK(esp_zb_start(false));
    esp_zb_stack_main_loop();
}