/**
 * @file ble_tpms.c
 * @brief BLE TPMS (Tire Pressure Monitoring System) driver implementation
 *        Using NimBLE stack (lighter weight than Bluedroid)
 */

#include "ble_tpms.h"
#include "esp_log.h"
#include "esp_nimble_hci.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "BLE_TPMS";

// TPMS manufacturer data characteristics
#define TPMS_MANUF_DATA_LEN     18      // Expected manufacturer data length
#define TPMS_MANUF_ID_TOMTOM    0x0001  // Manufacturer ID for these sensors
#define TPMS_SENSOR_NUM_BASE    0x80    // First sensor is 0x80

// Scan duration and period
#define TPMS_SCAN_DURATION_SEC  5   // Scan for 5 seconds
#define TPMS_SCAN_PERIOD_SEC    30  // Then wait 30 seconds before next scan

// Registered sensor MAC addresses (configured by user)
static uint8_t registered_sensors[TPMS_POSITION_COUNT][6] = {0};
static bool sensor_registered[TPMS_POSITION_COUNT] = {false};

// Current sensor data
static tpms_sensor_data_t sensor_data[TPMS_POSITION_COUNT] = {0};

// Callback for updates
static tpms_update_callback_t update_callback = NULL;

// State
static bool initialized = false;
static bool scanning = false;
static bool scan_paused = false;  // True if scan was paused for SPI
static uint32_t last_scan_end_ms = 0;

// Forward declarations
static void process_tpms_data(const uint8_t *manuf_data, size_t len, int8_t rssi);
static tpms_position_t find_sensor_position(const uint8_t *mac_addr);
static int ble_gap_event(struct ble_gap_event *event, void *arg);
static void ble_host_task(void *param);
static void ble_on_sync(void);
static void ble_on_reset(int reason);

// BLE scan parameters - passive scan, less aggressive
static struct ble_gap_disc_params scan_params = {
    .itvl = 160,              // 100ms interval (160 * 0.625ms)
    .window = 80,             // 50ms window (80 * 0.625ms)
    .filter_policy = BLE_HCI_SCAN_FILT_NO_WL,
    .limited = 0,
    .passive = 1,             // Passive scan - don't send scan requests
    .filter_duplicates = 1,   // Filter duplicate advertisements
};

