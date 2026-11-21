/*
 * Acorn Plus 3 and ACP DFS: WD1770 at &FCC4-&FCC7 and drive latch at &FCC0.
 * Both poll the controller without IRQ or NMI. The byte-transfer state machine
 * follows the disk controller in esp32ql, with timings for the Electron.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include "elk_internal.h"
#include "uef.h"

#define SECTOR_SIZE 256
#define SECTORS_PER_TRACK 16
#define TRACK_SIZE (SECTOR_SIZE * SECTORS_PER_TRACK)
#define TRACK_BYTES 6250
#define DFS_SECTORS_PER_TRACK 10
#define MAX_TRACK 83
#define BYTE_TICKS 64                   /* 32 us at 250 kbit/s */
#define REVOLUTION (ELK_CPU_HZ / 5)     /* 300 rpm */
#define SEEK_SECTOR (ELK_CPU_HZ / 1000)
#define NOT_FOUND (5 * REVOLUTION)

enum {
    BUSY = 0x01, DRQ = 0x02, LOST = 0x04, CRC_ERROR = 0x08,
    RNF = 0x10, SPUN_UP = 0x20, PROTECT = 0x40, MOTOR = 0x80,
};

typedef struct {
    uint8_t *image;
    char path[192];
    size_t size, file_size;
    size_t dirty_begin, dirty_end;
    int tracks, sides, head, sectors_per_track;
    bool interleaved, mounted, protect, write_error;
    uint32_t reads, writes;
} drive_t;

static drive_t drives[ELK_DISK_DRIVES];

typedef enum { WAIT, READ_SECTOR, WRITE_SECTOR, READ_ADDRESS, READ_TRACK, WRITE_TRACK } operation_t;

static struct {
    uint8_t latch, command, status, track, sector, data, result;
    int step, id;
    bool type1, requested;
    operation_t op;
    uint64_t ready, motor_off;
    unsigned pos, length, track_slots;
    uint8_t *buffer;
} fdc;

static int selected(void)
{
    return (fdc.latch & 1) ? 0 : (fdc.latch & 2) ? 1 : -1;
}

static int side(void)
{
    return (fdc.latch >> 2) & 1;
}

static bool single_density(void)
{
    return (fdc.latch & 8) != 0;
}

static drive_t *unit(void)
{
    int selected_drive = selected();
    return selected_drive < 0 ? NULL : &drives[selected_drive];
}

static uint8_t *sector_at(drive_t *drive, int sector)
{
    if (!drive || !drive->mounted || drive->head >= drive->tracks ||
        side() >= drive->sides || sector >= drive->sectors_per_track ||
        single_density() != (drive->sectors_per_track == DFS_SECTORS_PER_TRACK)) {
        return NULL;
    }
    int track = drive->interleaved ? drive->head * drive->sides + side() :
                side() * drive->tracks + drive->head;
    return drive->image + (track * drive->sectors_per_track + sector) * SECTOR_SIZE;
}

static uint8_t *current_sector(void)
{
    drive_t *drive = unit();
    return drive && drive->head == fdc.track ? sector_at(drive, fdc.sector) : NULL;
}

static bool motor(uint64_t now)
{
    return (fdc.status & BUSY) || now < fdc.motor_off;
}

static void finish(uint64_t at, uint8_t result)
{
    fdc.status = (fdc.status & ~(BUSY | DRQ)) | result;
    fdc.op = WAIT;
    fdc.motor_off = at + 9 * REVOLUTION;
}

static void end_at(uint64_t at, uint8_t result)
{
    fdc.op = WAIT;
    fdc.result = result;
    fdc.ready = at;
}

static uint16_t crc_byte(uint16_t crc, uint8_t value)
{
    crc ^= (uint16_t)value << 8;
    for (int bit = 0; bit < 8; bit++) {
        crc = (crc << 1) ^ ((crc & 0x8000) ? 0x1021 : 0);
    }
    return crc;
}

static uint16_t field_crc(uint8_t marker, const uint8_t *data, size_t length)
{
    uint16_t crc = 0xFFFF;
    if (!single_density()) {
        for (int i = 0; i < 3; i++) {
            crc = crc_byte(crc, 0xA1);
        }
    }
    crc = crc_byte(crc, marker);
    for (size_t i = 0; i < length; i++) {
        crc = crc_byte(crc, data[i]);
    }
    return crc;
}

