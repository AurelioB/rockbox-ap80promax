/***************************************************************************
 * Uncompressed stereo DSF/DSDIFF codec with direct DSD output and a
 * DSD-to-PCM fallback.
 ***************************************************************************/

#include "codeclib.h"
#include "dsd.h"
#include "dsd2pcm.h"

CODEC_HEADER

#define DSF_BLOCK_MAX 4096u
#define DSD_IO_SIZE   8192u
#define DSD_SILENCE   0x69

enum dsd_container
{
    DSD_CONTAINER_DSF,
    DSD_CONTAINER_DFF,
};

enum dsd_play_result
{
    DSD_PLAY_OK = 0,
    DSD_PLAY_HALT = -1,
    DSD_PLAY_ERROR = -2,
};

enum dsd_output
{
    DSD_OUTPUT_DIRECT,
    DSD_OUTPUT_PCM,
};

struct dsd_file
{
    enum dsd_container container;
    unsigned int rate;
    unsigned int channels;
    uint64_t samples;
    off_t data_offset;
    uint64_t data_size;

    /* DSF only */
    unsigned int block_size;
    bool lsb_first;
};

static uint8_t channel_l[DSF_BLOCK_MAX];
static uint8_t channel_r[DSF_BLOCK_MAX];
static uint8_t io_buffer[DSD_IO_SIZE + 4];
static int32_t pcm_buffer[DSD_IO_SIZE / 2];

static uint16_t get_be16(const uint8_t *p)
{
    return ((uint16_t)p[0] << 8) | p[1];
}

static uint32_t get_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint32_t get_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

static uint64_t get_le64(const uint8_t *p)
{
    return (uint64_t)get_le32(p) | ((uint64_t)get_le32(p + 4) << 32);
}

static uint64_t get_be64(const uint8_t *p)
{
    return ((uint64_t)get_be32(p) << 32) | get_be32(p + 4);
}

static size_t read_at(off_t offset, void *buffer, size_t size)
{
    if (!ci->seek_buffer(offset))
        return 0;
    return ci->read_filebuf(buffer, size);
}

static bool parse_dsf(struct dsd_file *file)
{
    uint8_t header[52];
    uint64_t fmt_size;
    uint32_t bits_per_sample;

    if (read_at(0, header, 28) != 28 ||
        memcmp(header, "DSD ", 4) != 0 || get_le64(header + 4) < 28)
        return false;

    if (read_at(28, header, sizeof(header)) != sizeof(header) ||
        memcmp(header, "fmt ", 4) != 0)
        return false;

    fmt_size = get_le64(header + 4);
    bits_per_sample = get_le32(header + 32);
    if (fmt_size < sizeof(header) ||
        (bits_per_sample != 1 && bits_per_sample != 8))
        return false;

    file->container = DSD_CONTAINER_DSF;
    file->channels = get_le32(header + 24);
    file->rate = get_le32(header + 28);
    file->samples = get_le64(header + 36);
    file->block_size = get_le32(header + 44);
    file->lsb_first = bits_per_sample == 1;

    if (file->channels != 2 || file->rate == 0 || file->samples == 0 ||
        file->block_size == 0 || file->block_size > DSF_BLOCK_MAX)
        return false;

    file->data_offset = 28 + fmt_size;
    if (read_at(file->data_offset, header, 12) != 12 ||
        memcmp(header, "data", 4) != 0 || get_le64(header + 4) < 12)
        return false;

    file->data_size = get_le64(header + 4) - 12;
    file->data_offset += 12;
    return true;
}

static bool parse_dff_prop(struct dsd_file *file, off_t offset, uint64_t size,
                           bool *uncompressed)
{
    uint8_t header[12];
    off_t pos;
    off_t end;

    if (size < 4 || read_at(offset, header, 4) != 4 ||
        memcmp(header, "SND ", 4) != 0)
        return false;

    pos = offset + 4;
    end = offset + size;
    while (pos <= end - 12)
    {
        uint64_t chunk_size;
        off_t payload;

        if (read_at(pos, header, 12) != 12)
            return false;

        chunk_size = get_be64(header + 4);
        payload = pos + 12;
        if (chunk_size + (chunk_size & 1) > (uint64_t)(end - payload))
            return false;

        if (memcmp(header, "FS  ", 4) == 0 && chunk_size >= 4)
        {
            if (read_at(payload, header, 4) != 4)
                return false;
            file->rate = get_be32(header);
        }
        else if (memcmp(header, "CHNL", 4) == 0 && chunk_size >= 2)
        {
            if (read_at(payload, header, 2) != 2)
                return false;
            file->channels = get_be16(header);
        }
        else if (memcmp(header, "CMPR", 4) == 0 && chunk_size >= 4)
        {
            if (read_at(payload, header, 4) != 4)
                return false;
            *uncompressed = memcmp(header, "DSD ", 4) == 0;
        }

        pos = payload + chunk_size + (chunk_size & 1);
    }

    return true;
}

