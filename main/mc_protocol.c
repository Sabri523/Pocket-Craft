#include "mc_protocol.h"
#include "varint.h"
#include "config.h"

static const char *TAG = "mc_proto";

// Minecraft "next state" values sent in the Handshake packet.
#define NEXT_STATE_STATUS 1
#define NEXT_STATE_LOGIN  2

// --------------------------------------------------------------------
// Low-level packet I/O
// --------------------------------------------------------------------

// Reads one full packet: [VarInt length][packet ID + data, `length` bytes].
// Returns the packet ID, fills `payload`/`payload_len` with the bytes that
// follow the packet ID (NOT including the ID itself). Returns -1 on error.
// `payload` must be at least MC_MAX_PACKET_SIZE bytes.
static int read_packet(int sock, uint8_t *payload, int *payload_len) {
    int ok; 
    int32_t length = mc_read_varint_sock(sock, &ok); // reads varint
    if (!ok || length <= 0 || length > MC_MAX_PACKET_SIZE) {
        if (ok) ESP_LOGW(TAG, "Rejecting oversized/invalid packet length %ld", (long)length);
        return -1;
    }

    uint8_t buf[MC_MAX_PACKET_SIZE];
    if (mc_read_exact(sock, buf, (size_t)length) != 0) { // adds bytes to buf based on varint
        return -1;
    }

    size_t offset = 0;
    int32_t packet_id = mc_read_varint_buf(buf, (size_t)length, &offset, &ok); // reads varint from buffer
    if (!ok) {
        ESP_LOGW(TAG, "Malformed packet ID");
        return -1;
    }

    int remaining = (int)length - (int)offset;
    if (remaining < 0) remaining = 0;
    memcpy(payload, buf + offset, remaining);
    *payload_len = remaining;

    return (int)packet_id;
}

// Sends a full packet given a pre-built [packet ID + data] body.
static int send_packet(int sock, const uint8_t *body, int body_len) {
    uint8_t len_buf[5];
    int len_bytes = mc_write_varint(len_buf, body_len);

    if (send(sock, len_buf, len_bytes, 0) != len_bytes) return -1;
    if (send(sock, body, body_len, 0) != body_len) return -1;
    return 0;
}

// --------------------------------------------------------------------
// Reading specific field types out of a payload buffer
// --------------------------------------------------------------------

// Reads a Minecraft "String" (VarInt length-prefixed UTF-8) from buf.
// out must have room for at least max_out bytes (including the null
// terminator); the string is truncated if necessary. Returns the number
// of bytes consumed from buf, or -1 on error.
static int read_string(const uint8_t *buf, size_t len, size_t offset,
                        char *out, size_t max_out) {
    int ok;
    size_t local_offset = offset;
    int32_t str_len = mc_read_varint_buf(buf, len, &local_offset, &ok); // takes varint from buffer
    if (!ok || str_len < 0 || (size_t)(local_offset + str_len) > len) {
        return -1;
    }

    size_t copy_len = (size_t)str_len;
    if (copy_len > max_out - 1) copy_len = max_out - 1;     // copies  the data from buffer to char* out. char* out is one greater than read string for the null char

    memcpy(out, buf + local_offset, copy_len);
    out[copy_len] = '\0'; // adds null char

    return (int)((local_offset + (size_t)str_len) - offset); // returns length of bytes consumed
}

// --------------------------------------------------------------------
// Status response JSON
// --------------------------------------------------------------------

static void build_status_json(char *out, size_t out_size) {
    snprintf(out, out_size, // safe way of printing without overflowing, string print format n bytes limited.
        "{"
          "\"version\":{\"name\":\"%s\",\"protocol\":%d},"
          "\"players\":{\"max\":%d,\"online\":%d,\"sample\":[]},"
          "\"description\":{\"text\":\"%s\"}"
        "}",
        MC_VERSION_NAME, MC_PROTOCOL_VERSION,
        MC_MAX_PLAYERS, MC_ONLINE_PLAYERS,
        MC_MOTD
    );
}

// --------------------------------------------------------------------
// State handlers
// --------------------------------------------------------------------

