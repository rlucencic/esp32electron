#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define UEF_HEADER 12
#define UEF_CHUNK_HEADER 6
#define UEF_MAX_SIZE (4u * 1024 * 1024)

static inline uint16_t uef_le16(const uint8_t *p)
{
    return p[0] | (uint16_t)p[1] << 8;
}

static inline uint32_t uef_le32(const uint8_t *p)
{
    return uef_le16(p) | (uint32_t)uef_le16(p + 2) << 16;
}

/* Load a raw or gzip UEF and validate all chunk bounds. The caller owns the buffer. */
uint8_t *uef_load(const char *path, size_t *image_size, bool *compressed);
