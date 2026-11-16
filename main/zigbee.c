#include "esp_zigbee_core.h"
#include "esp_check.h"
#include "esp_log.h"
#include "string.h"

static const char *TAG = "zigbee";

// Sets an attribute and reports it to the coordinator. Called from the sensor
// task, so the Zigbee lock is mandatory (the stack is not thread safe).
bool reportAttribute(uint8_t endpoint, uint16_t clusterID, uint16_t attributeID, void *value)
{
    esp_zb_zcl_report_attr_cmd_t cmd = {
        .zcl_basic_cmd = {
            .dst_addr_u.addr_short = 0x0000,
            .dst_endpoint = endpoint,
            .src_endpoint = endpoint,
        },
        .address_mode = ESP_ZB_APS_ADDR_MODE_16_ENDP_PRESENT,
        .clusterID = clusterID,
        .attributeID = attributeID,
        .cluster_role = ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
    };
    if (!esp_zb_lock_acquire(portMAX_DELAY)) {
        ESP_LOGW(TAG, "Could not acquire Zigbee lock");
        return false;
    }
    esp_err_t err = ESP_FAIL;
    esp_zb_zcl_status_t status = esp_zb_zcl_set_attribute_val(endpoint, clusterID, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE, attributeID, value, false);
    if (status == ESP_ZB_ZCL_STATUS_SUCCESS) {
        err = esp_zb_zcl_report_attr_cmd_req(&cmd);
    }
    esp_zb_lock_release();
    if (status != ESP_ZB_ZCL_STATUS_SUCCESS || err != ESP_OK) {
        // Not fatal: the next measurement sends the value again.
        ESP_LOGW(TAG, "Report of cluster 0x%04x attribute 0x%04x failed (status %d, %s)",
                 clusterID, attributeID, status, esp_err_to_name(err));
        return false;
    }
    return true;
}