static bool parse_dff(struct dsd_file *file)
{
    uint8_t header[16];
    off_t pos = 16;
    bool uncompressed = false;

    if (read_at(0, header, sizeof(header)) != sizeof(header) ||
        memcmp(header, "FRM8", 4) != 0 ||
        memcmp(header + 12, "DSD ", 4) != 0)
        return false;

    file->container = DSD_CONTAINER_DFF;
    while (pos <= ci->filesize - 12)
    {
        uint64_t size;
        off_t payload;

        if (read_at(pos, header, 12) != 12)
            return false;

        size = get_be64(header + 4);
        payload = pos + 12;
        if (size > (uint64_t)(ci->filesize - payload))
            return false;

        if (memcmp(header, "PROP", 4) == 0)
        {
            if (!parse_dff_prop(file, payload, size, &uncompressed))
                return false;
        }
        else if (memcmp(header, "DSD ", 4) == 0)
        {
            file->data_offset = payload;
            file->data_size = size;
        }
        else if (memcmp(header, "DST ", 4) == 0)
        {
            return false;
        }

        pos = payload + size + (size & 1);
    }

    if (!uncompressed || file->channels != 2 || file->rate == 0 ||
        file->data_size == 0 || (file->data_size & 1))
        return false;

    file->samples = file->data_size * 8 / file->channels;
    return true;
}

static bool parse_file(struct dsd_file *file)
{
    uint8_t magic[4];

    memset(file, 0, sizeof(*file));
    if (read_at(0, magic, sizeof(magic)) != sizeof(magic))
        return false;

    if (memcmp(magic, "DSD ", 4) == 0)
        return parse_dsf(file);
    if (memcmp(magic, "FRM8", 4) == 0)
        return parse_dff(file);
    return false;
}

static uint8_t reverse_byte(uint8_t value)
{
    value = (value >> 4) | (value << 4);
    value = ((value & 0xcc) >> 2) | ((value & 0x33) << 2);
    return ((value & 0xaa) >> 1) | ((value & 0x55) << 1);
}

static int check_command(uint64_t *byte_position, const struct dsd_file *file,
                         enum dsd_output output,
                         struct dsd2pcm_state *converter)
{
    intptr_t param = 0;
    long action = ci->get_command(&param);

    if (action == CODEC_ACTION_HALT)
        return DSD_PLAY_HALT;

    if (action != CODEC_ACTION_SEEK_TIME)
        return 0;

    if (output == DSD_OUTPUT_DIRECT)
        ci->dsd_close(false);

    uint64_t per_channel = (uint64_t)param * file->rate / 8000;
    uint64_t max_per_channel = (file->samples + 7) / 8;
    if (per_channel > max_per_channel)
        per_channel = max_per_channel;

    /* Both AP80 transports consume two successive bytes per channel. */
    per_channel &= ~(uint64_t)1;
    *byte_position = file->container == DSD_CONTAINER_DSF ?
                     per_channel : per_channel * file->channels;

    ci->set_elapsed(per_channel * 8000 / file->rate);
    ci->seek_complete();

    if (output == DSD_OUTPUT_DIRECT)
    {
        if (ci->dsd_configure(file->rate, file->channels) ==
            DSD_TRANSPORT_ERROR)
            return DSD_PLAY_ERROR;
    }
    else if (!dsd2pcm_init(converter, file->rate))
    {
        return DSD_PLAY_ERROR;
    }

    return 1;
}

static bool write_audio(uint8_t *data, size_t size, enum dsd_output output,
                        struct dsd2pcm_state *converter)
{
    if (output == DSD_OUTPUT_DIRECT)
    {
        size_t padded_size = size;
        while (padded_size & 3)
            data[padded_size++] = DSD_SILENCE;
        return ci->dsd_write(data, padded_size) == (ssize_t)padded_size;
    }

    size_t frames = dsd2pcm_convert(converter, data, size, pcm_buffer,
                                    ARRAYLEN(pcm_buffer) / 2);
    if (frames > 0)
        ci->pcmbuf_insert(pcm_buffer, NULL, frames);
    return true;
}

static int play_dsf(const struct dsd_file *file, uint64_t *byte_position,
                    enum dsd_output output,
                    struct dsd2pcm_state *converter)
{
    uint64_t bytes_per_channel = (file->samples + 7) / 8;

