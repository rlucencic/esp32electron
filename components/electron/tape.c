/* UEF input and the ULA's serial receiver. Time is in 2 MHz ticks. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef ESP_PLATFORM
#include "miniz.h"
#else
#include <zlib.h>
#endif

#include "elk_internal.h"

#define MAX_IMAGE (4u * 1024 * 1024)
#define UEF_HEADER 12
#define NO_EVENT UINT64_MAX

enum { SILENCE = -1, END_OF_TAPE = -2 };

typedef struct {
    uint8_t *image;
    size_t size, offset;
    const uint8_t *chunk;
    uint32_t length, index;
    unsigned id, baud;
    uint32_t received, ones;
    unsigned bits;
    uint8_t shift, data;
    int symbol;
    uint32_t ticks;
    bool fast;
} tape_t;

static tape_t tape;
uint64_t elk_tape_event = NO_EVENT;

static uint16_t le16(const uint8_t *p)
{
    return p[0] | (uint16_t)p[1] << 8;
}

static uint32_t le32(const uint8_t *p)
{
    return le16(p) | (uint32_t)le16(p + 2) << 16;
}

static float le_float(const uint8_t *p)
{
    uint32_t bits = le32(p);
    float value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static bool inflate_raw(const uint8_t *in, size_t size, uint8_t *out, size_t length)
{
#ifdef ESP_PLATFORM
    tinfl_decompressor *decoder = ELK_SLOW_ALLOC(sizeof(*decoder));
    if (!decoder) {
        return false;
    }
    tinfl_init(decoder);
    size_t input = size, output = length;
    tinfl_status status = tinfl_decompress(decoder, in, &input, out, out, &output,
                                          TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
    free(decoder);
    return status == TINFL_STATUS_DONE && input == size && output == length;
#else
    z_stream stream = { .next_in = (Bytef *)in, .avail_in = size,
                        .next_out = out, .avail_out = length };
    if (inflateInit2(&stream, -MAX_WBITS) != Z_OK) {
        return false;
    }
    bool ok = inflate(&stream, Z_FINISH) == Z_STREAM_END &&
              stream.total_in == size && stream.total_out == length;
    inflateEnd(&stream);
    return ok;
#endif
}

static uint32_t image_crc(const uint8_t *bytes, size_t size)
{
#ifdef ESP_PLATFORM
    return mz_crc32(0, bytes, size);
#else
    return crc32(0, bytes, size);
#endif
}

static uint8_t *gunzip(const uint8_t *in, size_t *length)
{
    if (*length < 18 || in[2] != 8 || (in[3] & 0xE0)) {
        return NULL;
    }
    size_t end = *length - 8, start = 10;
    uint8_t flags = in[3];
    if (flags & 4) {
        if (end - start < 2) {
            return NULL;
        }
        start += 2 + le16(in + start);
        if (start > end) {
            return NULL;
        }
    }
    for (uint8_t flag = 8; flag <= 16; flag <<= 1) {
        if (flags & flag) {
            const uint8_t *nul = memchr(in + start, 0, end - start);
            if (!nul) {
                return NULL;
            }
            start = (size_t)(nul - in) + 1;
        }
    }
    if (flags & 2) {
        if (end - start < 2 || (uint16_t)image_crc(in, start) != le16(in + start)) {
            return NULL;
        }
        start += 2;
    }
    size_t size = le32(in + end + 4);
    if (start >= end || size < UEF_HEADER || size > MAX_IMAGE) {
        return NULL;
    }
    uint8_t *out = ELK_SLOW_ALLOC(size);
    if (!out || !inflate_raw(in + start, end - start, out, size) ||
        image_crc(out, size) != le32(in + end)) {
        free(out);
        return NULL;
    }
    *length = size;
    return out;
}

static bool valid_uef(const uint8_t *image, size_t size)
{
    if (size < UEF_HEADER || memcmp(image, "UEF File!", 10) != 0 || image[11] != 0) {
        return false;
    }
    size_t offset = UEF_HEADER;
    while (offset < size) {
        if (size - offset < 6) {
            return false;
        }
        uint32_t length = le32(image + offset + 2);
        offset += 6;
        if (length > size - offset) {
            return false;
        }
        offset += length;
    }
    return true;
}

/* Frames have a start bit, LSB-first data, optional parity and stop bits. */
static int serial_bit(uint8_t byte, unsigned index, unsigned width, bool parity, bool odd)
{
    if (index == 0) {
        return 0;
    }
    if (index <= width) {
        return (byte >> (index - 1)) & 1;
    }
    if (parity && index == width + 1) {
        return __builtin_parity(byte & ((1u << width) - 1)) ^ odd;
    }
    return 1;
}