static void store_sector(drive_t *drive, uint8_t *destination, const uint8_t *source)
{
    size_t offset = destination - drive->image;
    if (!drive->dirty_end || offset < drive->dirty_begin) {
        drive->dirty_begin = offset;
    }
    if (drive->dirty_end < offset + SECTOR_SIZE) {
        drive->dirty_end = offset + SECTOR_SIZE;
    }
    memcpy(destination, source, SECTOR_SIZE);
    drive->writes++;
}

static void type1(uint8_t command, uint64_t at)
{
    static const unsigned rates[] = { 6, 12, 20, 30 };
    drive_t *drive = unit();
    unsigned group = command >> 4;
    int delta;
    if (group == 0) {
        delta = drive ? -drive->head : 0;
        fdc.track = 0;
        fdc.step = -1;
    } else if (group == 1) {
        delta = (int)fdc.data - fdc.track;
        fdc.track = fdc.data;
        fdc.step = delta < 0 ? -1 : 1;
    } else {
        if (group >= 4) {
            fdc.step = group >= 6 ? -1 : 1;
        }
        delta = fdc.step;
        if (command & 0x10) {
            fdc.track += delta;
        }
    }
    if (drive) {
        int goal = drive->head + delta;
        drive->head = goal < 0 ? 0 : goal > MAX_TRACK ? MAX_TRACK : goal;
    }
    unsigned steps = abs(delta);
    at += (uint64_t)(steps ? steps : 1) * rates[command & 3] * (ELK_CPU_HZ / 1000);
    bool verify_error = (command & 4) && (!sector_at(drive, 0) || drive->head != fdc.track);
    end_at(at + (verify_error ? NOT_FOUND : 0), verify_error ? RNF : 0);
}

static void begin_transfer(operation_t op, uint64_t at, unsigned length)
{
    fdc.op = op;
    fdc.pos = fdc.track_slots = 0;
    fdc.requested = false;
    fdc.length = length;
    fdc.ready = at;
}

static void sector_command(uint64_t at)
{
    drive_t *drive = unit();
    bool write = (fdc.command & 0x20) != 0;
    uint8_t *data = current_sector();
    if (!data) {
        end_at(at + NOT_FOUND, RNF);
    } else if (write && drive->protect) {
        end_at(at, PROTECT);
    } else {
        if (!write) {
            memcpy(fdc.buffer, data, SECTOR_SIZE);
        }
        begin_transfer(write ? WRITE_SECTOR : READ_SECTOR, at + SEEK_SECTOR, SECTOR_SIZE);
    }
}

static uint8_t *gap(uint8_t *p, uint8_t value, size_t size)
{
    memset(p, value, size);
    return p + size;
}

static uint8_t *field(uint8_t *p, uint8_t marker, const uint8_t *data, size_t length)
{
    p = gap(p, 0x00, single_density() ? 6 : 12);
    if (!single_density()) {
        p = gap(p, 0xA1, 3);
    }
    *p++ = marker;
    memcpy(p, data, length);
    p += length;
    uint16_t crc = field_crc(marker, data, length);
    *p++ = crc >> 8;
    *p++ = crc;
    return p;
}

static void read_track(drive_t *drive)
{
    if (single_density()) {
        uint8_t *p = gap(fdc.buffer, 0xFF, 40);
        p = gap(p, 0, 6);
        *p++ = 0xFC;
        for (int sector = 0; sector < drive->sectors_per_track; sector++) {
            uint8_t id[] = { drive->head, 0, sector, 1 };
            p = gap(p, 0xFF, 12);
            p = field(p, 0xFE, id, sizeof(id));
            p = gap(p, 0xFF, 11);
            p = field(p, 0xFB, sector_at(drive, sector), SECTOR_SIZE);
        }
        gap(p, 0xFF, fdc.buffer + TRACK_BYTES / 2 - p);
        return;
    }
    uint8_t *p = gap(fdc.buffer, 0x4E, 80);
    p = gap(p, 0x00, 12);
    p = gap(p, 0xC2, 3);
    *p++ = 0xFC;
    p = gap(p, 0x4E, 50);
    for (int sector = 0; sector < SECTORS_PER_TRACK; sector++) {
        uint8_t id[] = { drive->head, side(), sector, 1 };
        p = field(p, 0xFE, id, sizeof(id));
        p = gap(p, 0x4E, 22);
        p = field(p, 0xFB, sector_at(drive, sector), SECTOR_SIZE);
        p = gap(p, 0x4E, 40);
    }
    gap(p, 0x4E, fdc.buffer + TRACK_BYTES - p);
}

