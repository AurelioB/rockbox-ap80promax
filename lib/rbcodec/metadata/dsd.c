/***************************************************************************
 * Metadata parser for DSF and uncompressed DSDIFF.
 ***************************************************************************/

#include <string.h>
#include <unistd.h>

#include "metadata.h"
#include "metadata_common.h"
#include "metadata_parsers.h"

static bool read_exact(int fd, void *buf, size_t size)
{
    return read(fd, buf, size) == (ssize_t)size;
}

static bool read_id3v2_at(int fd, struct mp3entry *id3, off_t offset,
                          uint64_t available)
{
    uint8_t header[10];
    uint32_t body_size;
    uint64_t tag_size;

    if (offset < 0 || available < sizeof(header) ||
        lseek(fd, offset, SEEK_SET) < 0 ||
        !read_exact(fd, header, sizeof(header)) ||
        memcmp(header, "ID3", 3) != 0 ||
        header[3] < 2 || header[3] > 4 ||
        (header[6] | header[7] | header[8] | header[9]) & 0x80)
        return false;

    body_size = ((uint32_t)header[6] << 21) |
                ((uint32_t)header[7] << 14) |
                ((uint32_t)header[8] << 7) |
                header[9];
    tag_size = sizeof(header) + body_size;
    if (tag_size > available)
        return false;

    id3->id3v2len = tag_size;
    if (lseek(fd, offset, SEEK_SET) < 0)
        return false;

    setid3v2title(fd, id3);
    return id3->id3version != 0;
}

static bool finish_metadata(struct mp3entry *id3, unsigned int rate,
                            unsigned int channels, uint64_t samples,
                            off_t data_offset)
{
    if (rate == 0 || channels != 2 || samples == 0)
        return false;

    id3->frequency = rate;
    id3->samples = samples;
    id3->length = samples * 1000 / rate;
    id3->bitrate = (uint64_t)rate * channels / 1000;
    id3->first_frame_offset = data_offset;
    id3->vbr = false;
    return true;
}

static bool get_dsf_metadata(int fd, struct mp3entry *id3)
{
    uint8_t header[52];
    uint64_t fmt_size;
    uint64_t samples;
    uint32_t channels;
    uint32_t rate;
    off_t data_offset;
    off_t file_size;
    uint64_t metadata_offset;

    if (lseek(fd, 0, SEEK_SET) < 0 || !read_exact(fd, header, 28) ||
        memcmp(header, "DSD ", 4) != 0 || get_uint64_le(header + 4) < 28)
        return false;

    metadata_offset = get_uint64_le(header + 20);

    if (!read_exact(fd, header, sizeof(header)) ||
        memcmp(header, "fmt ", 4) != 0)
        return false;

    fmt_size = get_uint64_le(header + 4);
    channels = get_long_le(header + 24);
    rate = get_long_le(header + 28);
    samples = get_uint64_le(header + 36);

    if (fmt_size < sizeof(header) ||
        (get_long_le(header + 32) != 1 &&
         get_long_le(header + 32) != 8))
        return false;

    data_offset = 28 + fmt_size;
    if (lseek(fd, data_offset, SEEK_SET) < 0 ||
        !read_exact(fd, header, 12) || memcmp(header, "data", 4) != 0 ||
        get_uint64_le(header + 4) < 12)
        return false;

    file_size = filesize(fd);
    id3->filesize = file_size;
    if (!finish_metadata(id3, rate, channels, samples, data_offset + 12))
        return false;

    if (metadata_offset != 0 && metadata_offset < (uint64_t)file_size)
        read_id3v2_at(fd, id3, metadata_offset,
                      (uint64_t)file_size - metadata_offset);

    return true;
}

static bool parse_dff_prop(int fd, uint64_t prop_size,
                           unsigned int *rate, unsigned int *channels,
                           bool *uncompressed, struct mp3entry *id3)
{
    uint8_t header[16];
    uint64_t left;

    if (prop_size < 4 || !read_exact(fd, header, 4) ||
        memcmp(header, "SND ", 4) != 0)
        return false;

    left = prop_size - 4;
    while (left >= 12)
    {
        uint64_t size;
        off_t payload;

        if (!read_exact(fd, header, 12))
            return false;

        size = ((uint64_t)get_long_be(header + 4) << 32) |
               get_long_be(header + 8);
        payload = lseek(fd, 0, SEEK_CUR);
        if (payload < 0 || size + (size & 1) > left - 12)
            return false;

        if (memcmp(header, "FS  ", 4) == 0 && size >= 4)
        {
            if (!read_exact(fd, header, 4))
                return false;
            *rate = get_long_be(header);
        }
        else if (memcmp(header, "CHNL", 4) == 0 && size >= 2)
        {
            if (!read_exact(fd, header, 2))
                return false;
            *channels = get_short_be(header);
        }
        else if (memcmp(header, "CMPR", 4) == 0 && size >= 4)
        {
            if (!read_exact(fd, header, 4))
                return false;
            *uncompressed = memcmp(header, "DSD ", 4) == 0;
        }
        else if (memcmp(header, "ID3 ", 4) == 0)
        {
            read_id3v2_at(fd, id3, payload, size);
        }

        if (lseek(fd, payload + size + (size & 1), SEEK_SET) < 0)
            return false;
        left -= 12 + size + (size & 1);
    }

    return true;
}

static bool get_dff_metadata(int fd, struct mp3entry *id3)
{
    uint8_t header[16];
    unsigned int rate = 0;
    unsigned int channels = 0;
    bool uncompressed = false;
    uint64_t data_size = 0;
    off_t data_offset = 0;
    off_t file_size = filesize(fd);

    if (lseek(fd, 0, SEEK_SET) < 0 ||
        !read_exact(fd, header, sizeof(header)) ||
        memcmp(header, "FRM8", 4) != 0 ||
        memcmp(header + 12, "DSD ", 4) != 0)
        return false;

    while (lseek(fd, 0, SEEK_CUR) <= file_size - 12)
    {
        uint64_t size;
        off_t payload;

        if (!read_exact(fd, header, 12))
            break;

        size = ((uint64_t)get_long_be(header + 4) << 32) |
               get_long_be(header + 8);
        payload = lseek(fd, 0, SEEK_CUR);
        if (payload < 0 || size > (uint64_t)(file_size - payload))
            return false;

        if (memcmp(header, "PROP", 4) == 0)
        {
            if (!parse_dff_prop(fd, size, &rate, &channels, &uncompressed,
                                id3))
                return false;
        }
        else if (memcmp(header, "ID3 ", 4) == 0)
        {
            read_id3v2_at(fd, id3, payload, size);
        }
        else if (memcmp(header, "DSD ", 4) == 0)
        {
            data_offset = payload;
            data_size = size;
        }
        else if (memcmp(header, "DST ", 4) == 0)
        {
            return false;
        }

        if (lseek(fd, payload + size + (size & 1), SEEK_SET) < 0)
            return false;
    }

    id3->filesize = file_size;
    return uncompressed && data_size && finish_metadata(
            id3, rate, channels, data_size * 8 / channels, data_offset);
}

bool get_dsd_metadata(int fd, struct mp3entry *id3)
{
    uint8_t magic[4];

    if (lseek(fd, 0, SEEK_SET) < 0 || !read_exact(fd, magic, sizeof(magic)))
        return false;

    if (memcmp(magic, "DSD ", 4) == 0)
        return get_dsf_metadata(fd, id3);
    if (memcmp(magic, "FRM8", 4) == 0)
        return get_dff_metadata(fd, id3);
    return false;
}
