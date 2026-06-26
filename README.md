# ESP-IDF Minecraft 1.16.5 Status Server

A minimal, pure ESP-IDF (no Arduino) firmware that makes an ESP32 show up
in the Minecraft Java Edition multiplayer server list — with a working
MOTD, player count, version string, and accurate ping — and gracefully
kicks anyone who tries to actually join.

This is the **handshake + status + login-kick** layer only. It is a
starting point, not a replacement for projects like
[bareiron](https://github.com/p2r3/bareiron) or
[macerun](https://github.com/4ngel2769/macerun), which go on to implement
chunk generation, entities, inventories, and the full play state. Building
that out is a much larger undertaking — see "What's missing" below.

## What this firmware does

- Connects to WiFi in station mode
- Listens on TCP port 25565
- Parses the Minecraft protocol's VarInt-framed packets
- Implements the **Handshake** packet (protocol 754 / MC 1.16.5)
- Implements the **Status** state: Request → JSON Response, Ping → Pong
- Implements the **Login** state just enough to read the username and
  send a clean **Disconnect** packet with a JSON kick reason, so the
  client doesn't hang or show a garbled error

Each connection is handled on its own FreeRTOS task so one stuck/slow
client can't block new connections from being accepted.

## What's missing (by design — this is a foundation, not a full server)

- No encryption / compression (fine for offline-mode clients on status+kick)
- No actual Play state: no world, no chunks, no entities, no inventory
- No persistence
- No support for the legacy (pre-Netty) ping protocol some older clients
  use as a fallback

Getting from here to "you can actually walk around" means implementing the
Play state: chunk data packets, entity spawn/tracking, keepalives, and a
block/world model — that's the bulk of what bareiron and macerun do.

## Setup

1. Install ESP-IDF (v5.x recommended) and source `export.sh`/`export.ps1`
2. Edit `main/config.h`:
   - `WIFI_SSID` / `WIFI_PASS` — your network credentials
   - `MC_MOTD` — the message shown in the server list
   - `MC_MAX_PLAYERS` — cosmetic only; nobody can actually join yet
   - `MC_KICK_MESSAGE` — shown to anyone who tries to join
   - Avoid `"` or `\` characters in `MC_MOTD` / `MC_KICK_MESSAGE` — they're
     interpolated directly into JSON without escaping
3. Set your target chip and build:
   ```
   idf.py set-target esp32        # or esp32s3, esp32c3, etc.
   idf.py build
   idf.py -p /dev/ttyUSB0 flash monitor
   ```
4. Watch the serial monitor for the assigned IP address
5. In Minecraft Java Edition 1.16.5, add a server at `<that IP>:25565` —
   it should appear with your MOTD, player count, and a real ping. Joining
   will show your kick message instead of hanging.

## File layout

```
main/
  main.c          - app_main, TCP accept loop, per-client task spawn
  wifi.c / .h      - WiFi station connect (event-group based, with retry)
  varint.c / .h    - VarInt encode/decode, exact-read socket helper
  mc_protocol.c/.h - Handshake / Status / Login packet handling
  config.h         - all user-editable settings
```

## Next steps if you want to go further

In rough order of how bareiron-style projects tend to build it up:
1. Login success (offline-mode UUID) + switch to Play state
2. Join Game packet + a single hardcoded/flat chunk so the client doesn't
   fall through the void
3. Keepalive packets (required or the client times out and disconnects)
4. Player position/look handling so movement doesn't desync
5. Basic block get/set so the world feels real
6. Chunk generation, entities, inventory, crafting — the long tail

Happy to help implement any of these next; the Play state is a
significantly bigger lift than the status/login layer here, mostly because
of the chunk data packet format.