static void track_command(uint64_t at)
{
    drive_t *drive = unit();
    if (!sector_at(drive, 0)) {
        end_at(at + NOT_FOUND, RNF);
        return;
    }
    switch (fdc.command >> 4) {
    case 0xC: {
        unsigned sector = fdc.id++ % drive->sectors_per_track;
        uint8_t id[] = { drive->head, side(), sector, 1 };
        uint16_t crc = field_crc(0xFE, id, sizeof(id));
        memcpy(fdc.buffer, id, sizeof(id));
        fdc.buffer[4] = crc >> 8;
        fdc.buffer[5] = crc;
        begin_transfer(READ_ADDRESS, at + SEEK_SECTOR, 6);
        break;
    }
    case 0xE:
        read_track(drive);
        begin_transfer(READ_TRACK, at + SEEK_SECTOR, single_density() ? TRACK_BYTES / 2 : TRACK_BYTES);
        break;
    case 0xF:
        if (drive->protect) {
            end_at(at, PROTECT);
        } else {
            begin_transfer(WRITE_TRACK, at + SEEK_SECTOR, single_density() ? TRACK_BYTES / 2 : TRACK_BYTES);
        }
        break;
    default:
        end_at(at, RNF);
        break;
    }
}

/* Write Track accepts FM marks or MFM marks preceded by the F5 sync token.
 * Raw images store ordinary 256-byte sectors from the track data fields.
 */
static bool write_track(drive_t *drive)
{
    unsigned sectors = 0;
    int sector = -1;
    for (unsigned i = 1; i < fdc.pos; i++) {
        if (fdc.buffer[i - 1] != (single_density() ? 0 : 0xF5)) {
            continue;
        }
        if (fdc.buffer[i] == 0xFE && i + 4 < fdc.pos) {
            sector = fdc.buffer[i + 4] == 1 ? fdc.buffer[i + 3] : -1;
            i += 4;
        } else if ((fdc.buffer[i] == 0xFB || fdc.buffer[i] == 0xF8) &&
                   sector >= 0 && i + SECTOR_SIZE < fdc.pos) {
            uint8_t *data = sector_at(drive, sector);
            if (data) {
                store_sector(drive, data, fdc.buffer + i + 1);
                sectors++;
            }
            i += SECTOR_SIZE;
            sector = -1;
        }
    }
    return sectors != 0;
}

static void transfer_done(uint64_t at)
{
    drive_t *drive = unit();
    switch (fdc.op) {
    case WRITE_TRACK:
        finish(at, write_track(drive) ? 0 : RNF);
        return;
    case WRITE_SECTOR:
        if (fdc.status & LOST) {
            finish(at, 0);
            return;
        }
        store_sector(drive, current_sector(), fdc.buffer);
        break;
    case READ_ADDRESS:
        fdc.sector = fdc.buffer[0];
        finish(at, 0);
        return;
    case READ_SECTOR:
    case READ_TRACK:
        drive->reads++;
        break;
    default:
        break;
    }
    if ((fdc.op == READ_SECTOR || fdc.op == WRITE_SECTOR) && (fdc.command & 0x10)) {
        fdc.sector++;
        sector_command(at);
    } else {
        finish(at, 0);
    }
}

