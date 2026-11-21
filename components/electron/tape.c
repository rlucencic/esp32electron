/* UEF cassette playback and recording. Time is in 2 MHz ticks. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "elk_internal.h"
#include "uef.h"

#define NO_EVENT UINT64_MAX
#define TX_BIT_TICKS ((ELK_CPU_HZ + 600) / 1200)

enum { SILENCE = -1, END_OF_TAPE = -2 };

typedef struct {
    uint8_t *image;
    size_t size, capacity, persisted, offset;
    char path[192];
    const uint8_t *chunk;
    uint32_t length, index;
    unsigned id, baud;
    uint32_t received, ones;
    unsigned bits;
    uint8_t shift, data;
    int symbol;
    uint32_t ticks;
    uint32_t recorded;
    bool fast, mounted, writable, write_error, full;
} tape_t;

static tape_t tape;
static uint64_t rx_event = NO_EVENT;
static struct {
    uint64_t event, start, end, last_end;
    size_t data_offset;
    uint8_t value;
    bool active, new_session;
} tx = { .event = NO_EVENT };
uint64_t elk_tape_event = NO_EVENT;

static float le_float(const uint8_t *p)
{
    uint32_t bits = uef_le32(p);
    float value;
    memcpy(&value, &bits, sizeof(value));
    return value;
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
        if (tape.length == 2 && carrier(uef_le16(p), i, ticks)) {
            return 1;
        }
        break;
    case 0x0111:
        if (tape.length == 4) {
            uint32_t lead = uef_le16(p);
            if (carrier(lead, i, ticks)) {
                return 1;
            }
            i -= tape.fast && lead > 64 ? 32 : (lead + 1) / 2;
            if (i < 10) {
                return serial_bit(0xAA, i, 8, false, false);
            }
            if (carrier(uef_le16(p + 2), i - 10, ticks)) {
                return 1;
            }
        }
        break;
    case 0x0112:
        if (i == 0 && tape.length == 2 && uef_le16(p)) {
            *ticks = (uint64_t)*ticks * uef_le16(p) / 2;
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
            tape.id = uef_le16(header);
            tape.length = uef_le32(header + 2);
            tape.chunk = header + UEF_CHUNK_HEADER;
            tape.offset += UEF_CHUNK_HEADER + tape.length;
            tape.index = 0;
            if (tape.id == 0x0113 && tape.length == 4) {
                float baud = le_float(tape.chunk);
                if (baud >= 300 && baud <= 10000) {
                    tape.baud = baud + 0.5f;
                }
            } else if (tape.id == 0x0117 && tape.length == 2) {
                unsigned baud = uef_le16(tape.chunk);
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
    rx_event = now + tape.ticks;
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

static void publish_event(void)
{
    elk_tape_event = rx_event < tx.event ? rx_event : tx.event;
    m6502_lower_until(&elk.cpu, elk_tape_event);
}

static bool reserve(size_t needed)
{
    if (needed <= tape.capacity) {
        return true;
    }
    if (needed > UEF_MAX_SIZE) {
        return false;
    }
    size_t capacity = tape.capacity < 4096 ? 4096 : tape.capacity * 2;
    if (capacity < needed) {
        capacity = needed;
    }
    if (capacity > UEF_MAX_SIZE) {
        capacity = UEF_MAX_SIZE;
    }
    uint8_t *image = ELK_SLOW_ALLOC(capacity);
    if (!image) {
        return false;
    }
    memcpy(image, tape.image, tape.size);
    if (tape.chunk) {
        tape.chunk = image + (tape.chunk - tape.image);
    }
    free(tape.image);
    tape.image = image;
    tape.capacity = capacity;
    return true;
}

static void put32(uint8_t *p, uint32_t value)
{
    for (unsigned i = 0; i < 4; i++) {
        *p++ = value >> (i * 8);
    }
}

static void append_tone(uint8_t kind, unsigned count)
{
    uint8_t chunk[] = { kind, 1, 2, 0, 0, 0, count & 255, count >> 8 };
    memcpy(tape.image + tape.size, chunk, sizeof(chunk));
    tape.size += sizeof(chunk);
}

static void record_byte(void)
{
    if (!tape.mounted || !tape.writable || tape.full) {
        return;
    }
    uint64_t gap = tx.start >= tx.last_end ? (tx.start - tx.last_end) / TX_BIT_TICKS : 0;
    bool leader = !tx.data_offset || gap >= 10;
    if (leader && gap < 32) {
        gap = 32;
    }
    if (gap > 32767) {
        gap = 32767;
    }
    size_t extra = (leader ? 8 + UEF_CHUNK_HEADER : 0) + (tx.new_session && tape.size > UEF_HEADER ? 8 : 0);
    if (!reserve(tape.size + extra + 1)) {
        tape.full = true;
        return;
    }
    if (tx.new_session && tape.size > UEF_HEADER) {
        append_tone(0x12, 600);       /* separate successive recordings by 250 ms */
    }
    tx.new_session = false;
    if (leader) {
        append_tone(0x10, gap * 2);
        tx.data_offset = tape.size;
        tape.image[tape.size++] = 0;
        tape.image[tape.size++] = 1;
        tape.size += 4;
    }
    tape.image[tape.size++] = tx.value;
    put32(tape.image + tx.data_offset + 2, tape.size - tx.data_offset - UEF_CHUNK_HEADER);
    tape.recorded++;
    tx.last_end = tx.end;
}