// Status state: client sends Request (0x00) then optionally Ping (0x01).
static void handle_status_state(int sock) {
    uint8_t payload[MC_MAX_PACKET_SIZE];
    int payload_len;

    // Packet 0x00: Status Request (empty payload) -> respond with JSON.
    int packet_id = read_packet(sock, payload, &payload_len);
    if (packet_id != 0x00) { // reject invalid packet
        ESP_LOGW(TAG, "Expected Status Request, got 0x%02x", packet_id);
        return;
    }

    char json[768];
    build_status_json(json, sizeof(json));

    size_t json_len = strlen(json); // why use strlen on top but sizeof at bottom
    uint8_t body[8 + sizeof(json)];
    int idx = 0;
    body[idx++] = 0x00; // packet ID: Response
    idx += mc_write_varint(body + idx, (int32_t)json_len); // pointer arithmatic to write one byte after body the varint. idx = len(varint)+idx
    memcpy(body + idx, json, json_len);//copy to after varint, the json of json length
    idx += json_len; // idx becomes the body length

    if (send_packet(sock, body, idx) != 0) { // send packet to sock of body with bodylength
        ESP_LOGW(TAG, "Failed to send status response");
        return;
    }

    // Packet 0x01: Ping (contains an 8-byte long payload) -> Pong, echo it back.
    packet_id = read_packet(sock, payload, &payload_len);
    if (packet_id != 0x01 || payload_len != 8) {
        // Client may simply close after status (e.g. server list refresh
        // without opening the connection details). Not an error.
        return;
    }

    uint8_t pong_body[9];
    pong_body[0] = 0x01; // packet ID: Pong
    memcpy(pong_body + 1, payload, 8); // echo the same 8 bytes back
    send_packet(sock, pong_body, sizeof(pong_body));
}
// Login state: client sends Login Start (0x00) with their username.
// We don't implement actual login (no encryption / compression / play
// state yet), so we cleanly kick with a chat-formatted JSON reason.
static int login_return_login_success (int sock, char* username, const char* TAG, struct ClientInformation* user) {
    
    if(user == NULL) {
        ESP_LOGI(TAG, "Memory Allocation Failed at line %d, program %c", __LINE__, __FILE__);
        return 1;
    }
    memcpy(user->UUID, username, sizeof(uint16_t));
    memcpy(user->username, username, strlen(username));
    uint8_t body[11+strlen((char*)user->UUID) + strlen((char*)user->username)];
    uint idx = 0;
    body [idx++] = 0x02;
    memcpy(body+idx, user->UUID, 16);
    idx += 16;
    idx += mc_write_varint(body+idx, (int32_t)strlen((char*)user->username));
    memcpy(body+idx, user->username, strlen((char*)user->username));
    idx += strlen((char*)user->username);

    send_packet(sock, body, idx);
    ESP_LOGI(TAG, "sent login success");

    return 0;
}

static int play_join_game(int sock, struct ClientInformation* user) {
    char* strWorldName = "michael"; // debug name

    user->entityID = 1;
    memcpy(user->worldName, strWorldName, strlen(strWorldName)); 
    user->enableRespawnScreen = false;

    // writes the data required to join world into a packet
    uint8_t body[1024];
    uint32_t idx = 0;
    body[idx++] = 0x24;
    memcpy(body+idx, &user->entityID, sizeof(user->entityID));
    idx += sizeof(user->entityID);

    body[idx++] = user->isHardcore? 1 : 0;

    memcpy(body+idx, &user->gamemode, sizeof(user->gamemode));
    idx += sizeof(user->gamemode);

    memcpy(body+idx, &user->previousGamemode, sizeof(user->previousGamemode));
    idx += sizeof(user->previousGamemode);

    idx += mc_write_varint(body+idx, 1);

     size_t wlen = strlen(user->worldName);

    idx += mc_write_varint(body+idx, wlen);

    memcpy(body+idx, &user->worldName, wlen);
    idx += (int)wlen;

    memcpy(body + idx, DIMENSION_CODEC_NBT, DIMENSION_CODEC_NBT_LEN);
    idx += DIMENSION_CODEC_NBT_LEN;

    memcpy(body + idx, DIMENSION_NBT, DIMENSION_NBT_LEN);
    idx += DIMENSION_NBT_LEN;

    idx += mc_write_varint(body + idx, (int32_t)wlen);
    memcpy(body + idx, user->worldName, wlen);
    idx += (int)wlen;

    memcpy(body+idx, &user->hashedSeed, sizeof(user->hashedSeed));
    idx += sizeof(user->hashedSeed);
    user->maxPlayers = 4;
    idx += mc_write_varint(body+idx, 4);
    user->viewDistance = 2;
    idx += mc_write_varint(body+idx, 2);

    user->enableRespawnScreen = true;

    body[idx++] = user->reducedDebugInfo? 1 : 0;
    body[idx++] = user->enableRespawnScreen? 1 : 0;
    body[idx++] = user->isDebug? 1 : 0;

    user->isFlat = false;
    body[idx++] = user->isFlat? 1 : 0;

    ESP_LOGI("mc", "Join Game packet size: %d bytes", idx);

    if(send_packet(sock, body, idx)) {
        return 1;
    };

    return 0;
}

