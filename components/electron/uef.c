#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef ESP_PLATFORM
#include "miniz.h"
#else
#include <zlib.h>
#endif

#include "elk_internal.h"
#include "uef.h"

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
        start += 2 + uef_le16(in + start);
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
        if (end - start < 2 || (uint16_t)image_crc(in, start) != uef_le16(in + start)) {
            return NULL;
        }
        start += 2;
    }
    size_t size = uef_le32(in + end + 4);
    if (start >= end || size < UEF_HEADER || size > UEF_MAX_SIZE) {
        return NULL;
    }
    uint8_t *out = ELK_SLOW_ALLOC(size);
    if (!out || !inflate_raw(in + start, end - start, out, size) ||
        image_crc(out, size) != uef_le32(in + end)) {
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
        if (size - offset < UEF_CHUNK_HEADER) {
            return false;
        }
        uint32_t length = uef_le32(image + offset + 2);
        offset += UEF_CHUNK_HEADER;
        if (length > size - offset) {
            return false;
        }
        offset += length;
    }
    return true;
}

uint8_t *uef_load(const char *path, size_t *image_size, bool *compressed)
{
    FILE *file = fopen(path, "rb");
    if (!file) {
        return NULL;
    }
    long size = fseek(file, 0, SEEK_END) ? -1 : ftell(file);
    uint8_t *image = size >= UEF_HEADER && size <= UEF_MAX_SIZE && !fseek(file, 0, SEEK_SET)
                     ? ELK_SLOW_ALLOC(size) : NULL;
    bool ok = image && fread(image, 1, size, file) == (size_t)size;
    fclose(file);
    size_t length = ok ? size : 0;
    bool gzip = ok && image[0] == 0x1F && image[1] == 0x8B;
    if (gzip) {
        uint8_t *raw = gunzip(image, &length);
        free(image);
        image = raw;
    }
    if (!ok || !image || !valid_uef(image, length)) {
        free(image);
        return NULL;
    }
    *image_size = length;
    if (compressed) {
        *compressed = gzip;
    }
    return image;
}
