#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void put_le32(uint8_t *p, uint32_t value)
{
    p[0] = value;
    p[1] = value >> 8;
    p[2] = value >> 16;
    p[3] = value >> 24;
}

static void put_le64(uint8_t *p, uint64_t value)
{
    put_le32(p, value);
    put_le32(p + 4, value >> 32);
}

static void put_be16(uint8_t *p, uint16_t value)
{
    p[0] = value >> 8;
    p[1] = value;
}

static void put_be32(uint8_t *p, uint32_t value)
{
    p[0] = value >> 24;
    p[1] = value >> 16;
    p[2] = value >> 8;
    p[3] = value;
}

static void put_be64(uint8_t *p, uint64_t value)
{
    put_be32(p, value >> 32);
    put_be32(p + 4, value);
}

static void write_or_die(FILE *file, const void *data, size_t size)
{
    if (fwrite(data, 1, size, file) != size)
    {
        perror("fwrite");
        exit(1);
    }
}

static void make_dsf(const char *path)
{
    uint8_t header[92] = { 0 };
    uint8_t block[4096];
    FILE *file = fopen(path, "wb");
    if (!file)
    {
        perror(path);
        exit(1);
    }

    memcpy(header, "DSD ", 4);
    put_le64(header + 4, 28);
    put_le64(header + 12, 8284);

    memcpy(header + 28, "fmt ", 4);
    put_le64(header + 32, 52);
    put_le32(header + 40, 1);       /* format version */
    put_le32(header + 48, 2);       /* stereo channel type */
    put_le32(header + 52, 2);       /* channels */
    put_le32(header + 56, 2822400); /* DSD64 */
    put_le32(header + 60, 1);       /* LSB-first */
    put_le64(header + 64, 32);      /* samples per channel */
    put_le32(header + 72, 4096);

    memcpy(header + 80, "data", 4);
    put_le64(header + 84, 8204);
    write_or_die(file, header, sizeof(header));

    memset(block, 0x96, sizeof(block)); /* LSB-first DSD silence */
    block[0] = 0x80;
    block[1] = 0x40;
    block[2] = 0x20;
    block[3] = 0x10;
    write_or_die(file, block, sizeof(block));

    memset(block, 0x96, sizeof(block));
    block[0] = 0x08;
    block[1] = 0x04;
    block[2] = 0x02;
    block[3] = 0x01;
    write_or_die(file, block, sizeof(block));
    fclose(file);
}

static void make_dff(const char *path)
{
    static const uint8_t samples[] =
        { 0x01, 0x10, 0x02, 0x20, 0x04, 0x40, 0x08, 0x80 };
    uint8_t data[108] = { 0 };
    size_t pos = 0;
    FILE *file = fopen(path, "wb");
    if (!file)
    {
        perror(path);
        exit(1);
    }

    memcpy(data + pos, "FRM8", 4); pos += 4;
    put_be64(data + pos, 96); pos += 8;
    memcpy(data + pos, "DSD ", 4); pos += 4;

    memcpy(data + pos, "PROP", 4); pos += 4;
    put_be64(data + pos, 60); pos += 8;
    memcpy(data + pos, "SND ", 4); pos += 4;

    memcpy(data + pos, "FS  ", 4); pos += 4;
    put_be64(data + pos, 4); pos += 8;
    put_be32(data + pos, 2822400); pos += 4;

    memcpy(data + pos, "CHNL", 4); pos += 4;
    put_be64(data + pos, 10); pos += 8;
    put_be16(data + pos, 2); pos += 2;
    memcpy(data + pos, "SLFTSRGT", 8); pos += 8;

    memcpy(data + pos, "CMPR", 4); pos += 4;
    put_be64(data + pos, 5); pos += 8;
    memcpy(data + pos, "DSD ", 4); pos += 4;
    data[pos++] = 0;
    data[pos++] = 0; /* even-byte chunk padding */

    memcpy(data + pos, "DSD ", 4); pos += 4;
    put_be64(data + pos, sizeof(samples)); pos += 8;
    memcpy(data + pos, samples, sizeof(samples)); pos += sizeof(samples);

    if (pos != sizeof(data))
    {
        fprintf(stderr, "internal DFF fixture size error: %zu\n", pos);
        exit(1);
    }

    write_or_die(file, data, sizeof(data));
    fclose(file);
}

int main(int argc, char **argv)
{
    if (argc != 3)
    {
        fprintf(stderr, "usage: %s OUTPUT.dsf OUTPUT.dff\n", argv[0]);
        return 2;
    }

    make_dsf(argv[1]);
    make_dff(argv[2]);
    return 0;
}