void elk_plus3_update(void)
{
    uint64_t now = elk.cpu.cycles;
    unsigned byte_ticks = single_density() ? 2 * BYTE_TICKS : BYTE_TICKS;
    while ((fdc.status & BUSY) && fdc.ready <= now) {
        uint64_t at = fdc.ready;
        if (fdc.op == WAIT) {
            finish(at, fdc.result);
        } else if (fdc.op == WRITE_SECTOR || fdc.op == WRITE_TRACK) {
            if (!fdc.requested) {
                fdc.requested = true;
            } else {
                bool missed = (fdc.status & DRQ) != 0;
                if (missed) {
                    fdc.status |= LOST;
                }
                uint8_t value = missed ? 0 : fdc.data;
                fdc.buffer[fdc.pos++] = value;
                unsigned slots = fdc.op == WRITE_TRACK && value == 0xF7 ? 2 : 1;
                fdc.track_slots += slots;
                if (fdc.track_slots >= fdc.length) {
                    transfer_done(at);
                    continue;
                }
                fdc.ready += (slots - 1) * byte_ticks;
            }
            fdc.status |= DRQ;
            fdc.ready += byte_ticks;
        } else if (fdc.pos == fdc.length) {
            transfer_done(at);
        } else {
            if (fdc.status & DRQ) {
                fdc.status |= LOST;
            }
            fdc.data = fdc.buffer[fdc.pos++];
            fdc.status |= DRQ;
            fdc.ready += fdc.pos == fdc.length ? 3 * byte_ticks : byte_ticks;
        }
    }
}

static uint8_t status(void)
{
    uint8_t value = fdc.status;
    uint64_t now = elk.cpu.cycles;
    bool spinning = motor(now);
    if (spinning) {
        value |= MOTOR;
    }
    if (fdc.type1) {
        const drive_t *drive = unit();
        if (!(value & BUSY) && spinning) {
            value |= SPUN_UP;
        }
        if (drive && drive->head == 0) {
            value |= LOST;               /* Type I: track zero */
        }
        if (drive && drive->mounted && spinning && now % REVOLUTION < ELK_CPU_HZ / 250) {
            value |= DRQ;                /* Type I: index */
        }
        if (drive && drive->mounted && drive->protect) {
            value |= PROTECT;
        }
    }
    return value;
}

uint8_t elk_plus3_read(uint16_t address)
{
    elk_plus3_update();
    if (!(address & 4)) {
        return 0xFF;                     /* the drive latch is write-only */
    }
    switch (address & 3) {
    case 0: return status();
    case 1: return fdc.track;
    case 2: return fdc.sector;
    default:
        fdc.status &= ~DRQ;
        return fdc.data;
    }
}

void elk_plus3_write(uint16_t address, uint8_t value)
{
    elk_plus3_update();
    uint64_t now = elk.cpu.cycles;
    if (!(address & 4)) {
        if (!(value & 0x20)) {
            elk_plus3_reset();
        } else if ((fdc.status & BUSY) && ((fdc.latch ^ value) & 0x0F)) {
            finish(now, RNF);
        }
        fdc.latch = value & 0x3F;
        return;
    }
    switch (address & 3) {
    case 0:
        if ((value & 0xF0) == 0xD0) {
            if (motor(now)) {
                finish(now, 0);
            }
            fdc.status = 0;
            fdc.type1 = true;
            fdc.command = value;
        } else if ((fdc.latch & 0x20) && !(fdc.status & BUSY)) {
            uint64_t at = now;
            if (!motor(now) && !(value & 8)) {
                at += 6 * REVOLUTION;
            }
            if ((value & 0x84) == 0x84) {
                at += 30 * (ELK_CPU_HZ / 1000);
            }
            fdc.command = value;
            fdc.type1 = !(value & 0x80);
            fdc.status = BUSY;
            if (fdc.type1) {
                type1(value, at);
            } else if (!(value & 0x40)) {
                sector_command(at);
            } else {
                track_command(at);
            }
        }
        break;
    case 1:
        if (!(fdc.status & BUSY)) {
            fdc.track = value;
        }
        break;
    case 2:
        if (!(fdc.status & BUSY)) {
            fdc.sector = value;
        }
        break;
    default:
        fdc.data = value;
        fdc.status &= ~DRQ;
        break;
    }
}

bool elk_plus3_init(void)
{
    if (!fdc.buffer) {
        fdc.buffer = ELK_SLOW_ALLOC(TRACK_BYTES);
    }
    return fdc.buffer != NULL;
}

void elk_plus3_reset(void)
{
    fdc.latch = fdc.command = fdc.status = fdc.track = fdc.sector = fdc.data = 0;
    fdc.op = WAIT;
    fdc.motor_off = 0;
    fdc.type1 = true;
    fdc.step = 1;
    fdc.id = 0;
}

