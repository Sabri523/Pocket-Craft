#pragma once

// Initializes NVS, netif, and the WiFi driver, then connects in station
// mode using the credentials in config.h. Blocks until an IP address has
// been obtained (or retries are exhausted, in which case it aborts).
void wifi_init_and_connect(void);
