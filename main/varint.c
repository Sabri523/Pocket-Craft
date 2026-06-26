#include "varint.h"
#include <sys/socket.h>
#include <errno.h>
#include <string.h>

#define SEGMENT_BITS 0x7F
#define CONTINUE_BIT 0x80

int mc_read_exact(int sock, uint8_t *buf, size_t len) {
    size_t total = 0;
    while (total < len) {
        ssize_t n = recv(sock, buf + total, len - total, 0); // write data from socket to buffer (buf+total is a pointer arithmatic that shifts memory tot the next.)
                                                             // len-total shows remaining bytes in buffer, and 0, where specific flags are present.
        if (n <= 0) {
            // n == 0 -> peer closed; n < 0 -> socket error
            return -1;
        }
        total += (size_t)n; // can be total += 1
    }
    return 0;
}

int32_t mc_read_varint_sock(int sock, int *ok) {
    int32_t value = 0;
    int position = 0;
    uint8_t byte;

    *ok = 1;

    while (1) {
        if (mc_read_exact(sock, &byte, 1) != 0) { // writes one byte to buffer
            *ok = 0;
            return 0;
        }

        value |= (int32_t)(byte & SEGMENT_BITS) << position; // shifts the first seven bits of the byte by position number and then ORs which copies to value

        if ((byte & CONTINUE_BIT) == 0) { // if no continuation bit, then return
            break;
        }

        position += 7; // else add 7 to the position for further left shift
        if (position >= 32) { //safety check
            // Malformed VarInt (too long)
            *ok = 0;
            return 0;
        }
    }

    return value; // value then contains the var int.
}

int32_t mc_read_varint_buf(const uint8_t *buf, size_t len, size_t *offset, int *ok) { // same as mc_read_varint_sock but from buf (uint8 array)
    int32_t value = 0;
    int position = 0;
    *ok = 1;

    while (1) {
        if (*offset >= len) {
            *ok = 0;
            return 0;
        }
        uint8_t byte = buf[*offset];
        (*offset)++;

        value |= (int32_t)(byte & SEGMENT_BITS) << position;

        if ((byte & CONTINUE_BIT) == 0) {
            break;
        }

        position += 7;
        if (position >= 32) {
            *ok = 0;
            return 0;
        }
    }

    return value;
}

int mc_write_varint(uint8_t *dst, int32_t value) {
    uint32_t uvalue = (uint32_t)value; // converts int32 value to uint32 because 8th bit is used as continuous bit and not as the sign bit
    int written = 0;

    while (1) {
        if ((uvalue & ~((uint32_t)SEGMENT_BITS)) == 0) { //uval & 0x1000000 == 0, which means show only first 7 bits
            dst[written++] = (uint8_t)uvalue; // write to dst the uval
            return written;// return number of bytes
        }

        dst[written++] = (uint8_t)((uvalue & SEGMENT_BITS) | CONTINUE_BIT); //conv uval to uint8 and then mask first 7 then turn 8th to 1 with OR MASK
        uvalue >>= 7; // uval right shifted by 7
    }
}

int mc_varint_size(int32_t value) {
    uint8_t scratch[5];
    return mc_write_varint(scratch, value); //returns data size, scratch is not dynamically allocated so the buffer is lost
}