static bool flush(drive_t *drive)
{
    if (drive->dirty_end == 0) {
        return true;
    }
    FILE *file = fopen(drive->path, "r+b");
    bool ok = false;
    if (file) {
        /* Materialize the zero-filled gap when writing past a trimmed SSD. */
        size_t start = drive->dirty_begin < drive->file_size ? drive->dirty_begin : drive->file_size;
        size_t length = drive->dirty_end - start;
        ok = fseek(file, start, SEEK_SET) == 0 &&
             fwrite(drive->image + start, 1, length, file) == length;
        ok = fclose(file) == 0 && ok;
    }
    if (ok) {
        if (drive->dirty_end > drive->file_size) {
            drive->file_size = drive->dirty_end;
        }
        drive->dirty_end = 0;
    } else if (!drive->write_error) {
        ELK_LOGW("%s: could not flush ADFS disk", drive->path);
    }
    drive->write_error = !ok;
    return ok;
}

bool elk_disks_dirty(void)
{
    return drives[0].dirty_end || drives[1].dirty_end;
}

bool elk_disks_flush(void)
{
    bool ok = true;
    for (int i = 0; i < ELK_DISK_DRIVES; i++) {
        drive_t *drive = &drives[i];
        if (!flush(drive)) {
            ok = false;
        } else if (!drive->mounted) {
            free(drive->image);
            drive->image = NULL;
        }
    }
    return ok;
}

static bool geometry(const char *path, size_t size, drive_t *drive)
{
    const char *ext = strrchr(path, '.');
    if (!ext) {
        return false;
    }
    if (!strcasecmp(ext, ".ssd")) {
        drive->tracks = size == ELK_DFS_40 ? 40 : 80;
        drive->sides = 1;
        drive->sectors_per_track = DFS_SECTORS_PER_TRACK;
        return size == ELK_DFS_40 || size == ELK_DFS_80;
    }
    drive->tracks = size == ELK_ADFS_SMALL ? 40 : 80;
    drive->sides = size == ELK_ADFS_LARGE ? 2 : 1;
    drive->interleaved = !strcasecmp(ext, ".adl");
    drive->sectors_per_track = SECTORS_PER_TRACK;
    if (size != (size_t)drive->tracks * drive->sides * TRACK_SIZE) {
        return false;
    }
    return !strcasecmp(ext, ".adf") || (drive->interleaved && size == ELK_ADFS_LARGE) ||
           (!strcasecmp(ext, ".ads") && size == ELK_ADFS_SMALL) ||
           (!strcasecmp(ext, ".adm") && size == ELK_ADFS_MEDIUM);
}

static size_t disk_capacity(FILE *file, const char *path, size_t length)
{
    const char *ext = strrchr(path, '.');
    if (!ext || strcasecmp(ext, ".ssd") || length < 2 * SECTOR_SIZE ||
        length >= ELK_DFS_80 || length % SECTOR_SIZE) {
        return length;
    }
    /* Archives often omit unused sectors from the end of an SSD. */
    uint8_t catalogue[2];
    if (fseek(file, SECTOR_SIZE + 6, SEEK_SET) == 0 &&
        fread(catalogue, 1, sizeof(catalogue), file) == sizeof(catalogue)) {
        size_t size = (((catalogue[0] & 3) << 8) | catalogue[1]) * SECTOR_SIZE;
        if (size == ELK_DFS_40 || size == ELK_DFS_80) {
            return size;
        }
    }
    return length;
}

/* Implicit UEF sides are in logical sector order. Older UEFs leave the
 * track count at zero, in which case the catalogue gives the capacity.
 */