    while (*byte_position < bytes_per_channel)
    {
        int command = check_command(byte_position, file, output, converter);
        if (command < 0)
            return command;
        if (command > 0)
            continue;

        uint64_t block_index = *byte_position / file->block_size;
        size_t skip = *byte_position % file->block_size;
        size_t valid = MIN((uint64_t)(file->block_size - skip),
                           bytes_per_channel - *byte_position);
        off_t block = file->data_offset +
                      block_index * file->block_size * file->channels;

        if (read_at(block, channel_l, file->block_size) != file->block_size ||
            read_at(block + file->block_size, channel_r,
                    file->block_size) != file->block_size)
            return DSD_PLAY_ERROR;

        for (size_t i = 0; i < valid; ++i)
        {
            uint8_t left = channel_l[skip + i];
            uint8_t right = channel_r[skip + i];
            if (file->lsb_first)
            {
                left = reverse_byte(left);
                right = reverse_byte(right);
            }
            io_buffer[i * 2] = left;
            io_buffer[i * 2 + 1] = right;
        }

        if (!write_audio(io_buffer, valid * 2, output, converter))
            return DSD_PLAY_ERROR;

        *byte_position += valid;
        ci->set_elapsed(*byte_position * 8000 / file->rate);
        ci->set_offset(block + skip + valid);
    }

    return DSD_PLAY_OK;
}

static int play_dff(const struct dsd_file *file, uint64_t *byte_position,
                    enum dsd_output output,
                    struct dsd2pcm_state *converter)
{
    while (*byte_position < file->data_size)
    {
        int command = check_command(byte_position, file, output, converter);
        if (command < 0)
            return command;
        if (command > 0)
            continue;

        size_t valid = MIN((uint64_t)DSD_IO_SIZE,
                           file->data_size - *byte_position);
        if (read_at(file->data_offset + *byte_position,
                    io_buffer, valid) != valid)
            return DSD_PLAY_ERROR;

        if (!write_audio(io_buffer, valid, output, converter))
            return DSD_PLAY_ERROR;

        *byte_position += valid;
        uint64_t per_channel = *byte_position / file->channels;
        ci->set_elapsed(per_channel * 8000 / file->rate);
        ci->set_offset(file->data_offset + *byte_position);
    }

    return DSD_PLAY_OK;
}

enum codec_status codec_main(enum codec_entry_call_reason reason)
{
    if (reason == CODEC_LOAD)
        ci->configure(DSP_SET_SAMPLE_DEPTH, 28);
    return CODEC_OK;
}

enum codec_status codec_run(void)
{
    struct dsd_file file;
    struct dsd2pcm_state converter;
    enum dsd_output output;
    uint64_t byte_position;

    if (codec_init() || !parse_file(&file))
        return CODEC_ERROR;

    uint64_t per_channel =
        (uint64_t)ci->id3->elapsed * file.rate / 8000;
    uint64_t max_per_channel = (file.samples + 7) / 8;
    if (per_channel > max_per_channel)
        per_channel = max_per_channel;
    per_channel &= ~(uint64_t)1;
    byte_position = file.container == DSD_CONTAINER_DSF ?
                    per_channel : per_channel * file.channels;

    do
    {
        enum dsd_transport transport = DSD_TRANSPORT_ERROR;
        if (ci->dsd_configure && ci->dsd_write && ci->dsd_close)
            transport = ci->dsd_configure(file.rate, file.channels);

        if (transport != DSD_TRANSPORT_ERROR)
        {
            output = DSD_OUTPUT_DIRECT;
            DEBUGF("DSD: %u Hz, %s\n", file.rate,
                   transport == DSD_TRANSPORT_DOP ? "DoP" : "native");
        }
        else
        {
            if (!dsd2pcm_init(&converter, file.rate))
                return CODEC_ERROR;

            output = DSD_OUTPUT_PCM;
            ci->configure(DSP_SET_FREQUENCY, DSD2PCM_OUTPUT_RATE);
            ci->configure(DSP_SET_STEREO_MODE, STEREO_INTERLEAVED);
            DEBUGF("DSD: %u Hz, PCM fallback at %u Hz\n", file.rate,
                   DSD2PCM_OUTPUT_RATE);
        }

        int result = file.container == DSD_CONTAINER_DSF ?
                     play_dsf(&file, &byte_position, output, &converter) :
                     play_dff(&file, &byte_position, output, &converter);
        if (result == DSD_PLAY_HALT)
        {
            if (output == DSD_OUTPUT_DIRECT)
                ci->dsd_close(false);
            return CODEC_OK;
        }
        if (result == DSD_PLAY_ERROR)
        {
            if (output == DSD_OUTPUT_DIRECT)
                ci->dsd_close(false);
            return CODEC_ERROR;
        }

        if (!ci->loop_track())
        {
            if (output == DSD_OUTPUT_DIRECT)
                ci->dsd_close(true);
            return CODEC_OK;
        }

        if (output == DSD_OUTPUT_DIRECT)
            ci->dsd_close(false);
        byte_position = 0;
        ci->set_elapsed(0);
    }
    while (true);
}
