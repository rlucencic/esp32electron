/*
 * Host build of the Electron core, for testing on the PC:
 *
 *   elk_host <os_basic.ic2> [frames] [out.ppm]
 *
 * Environment:
 *   ELK_ADFS=<rom>          a Plus 3 with that ADFS ROM
 *   ELK_PLUS1=<rom>         the Plus 1's 4 KiB Expansion 1.00 ROM
 *   ELK_SK1, ELK_SK2        cartridge images in the two Plus 1 sockets
 *   ELK_DISK0=<path>        an ADFS disk in drive 0 (writes persist)
 *   ELK_DISK1=<path>        an ADFS disk in drive 1
 *   ELK_NEWDISK=<path>      create a 320 KiB disk and mount it in drive 0
 *   ELK_LOG=<n>             the CPU's state every n frames
 *   ELK_SHOTS=<n>           a picture every n frames (out-NNNNN.ppm)
 *   ELK_STOP=<pc>[:<frame>] stop at pc (hex), from that frame on: the
 *                           instructions before and the registers
 *   ELK_TRACE=<pc>:<n>      from pc on, the next n instructions
 *   ELK_KEYS=<frame>:<key>:<0|1>[;...]  matrix keys (column * 4 + row)
 *   ELK_BREAK=<frame>       BREAK pressed for two frames
 *   ELK_TYPE=<frame>:<text>[|...]  type text from that frame (\n is RETURN)
 *   ELK_TAPE=<file.uef>     mount a raw or gzip-compressed UEF image
 *   ELK_TAPE_FAST=1         shorten carriers and gaps
 *   ELK_WAV=<file>          the sound
 *   ELK_DUMP=<addr>:<len>[;...]  memory in hex at the end
 *   ELK_ZP=<addr>           with the registers, 16 bytes of zero page from there
 *   ELK_WATCH=<addr>[:<frame>]  a CPU write to addr: the instructions before
 *   ELK_DASM=<addr>:<count> disassemble there (ROM page 10: BASIC) and exit
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "electron.h"
#include "elk_internal.h"
#include "m6502.h"

static uint32_t s_frame;

static void disassemble(uint16_t pc)
{
    char buf[64];
    m6502_disassemble(&elk.cpu, pc, buf, sizeof(buf));
    printf("  %04X  %s\n", pc, buf);
}

static void registers(void)
{
    const m6502_t *c = &elk.cpu;
    printf("  A %02X X %02X Y %02X S %02X P %02X PC %04X  ROM page %u  mode %u  CAPS LK %s  cycles %llu\n", c->a, c->x,
           c->y, c->s, c->p, c->pc, elk.rom_page, elk.mode, (elk.control & 0x80) ? "on" : "off",
           (unsigned long long)c->cycles);
    if (getenv("ELK_ZP")) {
        unsigned from = strtoul(getenv("ELK_ZP"), NULL, 16);
        printf("  %02X:", from);
        for (unsigned i = 0; i < 16; i++) {
            printf(" %02X", elk.ram[(from + i) & 0xFF]);
        }
        printf("\n");
    }
}

#define RING_SIZE 4096
static uint16_t ring[RING_SIZE];
static uint32_t ring_pos;

void m6502_host_hook(m6502_t *c)
{
    uint16_t pc = c->pc;
    ring[ring_pos++ % RING_SIZE] = pc;

    static long stop_pc = -2;
    static uint32_t stop_after;
    if (stop_pc == -2) {
        const char *e = getenv("ELK_STOP");
        stop_pc = e ? (long)strtoul(e, NULL, 16) : -1;
        stop_after = e && strchr(e, ':') ? atoi(strchr(e, ':') + 1) : 0;
    }
    if (stop_pc >= 0 && pc == stop_pc && s_frame >= stop_after) {
        printf("-- reached %04X at frame %lu after:\n", pc, (unsigned long)s_frame);
        for (uint32_t i = ring_pos - 60; i != ring_pos; i++) {
            disassemble(ring[i % RING_SIZE]);
        }
        registers();
        exit(0);
    }

    static long trace_pc = -2;
    static int trace_left;
    if (trace_pc == -2) {
        const char *e = getenv("ELK_TRACE");
        trace_pc = e ? (long)strtoul(e, NULL, 16) : -1;
        trace_left = e && strchr(e, ':') ? atoi(strchr(e, ':') + 1) : 0;
    }
    if (trace_left > 0 && (trace_pc < 0 || pc == trace_pc)) {
        trace_pc = -1;
        trace_left--;
        disassemble(pc);
    }
}

void elk_host_write(uint16_t a, uint8_t v)
{
    static long watch = -2;
    static uint32_t watch_after;
    if (watch == -2) {
        const char *e = getenv("ELK_WATCH");
        watch = e ? (long)strtoul(e, NULL, 16) : -1;
        watch_after = e && strchr(e, ':') ? atoi(strchr(e, ':') + 1) : 0;
    }
    if (watch >= 0 && a == watch && s_frame >= watch_after) {
        printf("-- write %02X to %04X at frame %lu, after:\n", v, a, (unsigned long)s_frame);
        for (uint32_t i = ring_pos - 30; i != ring_pos; i++) {
            disassemble(ring[i % RING_SIZE]);
        }
        registers();
    }
}

static void write_ppm(const char *path)
{
    FILE *f = fopen(path, "wb");
    if (f == NULL) {
        perror(path);
        return;
    }
    fprintf(f, "P6\n%d %d\n255\n", ELK_SCREEN_WIDTH, ELK_SCREEN_HEIGHT);
    uint16_t line[ELK_SCREEN_WIDTH];
    for (int y = 0; y < ELK_SCREEN_HEIGHT; y++) {
        elk_render_line_rgb565(y, line);
        for (int x = 0; x < ELK_SCREEN_WIDTH; x++) {
            uint16_t c = line[x];
            uint8_t rgb[3] = { (uint8_t)((c >> 11) << 3), (uint8_t)(((c >> 5) & 0x3F) << 2), (uint8_t)((c & 0x1F) << 3) };
            fwrite(rgb, 1, 3, f);
        }
    }
    fclose(f);
}

/* ELK_KEYS: frame:key:down;... */
static void keys_for_frame(uint32_t frame)
{
    const char *e = getenv("ELK_KEYS");
    while (e && *e) {
        unsigned f, key, down;
        if (sscanf(e, "%u:%u:%u", &f, &key, &down) == 3 && f == frame) {
            elk_key(key, down);
        }
        e = strchr(e, ';');
        e = e ? e + 1 : NULL;
    }
}