static bool load_uef_disk(const char *path, drive_t *drive)
{
    size_t length;
    uint8_t *uef = uef_load(path, &length, NULL);
    if (!uef) {
        return false;
    }
    const uint8_t *info = NULL;
    const uint8_t *sides[2] = { NULL, NULL };
    size_t lengths[2] = { 0, 0 };
    bool valid = true;
    for (size_t offset = UEF_HEADER; offset < length && valid;) {
        unsigned id = uef_le16(uef + offset);
        size_t bytes = uef_le32(uef + offset + 2);
        const uint8_t *data = uef + offset + UEF_CHUNK_HEADER;
        if (id == 0x0200) {
            valid = !info && bytes >= 6 && data[0] == 0 && uef_le16(data + 1) == SECTOR_SIZE;
            info = data;
        } else if (id == 0x0201) {
            unsigned side = bytes ? data[0] >> 7 : 0;
            valid = bytes > 1 && !(data[0] & 0x7F) && !sides[side];
            if (valid) {
                sides[side] = data + 1;
                lengths[side] = bytes - 1;
            }
        } else if (id == 0x0202) {
            valid = false;
        }
        offset += UEF_CHUNK_HEADER + bytes;
    }
    if (!valid || !info || !sides[0]) {
        free(uef);
        return false;
    }
    int sectors = info[3];
    int tracks = info[4];
    int side_count = sides[1] ? 2 : 1;
    if (sectors == SECTORS_PER_TRACK && lengths[0] >= SECTOR_SIZE) {
        unsigned total = uef_le16(sides[0] + 0xFC) | (unsigned)sides[0][0xFE] << 16;
        side_count = total == ELK_ADFS_LARGE / SECTOR_SIZE ? 2 : side_count;
        if (!tracks && (total == 640 || total == 1280 || total == 2560)) {
            tracks = total / (side_count * sectors);
        }
    } else if (sectors == DFS_SECTORS_PER_TRACK && !tracks && lengths[0] >= 2 * SECTOR_SIZE) {
        unsigned total = ((sides[0][0x106] & 3) << 8) | sides[0][0x107];
        if (total == 400 || total == 800) {
            tracks = total / sectors;
        }
    }
    size_t side_size = (size_t)tracks * sectors * SECTOR_SIZE;
    valid = (tracks == 40 || tracks == 80) && lengths[0] <= side_size && lengths[1] <= side_size &&
            ((sectors == SECTORS_PER_TRACK && (side_count == 1 || tracks == 80)) ||
             (sectors == DFS_SECTORS_PER_TRACK && side_count == 1));
    if (valid) {
        drive->size = side_size * side_count;
        drive->image = ELK_SLOW_ALLOC(drive->size);
        valid = drive->image != NULL;
    }
    if (valid) {
        for (int side = 0; side < side_count; side++) {
            if (sides[side]) {
                memcpy(drive->image + side * side_size, sides[side], lengths[side]);
            }
        }
        drive->tracks = tracks;
        drive->sides = side_count;
        drive->sectors_per_track = sectors;
        drive->protect = true;
    }
    free(uef);
    return valid;
}

static bool load_raw_disk(const char *path, drive_t *drive)
{
    FILE *file = fopen(path, "r+b");
    drive->protect = !file;
    if (!file) {
        file = fopen(path, "rb");
    }
    if (!file) {
        return false;
    }
    long length = fseek(file, 0, SEEK_END) == 0 ? ftell(file) : -1;
    drive->size = length > 0 ? disk_capacity(file, path, length) : 0;
    bool ok = length > 0 && (size_t)length <= drive->size && geometry(path, drive->size, drive) &&
              fseek(file, 0, SEEK_SET) == 0;
    if (ok) {
        drive->image = ELK_SLOW_ALLOC(drive->size);
        ok = drive->image && fread(drive->image, 1, length, file) == (size_t)length;
        drive->file_size = length;
    }
    fclose(file);
    return ok;
}

bool elk_disk_insert(int number, const char *path)
{
    if (number < 0 || number >= ELK_DISK_DRIVES ||
        strlen(path) >= sizeof(drives[0].path)) {
        return false;
    }
    drive_t *drive = &drives[number];
    if (!flush(drive)) {
        return false;
    }
    drive_t next = { .head = drive->head };
    const char *ext = strrchr(path, '.');
    bool ok = ext && (!strcasecmp(ext, ".uef") ? load_uef_disk(path, &next) : load_raw_disk(path, &next));
    if (!ok) {
        free(next.image);
        return false;
    }
    if (selected() == number && (fdc.status & BUSY)) {
        finish(elk.cpu.cycles, RNF);
    }
    next.mounted = true;
    strcpy(next.path, path);
    free(drive->image);
    *drive = next;
    return true;
}