void elk_tape_advance(uint64_t now)
{
    while (elk_tape_event <= now) {
        if (rx_event <= tx.event) {
            uint64_t at = rx_event;
            rx_event = NO_EVENT;
            tape.ticks = 0;
            receive_symbol(tape.symbol);
            schedule(at);
        } else {
            record_byte();
            tx.event = NO_EVENT;
            elk_ula_raise(INT_TX_EMPTY);
        }
        publish_event();
    }
}

void elk_tape_control(void)
{
    uint64_t now = elk.cpu.cycles;
    elk_tape_advance(now);
    bool output = (elk.control & 0x46) == 0x44;
    if (output != tx.active) {
        tx.active = output;
        tx.event = NO_EVENT;
        tx.data_offset = 0;
        tx.end = tx.last_end = now;
        tx.new_session = output;
        if (output) {
            elk_ula_raise(INT_TX_EMPTY);
        }
    }
    if (receiving() && tape.mounted) {
        if (rx_event == NO_EVENT) {
            schedule(now);
        }
    } else if (rx_event != NO_EVENT) {
        tape.ticks = rx_event - now;
        rx_event = NO_EVENT;
        tape.ones = tape.bits = 0;
    }
    publish_event();
}

void elk_tape_write(uint8_t value)
{
    uint64_t now = elk.cpu.cycles;
    elk_tape_advance(now);
    elk.int_status &= ~INT_TX_EMPTY;
    elk_ula_update_irq();
    if (tx.active) {
        tx.value = value;
        if (tx.event == NO_EVENT) {
            tx.start = now < tx.end ? tx.end : now;
            tx.event = tx.start + 9 * TX_BIT_TICKS;
            tx.end = tx.event + TX_BIT_TICKS;
        }
        publish_event();
    }
}

uint8_t elk_tape_data(void)
{
    return tape.data;
}

void elk_tape_rewind(void)
{
    rx_event = NO_EVENT;
    publish_event();
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
    if (strlen(path) >= sizeof(tape.path) || !elk_tape_flush()) {
        return false;
    }
    size_t length;
    bool compressed;
    uint8_t *image = uef_load(path, &length, &compressed);
    if (!image) {
        return false;
    }
    bool disc = false, audio = false;
    for (size_t offset = UEF_HEADER; offset < length;) {
        unsigned id = uef_le16(image + offset);
        disc |= id == 0x0201 || id == 0x0202;
        audio |= id == 0x0100 || id == 0x0102 || id == 0x0104;
        offset += UEF_CHUNK_HEADER + uef_le32(image + offset + 2);
    }
    if (disc && !audio) {
        free(image);
        return false;
    }
    size_t path_length = strlen(path);
    bool recording = path_length >= 8 && !strcasecmp(path + path_length - 8, ".rec.uef");
    FILE *file = recording && !compressed && !disc ? fopen(path, "r+b") : NULL;
    bool writable = file != NULL;
    if (file) {
        fclose(file);
    }
    free(tape.image);
    tape.image = image;
    tape.size = tape.capacity = tape.persisted = length;
    tape.mounted = true;
    tape.writable = writable;
    tape.write_error = tape.full = false;
    tape.recorded = 0;
    strcpy(tape.path, path);
    tx.data_offset = 0;
    tx.new_session = true;
    tx.last_end = elk.cpu.cycles;
    elk_tape_rewind();
    return true;
}

bool elk_tape_create(const char *path)
{
    FILE *file = fopen(path, "wbx");
    if (!file) {
        return false;
    }
    static const uint8_t header[UEF_HEADER] = "UEF File!\0\x0a";
    bool ok = fwrite(header, 1, sizeof(header), file) == sizeof(header);
    ok = fclose(file) == 0 && ok;
    if (!ok) {
        remove(path);
    }
    return ok;
}

bool elk_tape_dirty(void)
{
    return tape.size > tape.persisted && (!tx.active || !tape.mounted);
}

bool elk_tape_flush(void)
{
    if (tape.size > tape.persisted) {
        FILE *file = fopen(tape.path, "r+b");
        bool ok = false;
        if (file) {
            size_t length = tape.size - tape.persisted;
            ok = fseek(file, tape.persisted, SEEK_SET) == 0 &&
                 fwrite(tape.image + tape.persisted, 1, length, file) == length;
            ok = fclose(file) == 0 && ok;
        }
        tape.write_error = !ok;
        if (!ok) {
            return false;
        }
        tape.persisted = tape.size;
        tx.data_offset = 0;
    }
    if (!tape.mounted) {
        free(tape.image);
        tape.image = NULL;
        tape.size = tape.capacity = tape.persisted = 0;
    }
    return true;
}

void elk_tape_eject(void)
{
    tape.mounted = false;
    elk_tape_rewind();
    elk_tape_flush();
}

void elk_tape_fast(bool fast)
{
    tape.fast = fast;
}

void elk_tape_status(elk_tape_status_t *status)
{
    *status = (elk_tape_status_t) {
        .size = tape.mounted ? tape.size : 0, .position = tape.mounted ? tape.offset : 0,
        .received = tape.received, .playing = rx_event != NO_EVENT,
        .recorded = tape.mounted ? tape.recorded : 0,
        .recording = tx.active && tape.mounted && tape.writable,
        .writable = tape.mounted && tape.writable,
        .write_error = tape.write_error || (tape.mounted && tape.full),
    };
}
