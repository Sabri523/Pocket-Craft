#include "wifi.h"
#include "config.h"

#include <string.h>
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

static const char *TAG = "wifi";

static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static int s_retry_num = 0;

static void event_handler(void *arg, esp_event_base_t event_base,
                           int32_t event_id, void *event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) { // if wifi started
        esp_wifi_connect(); // wifi connect
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) { // if wifi disconnected
        if (s_retry_num < WIFI_MAXIMUM_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "Retrying WiFi connection (%d/%d)...", s_retry_num, WIFI_MAXIMUM_RETRY);
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT); // changes bit form 00 to 10, where 10 defined events start
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data; //dhcp succeeded
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

void wifi_init_and_connect(void) {
    esp_err_t ret = nvs_flash_init(); // data type for error, initialise nvs
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) { // if err then abort()
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init()); // initialise network interface
    ESP_ERROR_CHECK(esp_event_loop_create_default()); // create event loop
    esp_netif_create_default_wifi_sta(); // create wifi station

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT(); // load wifi default settings
    ESP_ERROR_CHECK(esp_wifi_init(&cfg)); // pass default config to wifi initialise

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Connecting to SSID: %s", WIFI_SSID);

    EventBits_t bits = xEventGroupWaitBits(
        s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        pdFALSE, pdFALSE, portMAX_DELAY); // in s_wifi event group, if bit is set, then do not clear bits on wake (pdFALSE), wake if any bits become set (pdFalse), (wait forever,, portmax delay) 

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Connected to AP: %s", WIFI_SSID);
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGE(TAG, "Failed to connect to AP: %s. Check credentials in config.h", WIFI_SSID);
        // Spin forever; flashing again after fixing config.h is required.
        while (1) {
            vTaskDelay(pdMS_TO_TICKS(5000));
        }
    }
}
/*
wifi_init_and_connect()
        │
        ▼
Initialize NVS
        │
        ▼
Initialize TCP/IP stack
        │
        ▼
Initialize WiFi driver
        │
        ▼
Register event handler
        │
        ▼
Start WiFi
        │
        ▼
WIFI_EVENT_STA_START
        │
        ▼
esp_wifi_connect()
        │
        ▼
Connect succeeds?
   ┌────┴────┐
   │         │
 Yes        No
   │         │
   ▼         ▼
Got IP   Retry up to N times
   │         │
   ▼         ▼
Set       Set FAIL bit
CONNECTED
bit
   │
   ▼
xEventGroupWaitBits()
returns
   │
   ▼
Application continues
*/
