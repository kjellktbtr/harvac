/* fmt.c -- FAT16 name/date/time formatting for HarvaC HDK.
 * Extracted from apps/ncd/fs.c (format_name, format_name12, format_date,
 * format_time, is_dot_entry). Uses the shared string helpers. */

#include "types.h"
#include "string.h"
#include "harva.h"

void fat_format_name(const fat_dirent_t *ent, char *out)
{
    uint16_t i;
    int j = 0;

    for (i = 0; i < 8 && ent->name[i] != ' '; i++)
        out[j++] = (char)ent->name[i];
    if (ent->ext[0] != ' ') {
        out[j++] = '.';
        for (i = 0; i < 3 && ent->ext[i] != ' '; i++)
            out[j++] = (char)ent->ext[i];
    }
    out[j] = '\0';
}

void fat_format_name12(const char *name, char *out)
{
    uint16_t i;
    uint16_t dot = 0xFFFF;

    for (i = 0; name[i]; i++) {
        if (name[i] == '.')
            dot = i;
    }

    for (i = 0; i < 12; i++)
        out[i] = ' ';
    out[12] = '\0';

    if (dot == 0xFFFF || dot > 8) {
        for (i = 0; name[i] && i < 12; i++)
            out[i] = name[i];
    } else {
        for (i = 0; i < dot; i++)
            out[i] = name[i];
        for (i = 0; name[dot + 1 + i] && i < 3; i++)
            out[9 + i] = name[dot + 1 + i];
    }
}

void fat_format_date(uint16_t date, char *out)
{
    uint16_t i;

    if (date == 0) {
        for (i = 0; i < 10; i++)
            out[i] = ' ';
        out[10] = '\0';
        return;
    }

    /* FAT16 date: bits 0-4=day, 5-8=month, 9-15=year offset from 1980 */
    u32toa((uint32_t)((date >> 9) + 1980), out);   /* 4-digit year */
    out[4] = '.';
    utoa_pad2((date >> 5) & 0x0F, out + 5);
    out[7] = '.';
    utoa_pad2(date & 0x1F, out + 8);
    out[10] = '\0';
}

void fat_format_time(uint16_t date, uint16_t time, char *out)
{
    uint16_t i;

    if (date == 0 && time == 0) {
        for (i = 0; i < 5; i++)
            out[i] = ' ';
        out[5] = '\0';
        return;
    }

    /* FAT16 time: bits 0-4=sec/2, 5-10=minute, 11-15=hour */
    utoa_pad2(time >> 11, out);
    out[2] = ':';
    utoa_pad2((time >> 5) & 0x3F, out + 3);
    out[5] = '\0';
}

int fat_is_dot_entry(const fat_dirent_t *ent)
{
    /* "." entry: name[0]=='.' and rest spaces */
    if (ent->name[0] == '.' && ent->name[1] == ' ')
        return 1;
    /* ".." entry */
    if (ent->name[0] == '.' && ent->name[1] == '.' && ent->name[2] == ' ')
        return 1;
    return 0;
}

/* Middle-truncate src to width chars: "very/long/path" -> "very/...path"
 * out must be at least width+1 bytes. */
void fmt_ellipsis_mid(char *out, const char *src, uint16_t width)
{
    uint16_t slen = (uint16_t)strlen(src);
    uint16_t i;

    if (slen <= width) {
        for (i = 0; i < slen; i++) out[i] = src[i];
        out[slen] = '\0';
        return;
    }
    if (width <= 3) {
        /* Degenerate: just dots */
        for (i = 0; i < width; i++) out[i] = '.';
        out[width] = '\0';
        return;
    }
    {
        uint16_t head = (uint16_t)((width - 3) / 2);
        uint16_t tail = (uint16_t)(width - 3 - head);
        uint16_t tail_start = slen - tail;
        for (i = 0; i < head; i++) out[i] = src[i];
        out[head]     = '.';
        out[head + 1] = '.';
        out[head + 2] = '.';
        for (i = 0; i < tail; i++) out[head + 3 + i] = src[tail_start + i];
        out[width] = '\0';
    }
}

/* Right-align val in a field of width ASCII chars; pad with spaces. */
void fmt_uint_field(char *out, uint32_t val, uint16_t width)
{
    char tmp[12];
    uint16_t len, pad, i;
    u32toa(val, tmp);
    len = (uint16_t)strlen(tmp);
    pad = (len < width) ? (uint16_t)(width - len) : 0;
    for (i = 0; i < pad; i++) out[i] = ' ';
    for (i = 0; i < len && pad + i < width; i++) out[pad + i] = tmp[i];
    out[width] = '\0';
}