static int play_player_position_and_look(int sock, struct ClientInformation* user){
    user->x = 0;
    user->y = 20;
    user->z = 0;
    user->yaw = 0;
    user->pitch = 0;
    user->flags = 0;

    uint8_t body[64];
    int idx = 0;

    body[idx++] = 0x34;

    uint64_t x_bits; memcpy(&x_bits, &user->x, 8);
    x_bits = __builtin_bswap64(x_bits);
    memcpy(body+idx, &user->x, sizeof(user->x)); 
    idx += sizeof(user->x);

    uint64_t y_bits; memcpy(&y_bits, &user->y, 8);
    y_bits = __builtin_bswap64(y_bits);
    memcpy(body+idx, &y_bits, sizeof(user->y)); 
    idx += sizeof(user->y);

    uint64_t z_bits; memcpy(&z_bits, &user->z, 8);
    z_bits = __builtin_bswap64(z_bits);
    memcpy(body+idx, &user->z, sizeof(user->z)); 
    idx += sizeof(user->z);

    memcpy(body+idx, &user->yaw, sizeof(user->yaw)); 
    idx += sizeof(user->yaw);

    memcpy(body+idx, &user->pitch, sizeof(user->pitch)); 
    idx += sizeof(user->pitch);

    memcpy(body+idx, &user->flags, sizeof(user->flags)); 
    idx += sizeof(user->flags);

    idx += mc_write_varint(body+idx, 0);

    if(send_packet(sock, body, idx)){
        return 1;
    }

    return 0;
}

void mc_keep_alive_task (void* arg) {
    int sock = (int)(intptr_t)arg;
    uint64_t ka_id = 0;

    while(1){
        vTaskDelay(pdMS_TO_TICKS(10000));

        uint8_t body[9];
        int idx = 0;
        body[idx++] = 0x1F;
        uint64_t id_be = __builtin_bswap64(ka_id++);
        memcpy(body + idx, &id_be, 8);
        idx += 8;

        if (send_packet(sock, body, idx) != 0) {
            ESP_LOGI("mc_ka", "Socket closed, stopping keepalive");
            vTaskDelete(NULL);
            return;
        }
        ESP_LOGI("mc_ka", "Sent keepalive ID %llu", (unsigned long long)ka_id);

    }
}

static int play_update_view_position(int sock, struct ClientInformation* user, int chunkX, int chunkZ){
    user->chunkX = chunkX;
    user->chunkZ = chunkZ;

    uint8_t body[11];
    int idx = 0;
    body[idx++] = 0x40;
    idx += mc_write_varint(body+idx, chunkX);
    idx += mc_write_varint(body+idx, chunkZ);
    if(send_packet(sock, body, idx)){
        return 1;
    }
    return 0;
}

// claude test
#include "nbt_data.h"

// Builds one flat chunk section (y=0 to y=15):
// y=0: bedrock (block state 33)
// y=1-2: dirt (block state 10)
// y=3: grass (block state 9)
// y=4-15: air (block state 0)
static int build_flat_section(uint8_t *out) {
    int idx = 0;

    // Block count (non-air blocks): 16*16*1 bedrock + 16*16*2 dirt + 16*16*1 grass
    uint16_t block_count = htons(16 * 16 * 4);
    memcpy(out + idx, &block_count, 2); idx += 2;

    // Bits per entry: 4 (minimum, supports palette of up to 16 entries)
    out[idx++] = 4;

    // Palette: VarInt count + entries
    // 0=air, 1=grass_block[snowy=false] state 9, 2=dirt state 10, 3=bedrock state 33
    idx += mc_write_varint(out + idx, 4); // 4 palette entries
    idx += mc_write_varint(out + idx, 0);  // air
    idx += mc_write_varint(out + idx, 9);  // grass
    idx += mc_write_varint(out + idx, 10); // dirt
    idx += mc_write_varint(out + idx, 33); // bedrock

    // Data array: 4 bits per block, 16*16*16 = 4096 blocks
    // Packed into 64-bit longs: each long holds 64/4 = 16 blocks
    // Total longs: 4096 / 16 = 256 longs
    #define SECTION_LONGS 256
    idx += mc_write_varint(out + idx, SECTION_LONGS);

    uint64_t data[SECTION_LONGS];
    memset(data, 0, sizeof(data));

    // Fill blocks: iterate y,z,x (Minecraft storage order is x + z*16 + y*256)
    for (int y = 0; y < 16; y++) {
        for (int z = 0; z < 16; z++) {
            for (int x = 0; x < 16; x++) {
                // Palette index for this block
                uint8_t palette_idx;
                if      (y == 0) palette_idx = 3; // bedrock
                else if (y <= 2) palette_idx = 2; // dirt
                else if (y == 3) palette_idx = 1; // grass
                else             palette_idx = 0; // air

                // Pack into data array: 4 bits per entry
                int block_idx = x + z * 16 + y * 256;
                int long_idx  = block_idx / 16;
                int bit_idx   = (block_idx % 16) * 4;
                data[long_idx] |= ((uint64_t)palette_idx << bit_idx);
            }
        }
    }

    // Write longs big-endian
    for (int i = 0; i < SECTION_LONGS; i++) {
        uint64_t be = __builtin_bswap64(data[i]);
        memcpy(out + idx, &be, 8);
        idx += 8;
    }

    return idx;
}