void elk_disk_eject(int number)
{
    if (number < 0 || number >= ELK_DISK_DRIVES) {
        return;
    }
    if (selected() == number && (fdc.status & BUSY)) {
        finish(elk.cpu.cycles, RNF);
    }
    drives[number].mounted = false;
    elk_disks_flush();
}

const char *elk_disk_path(int number)
{
    return number >= 0 && number < ELK_DISK_DRIVES && drives[number].mounted ?
           drives[number].path : NULL;
}

void elk_disk_status(int number, elk_disk_status_t *out)
{
    *out = (elk_disk_status_t) { 0 };
    if (number < 0 || number >= ELK_DISK_DRIVES) {
        return;
    }
    const drive_t *drive = &drives[number];
    out->size = drive->mounted ? drive->size : 0;
    out->tracks = drive->tracks;
    out->sides = drive->sides;
    out->head = drive->head;
    out->protect = drive->protect;
    out->write_error = drive->write_error;
    out->dfs = drive->mounted && drive->sectors_per_track == DFS_SECTORS_PER_TRACK;
    out->reads = drive->reads;
    out->writes = drive->writes;
    out->motor = (elk.adfs || elk.cfg.dfs) && selected() == number && motor(elk.cpu.cycles);
}

void elk_plus3_status(elk_plus3_status_t *out)
{
    *out = (elk_plus3_status_t) {
        .enabled = elk.adfs || elk.cfg.dfs, .drive = selected(), .side = side(),
        .track = fdc.track, .sector = fdc.sector, .command = fdc.command, .status = status(),
    };
}

static void put24(uint8_t *p, uint32_t value)
{
    p[0] = value;
    p[1] = value >> 8;
    p[2] = value >> 16;
}

static uint8_t map_checksum(const uint8_t *sector)
{
    /* ADFS 1.00 adds from byte 254 down to zero with A=$FF and C=0. */
    unsigned sum = 0xFF;
    for (int i = 254; i >= 0; i--) {
        sum = (sum & 0xFF) + (sum >> 8) + sector[i];
    }
    return sum;
}

static void format_adfs(uint8_t *initial, unsigned sectors, const char *path)
{
    enum { ROOT_SECTOR = 2, FIRST_FREE = 7 };
    put24(initial, FIRST_FREE);
    put24(initial + 0xFC, sectors);
    put24(initial + 0x100, sectors - FIRST_FREE);
    /* Distinguish freshly created images when ADFS validates its cached map. */
    uint16_t id = (uint16_t)time(NULL);
    for (const uint8_t *p = (const uint8_t *)path; *p; p++) {
        id = (uint16_t)(id * 31 + *p);
    }
    initial[0x1FB] = id;
    initial[0x1FC] = id >> 8;
    initial[0x1FE] = 3;
    initial[0xFF] = map_checksum(initial);
    initial[0x1FF] = map_checksum(initial + SECTOR_SIZE);

    uint8_t *root = initial + ROOT_SECTOR * SECTOR_SIZE;
    memcpy(root + 1, "Hugo", 4);
    root[0x4CC] = root[0x4D9] = '$';
    put24(root + 0x4D6, ROOT_SECTOR);
    memcpy(root + 0x4FB, "Hugo", 4);
}

bool elk_disk_create(const char *path, uint32_t size)
{
    drive_t drive = { 0 };
    if (!geometry(path, size, &drive)) {
        return false;
    }
    uint8_t initial[7 * SECTOR_SIZE] = { 0 };
    unsigned sectors = size / SECTOR_SIZE;
    if (drive.sectors_per_track == DFS_SECTORS_PER_TRACK) {
        initial[0x106] = sectors >> 8;
        initial[0x107] = sectors;
    } else {
        format_adfs(initial, sectors, path);
    }

    FILE *file = fopen(path, "wb");
    if (!file) {
        return false;
    }
    bool ok = fwrite(initial, 1, sizeof(initial), file) == sizeof(initial);
    static const uint8_t zeros[TRACK_SIZE];
    for (size_t pos = sizeof(initial); ok && pos < size;) {
        size_t bytes = size - pos < sizeof(zeros) ? size - pos : sizeof(zeros);
        ok = fwrite(zeros, 1, bytes, file) == bytes;
        pos += bytes;
    }
    ok = fclose(file) == 0 && ok;
    if (!ok) {
        remove(path);
    }
    return ok;
}