static bool carrier(uint32_t cycles, uint32_t index, uint32_t *ticks)
{
    if (tape.fast && cycles > 64) {
        cycles = 64;                    /* 32 bits still let the MOS sync */
    }
    uint32_t used = index * 2;
    if (used >= cycles) {
        return false;
    }
    if (cycles - used == 1) {
        *ticks /= 2;
    }
    return true;
}

static int chunk_symbol(uint32_t *ticks)
{
    const uint8_t *p = tape.chunk;
    uint32_t i = tape.index;
    switch (tape.id) {
    case 0x0100:
        if (i / 10 < tape.length) {
            return serial_bit(p[i / 10], i % 10, 8, false, false);
        }
        break;
    case 0x0102:
        if (tape.length > 1 && p[0] <= 7 && i < (tape.length - 1) * 8 - p[0]) {
            return (p[1 + i / 8] >> (i % 8)) & 1;
        }
        break;
    case 0x0104:
        if (tape.length > 3 && p[0] >= 1 && p[0] <= 8) {
            bool parity = p[1] == 'E' || p[1] == 'O';
            unsigned frame = 1 + p[0] + parity + abs((int8_t)p[2]);
            if (i / frame < tape.length - 3) {
                return serial_bit(p[3 + i / frame], i % frame, p[0], parity, p[1] == 'O');
            }
        }
        break;
    case 0x0110:
        if (tape.length == 2 && carrier(le16(p), i, ticks)) {
            return 1;
        }
        break;
    case 0x0111:
        if (tape.length == 4) {
            uint32_t lead = le16(p);
            if (carrier(lead, i, ticks)) {
                return 1;
            }
            i -= tape.fast && lead > 64 ? 32 : (lead + 1) / 2;
            if (i < 10) {
                return serial_bit(0xAA, i, 8, false, false);
            }
            if (carrier(le16(p + 2), i - 10, ticks)) {
                return 1;
            }
        }
        break;
    case 0x0112:
        if (i == 0 && tape.length == 2 && le16(p)) {
            *ticks = (uint64_t)*ticks * le16(p) / 2;
            return SILENCE;
        }
        break;
    case 0x0116:
        if (i == 0 && tape.length == 4) {
            float seconds = le_float(p);
            if (seconds > 0 && seconds <= 600) {
                *ticks = 1 + (uint32_t)(seconds * ELK_CPU_HZ);
                return SILENCE;
            }
        }
        break;
    }
    return END_OF_TAPE;                 /* end of this chunk, or metadata */
}

static int next_symbol(uint32_t *ticks)
{
    while (tape.chunk || tape.offset < tape.size) {
        if (!tape.chunk) {
            const uint8_t *header = tape.image + tape.offset;
            tape.id = le16(header);
            tape.length = le32(header + 2);
            tape.chunk = header + 6;
            tape.offset += 6 + tape.length;
            tape.index = 0;
            if (tape.id == 0x0113 && tape.length == 4) {
                float baud = le_float(tape.chunk);
                if (baud >= 300 && baud <= 10000) {
                    tape.baud = baud + 0.5f;
                }
            } else if (tape.id == 0x0117 && tape.length == 2) {
                unsigned baud = le16(tape.chunk);
                if (baud == 300 || baud == 1200) {
                    tape.baud = baud;
                }
            }
        }
        *ticks = (ELK_CPU_HZ + tape.baud / 2) / tape.baud;
        int symbol = chunk_symbol(ticks);
        if (symbol != END_OF_TAPE) {
            tape.index++;
            if (symbol == SILENCE && tape.fast && *ticks > ELK_CPU_HZ / 10) {
                *ticks = ELK_CPU_HZ / 10;
            }
            return symbol;
        }
        tape.chunk = NULL;
    }
    return END_OF_TAPE;
}