esp_err_t ble_tpms_init(void)
{
    if (initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing BLE TPMS (NimBLE)...");

    // Initialize NVS if not already done
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Initialize NimBLE
    ret = nimble_port_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NimBLE port init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Configure the host
    ble_hs_cfg.sync_cb = ble_on_sync;
    ble_hs_cfg.reset_cb = ble_on_reset;

    // Initialize sensor data structures
    for (int i = 0; i < TPMS_POSITION_COUNT; i++) {
        memset(&sensor_data[i], 0, sizeof(tpms_sensor_data_t));
        sensor_data[i].valid = false;
    }

    // Start the NimBLE host task with lower priority
    nimble_port_freertos_init(ble_host_task);

    initialized = true;
    ESP_LOGI(TAG, "BLE TPMS initialized successfully (NimBLE)");
    return ESP_OK;
}

static void ble_on_sync(void)
{
    ESP_LOGI(TAG, "BLE host synced, ready to scan");
    // Don't auto-start scan here - let the app call ble_tpms_start_scan()
}

static void ble_on_reset(int reason)
{
    ESP_LOGW(TAG, "BLE host reset, reason: %d", reason);
    scanning = false;
}

static void ble_host_task(void *param)
{
    ESP_LOGI(TAG, "NimBLE host task started");
    nimble_port_run();  // This function will return only when nimble_port_stop() is called
    nimble_port_freertos_deinit();
}

esp_err_t ble_tpms_start_scan(void)
{
    if (!initialized) {
        ESP_LOGE(TAG, "Not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (scanning) {
        ESP_LOGW(TAG, "Already scanning");
        return ESP_OK;
    }

    // Check if host is synced
    if (!ble_hs_synced()) {
        ESP_LOGW(TAG, "BLE host not yet synced, will retry later");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Starting TPMS scan...");

    // Start discovery (scanning)
    int rc = ble_gap_disc(BLE_OWN_ADDR_PUBLIC, TPMS_SCAN_DURATION_SEC * 1000,
                          &scan_params, ble_gap_event, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to start scan: %d", rc);
        return ESP_FAIL;
    }

    scanning = true;
    ESP_LOGI(TAG, "TPMS scan started (duration: %d sec)", TPMS_SCAN_DURATION_SEC);
    return ESP_OK;
}

esp_err_t ble_tpms_stop_scan(void)
{
    if (!scanning) {
        return ESP_OK;
    }

    int rc = ble_gap_disc_cancel();
    if (rc != 0 && rc != BLE_HS_EALREADY) {
        ESP_LOGE(TAG, "Failed to stop scan: %d", rc);
        return ESP_FAIL;
    }

    scanning = false;
    ESP_LOGI(TAG, "TPMS scan stopped");
    return ESP_OK;
}

void ble_tpms_periodic_update(void)
{
    if (!initialized) return;
    
    // If not scanning and enough time has passed, restart scan
    if (!scanning && ble_hs_synced()) {
        uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
        uint32_t elapsed = now - last_scan_end_ms;
        
        if (elapsed >= (TPMS_SCAN_PERIOD_SEC * 1000)) {
            ESP_LOGI(TAG, "Restarting periodic TPMS scan");
            ble_tpms_start_scan();
        }
    }
}

// GAP event handler
static int ble_gap_event(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
        case BLE_GAP_EVENT_DISC: {
            // Got an advertisement
            struct ble_gap_disc_desc *disc = &event->disc;
            
            // Look for manufacturer-specific data in the advertisement
            struct ble_hs_adv_fields fields;
            int rc = ble_hs_adv_parse_fields(&fields, disc->data, disc->length_data);
            if (rc != 0) {
                return 0;  // Ignore parse errors
            }
            
            // Check for manufacturer data
            if (fields.mfg_data != NULL && fields.mfg_data_len >= TPMS_MANUF_DATA_LEN) {
                process_tpms_data(fields.mfg_data, fields.mfg_data_len, disc->rssi);
            }
            return 0;
        }

        case BLE_GAP_EVENT_DISC_COMPLETE:
            ESP_LOGI(TAG, "Scan complete, next scan in %d sec", TPMS_SCAN_PERIOD_SEC);
            scanning = false;
            last_scan_end_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
            return 0;

        default:
            return 0;
    }
}

esp_err_t ble_tpms_register_sensor(tpms_position_t position, const uint8_t *mac_address)
{
    if (position >= TPMS_POSITION_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }
    if (mac_address == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(registered_sensors[position], mac_address, 6);
    sensor_registered[position] = true;

    ESP_LOGI(TAG, "Registered sensor %s: %02X:%02X:%02X:%02X:%02X:%02X",
             ble_tpms_position_str(position),
             mac_address[0], mac_address[1], mac_address[2],
             mac_address[3], mac_address[4], mac_address[5]);

    return ESP_OK;
}

esp_err_t ble_tpms_register_sensor_str(tpms_position_t position, const char *mac_str)
{
    if (mac_str == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t mac[6];
    int parsed = sscanf(mac_str, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
                        &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5]);
    
    if (parsed != 6) {
        ESP_LOGE(TAG, "Failed to parse MAC address: %s", mac_str);
        return ESP_ERR_INVALID_ARG;
    }

    return ble_tpms_register_sensor(position, mac);
}

esp_err_t ble_tpms_get_data(tpms_position_t position, tpms_sensor_data_t *data)
{
    if (position >= TPMS_POSITION_COUNT || data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!sensor_data[position].valid) {
        return ESP_ERR_NOT_FOUND;
    }

    memcpy(data, &sensor_data[position], sizeof(tpms_sensor_data_t));
    return ESP_OK;
}

void ble_tpms_get_all_data(tpms_sensor_data_t *data)
{
    if (data != NULL) {
        memcpy(data, sensor_data, sizeof(sensor_data));
    }
}

void ble_tpms_register_callback(tpms_update_callback_t callback)
{
    update_callback = callback;
}

bool ble_tpms_any_low_pressure(float threshold_bar)
{
    for (int i = 0; i < TPMS_POSITION_COUNT; i++) {
        if (sensor_data[i].valid && sensor_data[i].pressure_bar < threshold_bar) {
            return true;
        }
    }
    return false;
}

bool ble_tpms_is_scanning(void)
{
    return scanning;
}

const char* ble_tpms_position_str(tpms_position_t position)
{
    switch (position) {
        case TPMS_FRONT_LEFT:  return "FL";
        case TPMS_FRONT_RIGHT: return "FR";
        case TPMS_REAR_LEFT:   return "RL";
        case TPMS_REAR_RIGHT:  return "RR";
        default: return "??";
    }
}

void ble_tpms_pause_scan(void)
{
    if (!initialized || !scanning) return;
    
    int rc = ble_gap_disc_cancel();
    if (rc == 0 || rc == BLE_HS_EALREADY) {
        scan_paused = true;
        scanning = false;
    }
}

void ble_tpms_resume_scan(void)
{
    if (!initialized || !scan_paused) return;
    
    scan_paused = false;
    
    // Check if host is synced before resuming
    if (!ble_hs_synced()) return;
    
    // Resume scanning
    int rc = ble_gap_disc(BLE_OWN_ADDR_PUBLIC, TPMS_SCAN_DURATION_SEC * 1000,
                          &scan_params, ble_gap_event, NULL);
    if (rc == 0) {
        scanning = true;
    }
}

// Find which position this MAC address is registered to
static tpms_position_t find_sensor_position(const uint8_t *mac_addr)
{
    for (int i = 0; i < TPMS_POSITION_COUNT; i++) {
        if (sensor_registered[i] && memcmp(registered_sensors[i], mac_addr, 6) == 0) {
            return (tpms_position_t)i;
        }
    }
    return TPMS_POSITION_COUNT; // Not found
}

// Process TPMS manufacturer data
static void process_tpms_data(const uint8_t *manuf_data, size_t len, int8_t rssi)
{
    if (len < TPMS_MANUF_DATA_LEN) {
        return; // Not TPMS data
    }

    // Check manufacturer ID (bytes 0-1, little-endian)
    uint16_t manuf_id = manuf_data[0] | (manuf_data[1] << 8);
    if (manuf_id != TPMS_MANUF_ID_TOMTOM) {
        return; // Not a TPMS sensor we recognize
    }

    // Extract sensor MAC from advertisement (bytes 2-7)
    // Format: SensorNum(1) + EACA(2) + Address(3)
    uint8_t sensor_mac[6];
    sensor_mac[0] = manuf_data[2];  // Sensor number (80, 81, 82, 83)
    sensor_mac[1] = manuf_data[3];  // EA
    sensor_mac[2] = manuf_data[4];  // CA
    sensor_mac[3] = manuf_data[5];  // Address byte 1
    sensor_mac[4] = manuf_data[6];  // Address byte 2
    sensor_mac[5] = manuf_data[7];  // Address byte 3

    // Find which tire this sensor belongs to
    tpms_position_t position = find_sensor_position(sensor_mac);
    
    // If not registered, log it for discovery purposes
    if (position >= TPMS_POSITION_COUNT) {
        static uint32_t last_unknown_log = 0;
        uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
        if (now - last_unknown_log > 5000) { // Log every 5 seconds max
            ESP_LOGI(TAG, "Unknown TPMS sensor: %02X:%02X:%02X:%02X:%02X:%02X (RSSI: %d)",
                     sensor_mac[0], sensor_mac[1], sensor_mac[2],
                     sensor_mac[3], sensor_mac[4], sensor_mac[5], rssi);
            last_unknown_log = now;
        }
        return;
    }

    // Extract pressure (bytes 8-11, little-endian, value in Pa)
    uint32_t pressure_raw = manuf_data[8] | 
                            (manuf_data[9] << 8) | 
                            (manuf_data[10] << 16) | 
                            (manuf_data[11] << 24);
    
    // Extract temperature (bytes 12-15, little-endian, value in 0.01°C)
    int32_t temp_raw = (int32_t)(manuf_data[12] | 
                                 (manuf_data[13] << 8) | 
                                 (manuf_data[14] << 16) | 
                                 (manuf_data[15] << 24));
    
    // Extract battery and alarm
    uint8_t battery = manuf_data[16];
    uint8_t alarm = manuf_data[17];

    // Update sensor data
    tpms_sensor_data_t *data = &sensor_data[position];
    memcpy(data->mac_address, sensor_mac, 6);
    data->pressure_kpa = pressure_raw / 1000.0f;
    data->pressure_bar = pressure_raw / 100000.0f;
    data->pressure_psi = data->pressure_bar * 14.5038f;  // Convert bar to PSI
    data->temperature_c = temp_raw / 100.0f;
    data->battery_percent = battery;
    data->alarm = (tpms_alarm_t)alarm;
    data->rssi = rssi;
    data->last_update_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
    data->valid = true;

    ESP_LOGI(TAG, "%s: %.1f PSI (%.2f bar), %.1f°C, Batt: %d%%, RSSI: %d",
             ble_tpms_position_str(position),
             data->pressure_psi, data->pressure_bar,
             data->temperature_c, data->battery_percent, rssi);

    // Call update callback if registered
    if (update_callback != NULL) {
        update_callback(position, data);
    }
}
