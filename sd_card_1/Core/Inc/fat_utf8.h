#ifndef FAT_UTF8_H
#define FAT_UTF8_H
#include "ff.h"
#include <stdint.h>
#include <stddef.h>

/* Convert valid UTF-8 paths into FatFs UTF-16. Reject malformed or oversized paths. */
static inline int fat_path_utf16(const char *source, TCHAR *dest, size_t capacity)
{
    size_t count = 0;
    while (*source) {
        uint32_t code = (uint8_t)*source++;
        unsigned extra = 0;
        uint32_t minimum = 0;
        if (code >= 0xF0 && code <= 0xF4) { code &= 7; extra = 3; minimum = 0x10000; }
        else if (code >= 0xE0 && code <= 0xEF) { code &= 15; extra = 2; minimum = 0x800; }
        else if (code >= 0xC2 && code <= 0xDF) { code &= 31; extra = 1; minimum = 0x80; }
        else if (code >= 0x80) return 0;
        while (extra--) {
            uint8_t next = (uint8_t)*source++;
            if ((next & 0xC0) != 0x80) return 0;
            code = (code << 6) | (next & 63);
        }
        if (code < minimum || code > 0x10FFFF || (code >= 0xD800 && code <= 0xDFFF)) return 0;
        if (code > 0xFFFF) {
            if (count + 2 >= capacity) return 0;
            code -= 0x10000;
            dest[count++] = (TCHAR)(0xD800 | (code >> 10));
            dest[count++] = (TCHAR)(0xDC00 | (code & 1023));
        } else {
            if (count + 1 >= capacity) return 0;
            dest[count++] = (TCHAR)code;
        }
    }
    dest[count] = 0;
    return 1;
}
static inline int fat_name_utf8(const TCHAR *source, char *dest, size_t capacity)
{
    size_t count = 0;
    while (*source) {
        uint32_t code = *source++;
        if (code >= 0xD800 && code <= 0xDBFF) {
            uint32_t low = *source;
            if (low >= 0xDC00 && low <= 0xDFFF) {
                ++source;
                code = 0x10000 + ((code - 0xD800) << 10) + low - 0xDC00;
            } else code = 0xFFFD;
        } else if (code >= 0xDC00 && code <= 0xDFFF) code = 0xFFFD;
        unsigned length = code < 0x80 ? 1 : code < 0x800 ? 2 : code < 0x10000 ? 3 : 4;
        if (count + length >= capacity) { dest[count] = 0; return 0; }
        if (length == 1) dest[count++] = (char)code;
        else {
            dest[count++] = (char)((length == 2 ? 0xC0 : length == 3 ? 0xE0 : 0xF0) |
                                  (code >> (6 * (length - 1))));
            for (unsigned shift = length - 1; shift > 0; --shift)
                dest[count++] = (char)(0x80 | ((code >> (6 * (shift - 1))) & 63));
        }
    }
    dest[count] = 0;
    return 1;
}
#endif