static bool receiving(void)
{
    return (elk.control & 0x46) == 0x40 && (elk.counter & 0x7F) == 0;
}

static void schedule(uint64_t now)
{
    if (tape.ticks == 0) {
        tape.symbol = next_symbol(&tape.ticks);
        if (tape.symbol == END_OF_TAPE) {
            tape.ticks = 0;
            return;
        }
    }
    elk_tape_event = now + tape.ticks;
}

static void receive_symbol(int symbol)
{
    if (symbol == SILENCE) {
        tape.ones = tape.bits = 0;
        return;
    }
    if (symbol) {
        if (tape.ones < 10 && ++tape.ones == 10) {
            elk_ula_raise(INT_HIGH_TONE);
        }
    } else {
        tape.ones = 0;
    }

    if (tape.bits == 0) {
        if (symbol == 0) {
            tape.bits = 1;
            elk.int_status &= ~INT_RX_FULL;
            elk_ula_update_irq();
        }
    } else if (tape.bits <= 8) {
        tape.shift = (tape.shift >> 1) | (symbol << 7);
        if (tape.bits++ == 8) {
            tape.data = tape.shift;
            tape.received++;
            elk_ula_raise(INT_RX_FULL);
        }
    } else {
        tape.bits = 0;                  /* stop bit */
    }
}

void elk_tape_advance(uint64_t now)
{
    while (elk_tape_event <= now) {
        uint64_t at = elk_tape_event;
        elk_tape_event = NO_EVENT;
        tape.ticks = 0;
        receive_symbol(tape.symbol);
        schedule(at);
    }
}

void elk_tape_control(void)
{
    uint64_t now = elk.cpu.cycles;
    if (receiving()) {
        if (elk_tape_event == NO_EVENT && tape.image) {
            schedule(now);
            m6502_lower_until(&elk.cpu, elk_tape_event);
        }
    } else if (elk_tape_event != NO_EVENT) {
        elk_tape_advance(now);
        if (elk_tape_event != NO_EVENT) {
            tape.ticks = elk_tape_event - now;
        }
        elk_tape_event = NO_EVENT;
        tape.ones = tape.bits = 0;
    }
}

uint8_t elk_tape_data(void)
{
    return tape.data;
}

void elk_tape_rewind(void)
{
    elk_tape_event = NO_EVENT;
    tape.offset = tape.image ? UEF_HEADER : 0;
    tape.chunk = NULL;
    tape.baud = 1200;
    tape.ticks = tape.ones = tape.bits = tape.received = 0;
    tape.shift = tape.data = 0;
    elk.int_status &= ~(INT_RX_FULL | INT_HIGH_TONE);
    elk_ula_update_irq();
    elk_tape_control();
}

bool elk_tape_insert(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (!file) {
        return false;
    }
    long size = fseek(file, 0, SEEK_END) ? -1 : ftell(file);
    uint8_t *image = size >= UEF_HEADER && size <= MAX_IMAGE && !fseek(file, 0, SEEK_SET)
                     ? ELK_SLOW_ALLOC(size) : NULL;
    bool ok = image && fread(image, 1, size, file) == (size_t)size;
    fclose(file);
    size_t length = ok ? size : 0;
    if (ok && image[0] == 0x1F && image[1] == 0x8B) {
        uint8_t *raw = gunzip(image, &length);
        free(image);
        image = raw;
    }
    if (!ok || !image || !valid_uef(image, length)) {
        free(image);
        return false;
    }
    free(tape.image);
    tape.image = image;
    tape.size = length;
    elk_tape_rewind();
    return true;
}

void elk_tape_eject(void)
{
    free(tape.image);
    tape.image = NULL;
    tape.size = 0;
    elk_tape_rewind();
}

void elk_tape_fast(bool fast)
{
    tape.fast = fast;
}

void elk_tape_status(elk_tape_status_t *status)
{
    *status = (elk_tape_status_t) {
        .size = tape.size, .position = tape.offset,
        .received = tape.received, .playing = elk_tape_event != NO_EVENT,
    };
}
