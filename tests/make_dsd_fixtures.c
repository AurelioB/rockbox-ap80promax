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

static void put_synchsafe32(uint8_t *p, uint32_t value)
{
    p[0] = (value >> 21) & 0x7f;
    p[1] = (value >> 14) & 0x7f;
    p[2] = (value >> 7) & 0x7f;
    p[3] = value & 0x7f;
}

static void write_or_die(FILE *file, const void *data, size_t size)
{
    if (fwrite(data, 1, size, file) != size)
    {
        perror("fwrite");
        exit(1);
    }
}

static size_t append_id3_frame(uint8_t *tag, size_t pos, int version,
                               const char *name, const uint8_t *payload,
                               size_t payload_size)
{
    memcpy(tag + pos, name, 4);
    if (version == 4)
        put_synchsafe32(tag + pos + 4, payload_size);
    else
        put_be32(tag + pos + 4, payload_size);
    tag[pos + 8] = 0;
    tag[pos + 9] = 0;
    memcpy(tag + pos + 10, payload, payload_size);
    return pos + 10 + payload_size;
}

static size_t append_id3_text(uint8_t *tag, size_t pos, int version,
                              const char *name, const char *value)
{
    uint8_t payload[128];
    size_t length = strlen(value);

    payload[0] = 0; /* ISO-8859-1 */
    memcpy(payload + 1, value, length);
    return append_id3_frame(tag, pos, version, name, payload, length + 1);
}

static size_t make_id3(uint8_t *tag, int version, const char *title,
                       const char *track)
{
    static const uint8_t jpeg[] = { 0xff, 0xd8, 0xff, 0xd9 };
    uint8_t picture[64];
    size_t picture_size = 0;
    size_t pos = 10;

    memcpy(tag, "ID3", 3);
    tag[3] = version;
    tag[4] = 0;
    tag[5] = 0;

    pos = append_id3_text(tag, pos, version, "TIT2", title);
    pos = append_id3_text(tag, pos, version, "TPE1", "Rockbox");
    pos = append_id3_text(tag, pos, version, "TALB", "DSD Tests");
    pos = append_id3_text(tag, pos, version,
                          version == 4 ? "TDRC" : "TYER", "2026");
    pos = append_id3_text(tag, pos, version, "TRCK", track);

    picture[picture_size++] = 0; /* ISO-8859-1 */
    memcpy(picture + picture_size, "image/jpeg", 11);
    picture_size += 11;
    picture[picture_size++] = 3; /* front cover */
    picture[picture_size++] = 0; /* empty description */
    memcpy(picture + picture_size, jpeg, sizeof(jpeg));
    picture_size += sizeof(jpeg);
    pos = append_id3_frame(tag, pos, version, "APIC",
                           picture, picture_size);

    put_synchsafe32(tag + 6, pos - 10);
    return pos;
}

static void make_dsf(const char *path)
{
    uint8_t header[92] = { 0 };
    uint8_t block[4096];
    uint8_t tag[512] = { 0 };
    size_t tag_size = make_id3(tag, 4, "DSF Metadata", "1");
    FILE *file = fopen(path, "wb");
    if (!file)
    {
        perror(path);
        exit(1);
    }

    memcpy(header, "DSD ", 4);
    put_le64(header + 4, 28);
    put_le64(header + 12, 8284 + tag_size);
    put_le64(header + 20, 8284);

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
    write_or_die(file, tag, tag_size);
    fclose(file);
}

static void make_dff(const char *path)
{
    static const uint8_t samples[] =
        { 0x01, 0x10, 0x02, 0x20, 0x04, 0x40, 0x08, 0x80 };
    uint8_t data[1024] = { 0 };
    uint8_t tag[512] = { 0 };
    size_t tag_size = make_id3(tag, 3, "DFF Metadata", "2");
    size_t pos = 0;
    FILE *file = fopen(path, "wb");
    if (!file)
    {
        perror(path);
        exit(1);
    }

    memcpy(data + pos, "FRM8", 4); pos += 4;
    pos += 8; /* form size is filled after all chunks are written */
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

    memcpy(data + pos, "ID3 ", 4); pos += 4;
    put_be64(data + pos, tag_size); pos += 8;
    memcpy(data + pos, tag, tag_size); pos += tag_size;
    if (tag_size & 1)
        data[pos++] = 0;

    memcpy(data + pos, "DSD ", 4); pos += 4;
    put_be64(data + pos, sizeof(samples)); pos += 8;
    memcpy(data + pos, samples, sizeof(samples)); pos += sizeof(samples);

    put_be64(data + 4, pos - 12);

    write_or_die(file, data, pos);
    fclose(file);
}

static void make_flac(const char *path)
{
    static const uint8_t jpeg[] = { 0xff, 0xd8, 0xff, 0xd9 };
    uint8_t data[128] = { 0 };
    uint8_t *streaminfo;
    size_t picture_start;
    size_t pos = 0;
    FILE *file = fopen(path, "wb");
    if (!file)
    {
        perror(path);
        exit(1);
    }

    memcpy(data + pos, "fLaC", 4); pos += 4;

    data[pos++] = 0; /* STREAMINFO, not the last metadata block */
    data[pos++] = 0;
    data[pos++] = 0;
    data[pos++] = 34;
    streaminfo = data + pos;
    put_be16(streaminfo, 16);
    put_be16(streaminfo + 2, 16);
    streaminfo[10] = 0x0a; /* 44.1 kHz, stereo, 16 bit */
    streaminfo[11] = 0xc4;
    streaminfo[12] = 0x42;
    streaminfo[13] = 0xf0;
    put_be32(streaminfo + 14, 44100);
    pos += 34;

    data[pos++] = 0x86; /* PICTURE, last metadata block */
    picture_start = pos;
    pos += 3; /* picture block size is filled below */
    put_be32(data + pos, 3); pos += 4; /* front cover */
    put_be32(data + pos, 10); pos += 4;
    memcpy(data + pos, "image/jpeg", 10); pos += 10;
    put_be32(data + pos, 0); pos += 4; /* empty description */
    put_be32(data + pos, 1); pos += 4; /* width */
    put_be32(data + pos, 1); pos += 4; /* height */
    put_be32(data + pos, 24); pos += 4; /* color depth */
    put_be32(data + pos, 0); pos += 4; /* indexed colors */
    put_be32(data + pos, sizeof(jpeg)); pos += 4;
    memcpy(data + pos, jpeg, sizeof(jpeg)); pos += sizeof(jpeg);

    data[picture_start] = ((pos - picture_start - 3) >> 16) & 0xff;
    data[picture_start + 1] = ((pos - picture_start - 3) >> 8) & 0xff;
    data[picture_start + 2] = (pos - picture_start - 3) & 0xff;

    write_or_die(file, data, pos);
    fclose(file);
}

int main(int argc, char **argv)
{
    if (argc != 4)
    {
        fprintf(stderr, "usage: %s OUTPUT.dsf OUTPUT.dff OUTPUT.flac\n",
                argv[0]);
        return 2;
    }

    make_dsf(argv[1]);
    make_dff(argv[2]);
    make_flac(argv[3]);
    return 0;
}