/* ELK_TYPE: frame:text|... (\n in the text is RETURN) */
static void type_for_frame(uint32_t frame)
{
    const char *e = getenv("ELK_TYPE");
    while (e && *e) {
        char *colon;
        unsigned long f = strtoul(e, &colon, 10);
        const char *end = strchr(e, '|');
        if (*colon == ':' && f == frame) {
            char text[256];
            size_t n = 0;
            for (const char *s = colon + 1; *s && s != end && n < sizeof(text) - 1; s++) {
                if (s[0] == '\\' && s[1] == 'n') {
                    text[n++] = '\n';
                    s++;
                } else {
                    text[n++] = *s;
                }
            }
            text[n] = 0;
            elk_type(text);
        }
        e = end ? end + 1 : NULL;
    }
}

static void wav_header(FILE *f, uint32_t frames)
{
    uint32_t bytes = frames * 4, rate = ELK_AUDIO_HZ, byte_rate = rate * 4;
    uint16_t pcm = 1, channels = 2, align = 4, bits = 16;
    uint32_t riff = 36 + bytes, fmt_len = 16;
    fwrite("RIFF", 1, 4, f);
    fwrite(&riff, 4, 1, f);
    fwrite("WAVEfmt ", 1, 8, f);
    fwrite(&fmt_len, 4, 1, f);
    fwrite(&pcm, 2, 1, f);
    fwrite(&channels, 2, 1, f);
    fwrite(&rate, 4, 1, f);
    fwrite(&byte_rate, 4, 1, f);
    fwrite(&align, 2, 1, f);
    fwrite(&bits, 2, 1, f);
    fwrite("data", 1, 4, f);
    fwrite(&bytes, 4, 1, f);
}