static int send_chunk(int sock, int32_t chunk_x, int32_t chunk_z) {
    // Chunk packet can be large — allocate on heap not stack
    uint8_t *body = malloc(6144);
    uint8_t *section_buf = malloc(4096);
    if (!body || !section_buf) {
        ESP_LOGE("mc", "malloc failed for chunk buffers");
        free(body);
        free(section_buf);
        return -1;
    }
    int idx = 0;

    // Packet ID
    body[idx++] = 0x20;

    // Chunk X, Z: big-endian Int
    int32_t cx = htonl(chunk_x);
    int32_t cz = htonl(chunk_z);
    memcpy(body + idx, &cx, 4); idx += 4;
    memcpy(body + idx, &cz, 4); idx += 4;

    // Full chunk = true
    body[idx++] = 1;

    // Primary bit mask: only section 0 (y=0..15) is non-empty
    idx += mc_write_varint(body + idx, 0x0001);

    // Heightmaps NBT — minimal valid compound with MOTION_BLOCKING and WORLD_SURFACE
    // Both are TAG_Long_Array of 36 longs (9 bits * 256 columns packed)
    // For a flat world at y=4 all values = 4, packed into 36 longs
    uint64_t hm_val = 0;
    // Pack 256 entries of value 4 (3 bits each... actually 9 bits in 1.16)
    // 9 bits per column, 7 columns per long (9*7=63 bits, 1 bit padding)
    uint64_t hm_longs[36];
    memset(hm_longs, 0, sizeof(hm_longs));
    for (int i = 0; i < 256; i++) {
        int li  = (i * 9) / 64;
        int bit = (i * 9) % 64;
        hm_longs[li] |= ((uint64_t)4 << bit);
        if (bit + 9 > 64 && li + 1 < 36) {
            hm_longs[li+1] |= ((uint64_t)4 >> (64 - bit));
        }
    }

    // Write heightmap NBT inline
    // TAG_Compound("")
    body[idx++] = 0x0A; body[idx++] = 0x00; body[idx++] = 0x00; // compound, empty name

    // TAG_Long_Array "MOTION_BLOCKING"
    const char *hm_name1 = "MOTION_BLOCKING";
    const char *hm_name2 = "WORLD_SURFACE";
    for (int pass = 0; pass < 2; pass++) {
        const char *nm = (pass == 0) ? hm_name1 : hm_name2;
        uint16_t nlen = htons((uint16_t)strlen(nm));
        body[idx++] = 12; // TAG_Long_Array
        memcpy(body + idx, &nlen, 2); idx += 2;
        memcpy(body + idx, nm, strlen(nm)); idx += strlen(nm);
        int32_t arr_len = htonl(36);
        memcpy(body + idx, &arr_len, 4); idx += 4;
        for (int i = 0; i < 36; i++) {
            uint64_t be = __builtin_bswap64(hm_longs[i]);
            memcpy(body + idx, &be, 8); idx += 8;
        }
    }
    body[idx++] = 0x00; // TAG_End closes heightmap compound

    // Biomes: 1024 VarInts (one per 4x4x4 column), all plains (ID 1)
    idx += mc_write_varint(body + idx, 1024);
    for (int i = 0; i < 1024; i++) {
        idx += mc_write_varint(body + idx, 1); // plains biome
    }

    // Data: build section bytes into temp buffer then write with length prefix
    int section_len = build_flat_section(section_buf);
    idx += mc_write_varint(body + idx, section_len);
    memcpy(body + idx, section_buf, section_len);
    idx += section_len;

    idx += mc_write_varint(body + idx, 0);

    ESP_LOGI("mc", "Chunk (%d,%d) packet: %d bytes", chunk_x, chunk_z, idx);
    int result = send_packet(sock, body, idx);

    free(section_buf);
    free(body);
    return result;
}

