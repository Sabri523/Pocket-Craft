#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "config.h"
#include "wifi.h"
#include "mc_protocol.h"

static const char *TAG = "mc_server";

// Each accepted client gets its own FreeRTOS task running this function.
// Minecraft status pings are short-lived (connect, query, disconnect),
// so a stack size of 4096 words is comfortable headroom.
static void client_task(void *pvParameters) {
    int sock = (int)(intptr_t)pvParameters; //intptr_t is an automatic scaling int that scales to int32 on 32 bit systems and int64 on 64bit.
    mc_handle_connection(sock); // closes sock internally before returning
    vTaskDelete(NULL);
}

static void server_task(void *pvParameters) { // pvParameters is a value used by freeRTOS
    int listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP); // socket() initialises socket with TCP, where return < 0 in error and > 0 at proper initialisation
    if (listen_sock < 0) { // if socket fails
        ESP_LOGE(TAG, "Failed to create socket"); //prints to idf.py monitor
        vTaskDelete(NULL); // deletes task (server_task)
        return;
    }

    int opt = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)); // defines socket options to 

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = htonl(INADDR_ANY), // listens on any network interfaces - host to network long
        .sin_port = htons(MC_SERVER_PORT), //sets listening port to 25565 - host to network short
    };

    if (bind(listen_sock, (struct sockaddr *)&addr, sizeof(addr)) != 0) { // binds socket to address returns 0 only if successful
        ESP_LOGE(TAG, "Bind failed on port %d", MC_SERVER_PORT);
        close(listen_sock);
        vTaskDelete(NULL);
        return;
    }

    if (listen(listen_sock, 4) != 0) { // turns the bound socket into a listening socket with upto four pending connections
        ESP_LOGE(TAG, "Listen failed");
        close(listen_sock);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Minecraft status server listening on port %d", MC_SERVER_PORT);

    while (1) { // server runs forever
        struct sockaddr_in client_addr; // initialise a struct for client addr
        socklen_t client_addr_len = sizeof(client_addr); // number of bytes of addr required by accept()
        int client_sock = accept(listen_sock, (struct sockaddr *)&client_addr, &client_addr_len); //accepts connection - sockaddr_in being cast to sockaddr is to convert ipv4 specific struct into a general one.

        if (client_sock < 0) { //if accept() returns <0 then error
            ESP_LOGW(TAG, "accept() failed");
            continue;
        }

        char ip_str[16];
        inet_ntoa_r(client_addr.sin_addr, ip_str, sizeof(ip_str)); // internet address _ network byte ordered binary structure to an ASCII _re-entrant
                                                                   // inet_ntoa is unsafe for multithreading
        ESP_LOGI(TAG, "Client connected: %s", ip_str);

        // Hand off to a dedicated task so a slow/stuck client can't block
        // new connections from being accepted.
        BaseType_t res = xTaskCreate(
            client_task, "mc_client", 6144, // runs client_task with 6144 bytes of alloacted heap
            (void *)(intptr_t)client_sock, 5, NULL); // 5 is priority, NULL is no task handle

        if (res != pdPASS) { // on task creation failure
            ESP_LOGE(TAG, "Failed to spawn client task, dropping connection");
            close(client_sock);
        }
    }
}

void app_main(void) {
    ESP_LOGI(TAG, "Starting Minecraft %s status server (protocol %d)",
             MC_VERSION_NAME, MC_PROTOCOL_VERSION);

    wifi_init_and_connect();

    xTaskCreate(server_task, "mc_server", 4096, NULL, 5, NULL);
}
