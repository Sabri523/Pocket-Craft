#pragma once

#include <stdint.h>
#include <stddef.h>

// Reads a VarInt from a raw socket (blocking, byte by byte).
// Returns the decoded value, or sets *ok = false on error/disconnect.
int32_t mc_read_varint_sock(int sock, int *ok);

// Reads a VarInt from an in-memory buffer.
// *offset is advanced past the VarInt. Returns the decoded value.
// Sets *ok = false if the buffer doesn't contain a complete/valid VarInt.
int32_t mc_read_varint_buf(const uint8_t *buf, size_t len, size_t *offset, int *ok);

// Encodes a VarInt into dst (must have room for up to 5 bytes).
// Returns the number of bytes written.
int mc_write_varint(uint8_t *dst, int32_t value);

// Returns the number of bytes a VarInt encoding of `value` would take.
int mc_varint_size(int32_t value);

// Reads exactly `len` raw bytes from the socket into buf. Returns 0 on
// success, -1 on error or premature disconnect.
int mc_read_exact(int sock, uint8_t *buf, size_t len);
