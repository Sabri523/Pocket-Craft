#pragma once

// ---- WiFi credentials -------------------------------------------------
#define WIFI_SSID       "Home"
#define WIFI_PASS       "ab834261"
#define WIFI_MAXIMUM_RETRY 10

// ---- Minecraft server identity -----------------------------------------
// Protocol version 754 = Minecraft 1.16.5 (also covers 1.16.4)
#define MC_PROTOCOL_VERSION 754
#define MC_VERSION_NAME     "1.16.5"
#define MC_MAX_PLAYERS       4
#define MC_ONLINE_PLAYERS    0
#define MC_MOTD              "ESP32 Minecraft Server (status-only demo)"

// Message shown to a client that tries to actually join, since
// this firmware only implements the status/ping handshake so far.
#define MC_KICK_MESSAGE "This server only supports the status screen right now."

// ---- Networking ----------------------------------------------------------
#define MC_SERVER_PORT 25565
// Status/Login-state packets are small (handshake, status request/response,
// ping/pong, login start, disconnect reason). 512 bytes is generous headroom
// while keeping per-connection stack usage low.
#define MC_MAX_PACKET_SIZE 512
