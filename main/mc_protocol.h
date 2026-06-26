#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <sys/socket.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "nbt_data.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

struct ClientInformation {
    // join game
    char UUID[16];
    char username[16];
    int32_t entityID;
    bool isHardcore;
    uint8_t gamemode;
    int8_t previousGamemode;
    char worldName[16];
    int64_t hashedSeed;
    uint32_t maxPlayers;
    uint32_t viewDistance;
    bool reducedDebugInfo;
    bool enableRespawnScreen;
    bool isDebug;
    bool isFlat;

    //player position and look

    double x;
    double y;
    double z;
    float yaw;
    float pitch;
    uint8_t flags;
    int teleportID;

    int chunkX;
    int chunkZ;
};
// Handles a single client connection end-to-end (handshake, status, ping,
// and a graceful kick if they attempt to log in). Blocks until the client
// disconnects or an unrecoverable protocol error occurs. Closes `sock`
// before returning.
void mc_handle_connection(int sock);