static uint8_t *load(const char *path, size_t size)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        perror(path);
        exit(1);
    }
    uint8_t *buf = calloc(1, size);
    size_t n = fread(buf, 1, size, f);
    fclose(f);
    if (n != size) {
        fprintf(stderr, "%s: %zu bytes, not %zu\n", path, n, size);
        exit(1);
    }
    return buf;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s <os_basic.ic2> [frames] [out.ppm]\n", argv[0]);
        return 1;
    }
    uint32_t frames = argc > 2 ? strtoul(argv[2], NULL, 10) : 300;
    const char *out = argc > 3 ? argv[3] : "screen.ppm";

    elk_config_t cfg = { 0 };
    cfg.os_basic = load(argv[1], ELK_OS_BASIC_SIZE);
    cfg.ram = calloc(1, ELK_RAM_SIZE);
    if (getenv("ELK_ADFS")) {
        cfg.adfs = load(getenv("ELK_ADFS"), ELK_PAGED_ROM_SIZE);
    }
    if (getenv("ELK_PLUS1")) {
        cfg.plus1 = load(getenv("ELK_PLUS1"), ELK_PLUS1_ROM_SIZE);
    }
    const char *cartridges[] = { getenv("ELK_SK1"), getenv("ELK_SK2") };
    for (int slot = 0; slot < ELK_CART_SLOTS; slot++) {
        if (cartridges[slot] && !elk_cart_insert(slot, cartridges[slot])) {
            fprintf(stderr, "%s: invalid or unreadable cartridge\n", cartridges[slot]);
            return 1;
        }
    }
    if (!elk_init(&cfg)) {
        fprintf(stderr, "elk_init failed\n");
        return 1;
    }

    const char *new_disk = getenv("ELK_NEWDISK");
    if (new_disk && !elk_disk_create(new_disk, ELK_ADFS_MEDIUM)) {
        fprintf(stderr, "%s: could not create an ADFS disk\n", new_disk);
        return 1;
    }
    const char *images[] = { new_disk ? new_disk : getenv("ELK_DISK0"), getenv("ELK_DISK1") };
    for (int drive = 0; drive < ELK_DISK_DRIVES; drive++) {
        if (images[drive] && !elk_disk_insert(drive, images[drive])) {
            fprintf(stderr, "%s: invalid or unreadable ADFS disk\n", images[drive]);
            return 1;
        }
    }

    const char *tape = getenv("ELK_TAPE");
    if (tape && !elk_tape_insert(tape)) {
        fprintf(stderr, "%s: invalid or unreadable UEF image\n", tape);
        return 1;
    }
    elk_tape_fast(getenv("ELK_TAPE_FAST") != NULL);

    if (getenv("ELK_DASM")) {
        unsigned addr = 0, count = 20;
        sscanf(getenv("ELK_DASM"), "%x:%u", &addr, &count);
        for (unsigned i = 0; i < count; i++) {
            char buf[64];
            int len = m6502_disassemble(&elk.cpu, addr, buf, sizeof(buf));
            printf("  %04X  %s\n", addr, buf);
            addr += len;
        }
        return 0;
    }
    int log_every = getenv("ELK_LOG") ? atoi(getenv("ELK_LOG")) : 0;
    int shots = getenv("ELK_SHOTS") ? atoi(getenv("ELK_SHOTS")) : 0;
    FILE *wav = getenv("ELK_WAV") ? fopen(getenv("ELK_WAV"), "wb") : NULL;
    uint32_t wav_frames = 0;
    if (wav) {
        wav_header(wav, 0);
    }
    for (s_frame = 0; s_frame < frames; s_frame++) {
        keys_for_frame(s_frame);
        if (getenv("ELK_BREAK")) {
            uint32_t at = strtoul(getenv("ELK_BREAK"), NULL, 10);
            if (s_frame == at || s_frame == at + 2) {
                elk_break(s_frame == at);
            }
        }
        type_for_frame(s_frame);
        elk_run_frame();
        if (wav) {
            int16_t buf[2048];
            int n;
            while ((n = elk_audio_render(buf, 1024)) > 0) {
                fwrite(buf, 4, n, wav);
                wav_frames += n;
            }
        } else {
            elk_audio_enable(false);
        }
        if (log_every && s_frame % log_every == 0) {
            printf("frame %u:", s_frame);
            registers();
        }
        if (shots && s_frame % shots == 0) {
            char name[300];
            snprintf(name, sizeof(name), "%.*s-%05u.ppm", (int)(strlen(out) - 4), out, s_frame);
            write_ppm(name);
        }
    }
    if (wav) {
        fseek(wav, 0, SEEK_SET);
        wav_header(wav, wav_frames);
        fclose(wav);
    }
    const char *d = getenv("ELK_DUMP");
    while (d && *d) {
        unsigned addr, len;
        if (sscanf(d, "%x:%u", &addr, &len) == 2) {
            for (unsigned i = 0; i < len; i++) {
                uint16_t at = addr + i;
                printf("%s%02X", i % 16 ? " " : (i ? "\n" : ""), at < 0x8000 ? elk.ram[at] : m6502_peek(&elk.cpu, at));
            }
            printf("\n");
        }
        d = strchr(d, ';');
        d = d ? d + 1 : NULL;
    }
    if (!elk_disks_flush()) {
        return 1;
    }
    printf("after %u frames: PC %04X\n", frames, elk.cpu.pc);
    write_ppm(out);
    return 0;
}