// Send a grid of chunks around spawn
static void send_spawn_chunks(int sock) {
    for (int x = -2; x <= 2; x++) {
        for (int z = -2; z <= 2; z++) {
            send_chunk(sock, x, z);
        }
    }
}

//end claude test


void mc_play_loop(int sock) {
    uint8_t payload[512];
    int payload_len;

    while (1) {
        int id = read_packet(sock, payload, &payload_len);
        if (id < 0) {
            ESP_LOGI("mc", "Client disconnected");
            break;
        }
        switch (id) {
            case 0x1F: break; // keepalive echo — discard
            case 0x12: break; // player position — discard for now
            case 0x13: break; // player position and look — discard for now
            case 0x05: break; // client settings — discard for now
            case 0x04: break; // client status — discard for now
            default:
                ESP_LOGW("mc", "Unhandled play packet 0x%02x len=%d", id, payload_len);
                break;
        }
    }
}

static void handle_login_state(int sock) {
    uint8_t payload[MC_MAX_PACKET_SIZE];
    int payload_len;

    int packet_id = read_packet(sock, payload, &payload_len);
    if (packet_id != 0x00) {
        ESP_LOGW(TAG, "Expected Login Start, got 0x%02x", packet_id);
        return;
    }

    char username[32];
    int consumed = read_string(payload, payload_len, 0, username, sizeof(username));
    if (consumed < 0) {
        ESP_LOGW(TAG, "Malformed Login Start packet");
        return;
    }
    ESP_LOGI(TAG, "Login attempt from username: %s", username);

    struct ClientInformation *user = calloc(1024, 1);

    if (login_return_login_success(sock, username, TAG, user)){ // login success return from server to client
        ESP_LOGI(TAG, "could not return login success");
    }
    if (play_join_game(sock, user)){
        ESP_LOGI(TAG, "Failed to send join game packet");
    }
    // start keepalive task cycle
    TaskHandle_t ka_handle = NULL;
    xTaskCreate(mc_keep_alive_task, "mc_ka", 2048, (void*)(intptr_t)sock, 5, &ka_handle);

    if (play_player_position_and_look(sock, user)) {
        ESP_LOGI(TAG, "Failed to send player position and look");
    }

    if (play_update_view_position(sock, user, 0, 0)) {
        ESP_LOGI("mc_play", "update view position failed.");
    }

    send_spawn_chunks(sock);

    mc_play_loop(sock); // blocks until client disconnects

    // Clean up keepalive task if play loop exits
    if (ka_handle != NULL) {
        vTaskDelete(ka_handle);
    }
}

// --------------------------------------------------------------------
// Entry point
// --------------------------------------------------------------------

void mc_handle_connection(int sock) {
    uint8_t payload[MC_MAX_PACKET_SIZE];
    int payload_len;

    // First packet must be Handshake (0x00):
    //   VarInt protocol_version
    //   String  server_address
    //   UShort  server_port
    //   VarInt  next_state (1 = status, 2 = login)
    int packet_id = read_packet(sock, payload, &payload_len);
    if (packet_id != 0x00) {
        ESP_LOGW(TAG, "Expected Handshake, got 0x%02x", packet_id);
        close(sock);
        return;
    }

    size_t offset = 0;
    int ok;
    int32_t protocol_version = mc_read_varint_buf(payload, payload_len, &offset, &ok);
    if (!ok) { close(sock); return; }

    char server_addr[256];
    int consumed = read_string(payload, payload_len, offset, server_addr, sizeof(server_addr));
    if (consumed < 0) { close(sock); return; }
    offset += consumed;

    if (offset + 2 > (size_t)payload_len) { close(sock); return; }
    offset += 2; // skip server_port (UShort), unused

    int32_t next_state = mc_read_varint_buf(payload, payload_len, &offset, &ok);
    if (!ok) { close(sock); return; }

    ESP_LOGI(TAG, "Handshake: protocol=%ld next_state=%ld",
             (long)protocol_version, (long)next_state);

    if (next_state == NEXT_STATE_STATUS) {
        handle_status_state(sock);
    } else if (next_state == NEXT_STATE_LOGIN) {
        handle_login_state(sock);
    } else {
        ESP_LOGW(TAG, "Unknown next_state %ld", (long)next_state);
    }

    close(sock);
}
