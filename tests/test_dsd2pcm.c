#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "dsd2pcm.h"

#define INPUT_BYTES 8192
#define MAX_FRAMES  2048

static uint8_t input[INPUT_BYTES];
static int32_t output_a[MAX_FRAMES * 2];
static int32_t output_b[MAX_FRAMES * 2];

static size_t expected_frames(unsigned int rate, size_t bytes)
{
    return bytes * 2 / (rate / (DSD2PCM_OUTPUT_RATE * 2));
}

static size_t convert_chunks(struct dsd2pcm_state *state, unsigned int rate)
{
    static const size_t chunks[] = { 2, 14, 66, 510, 1024, 2048, 4528 };
    size_t input_offset = 0;
    size_t output_offset = 0;

    assert(dsd2pcm_init(state, rate));
    for (unsigned int i = 0; i < sizeof(chunks) / sizeof(chunks[0]); ++i)
    {
        size_t frames = dsd2pcm_convert(state, input + input_offset, chunks[i],
                                        output_b + output_offset * 2,
                                        MAX_FRAMES - output_offset);
        input_offset += chunks[i];
        output_offset += frames;
    }

    assert(input_offset == INPUT_BYTES);
    return output_offset;
}

static void test_rate(unsigned int rate)
{
    struct dsd2pcm_state state;
    struct dsd2pcm_state chunked;
    size_t wanted = expected_frames(rate, INPUT_BYTES);

    for (size_t i = 0; i < INPUT_BYTES; i += 2)
    {
        input[i] = (uint8_t)(i * 17 + 0x69);
        input[i + 1] = (uint8_t)(i * 29 + 0x96);
    }

    assert(dsd2pcm_init(&state, rate));
    size_t frames = dsd2pcm_convert(&state, input, sizeof(input),
                                    output_a, MAX_FRAMES);
    assert(frames == wanted);
    assert(convert_chunks(&chunked, rate) == wanted);
    assert(memcmp(output_a, output_b, wanted * 2 * sizeof(output_a[0])) == 0);
}

static void test_levels(void)
{
    struct dsd2pcm_state state;
    size_t frames;

    memset(input, 0xff, sizeof(input));
    assert(dsd2pcm_init(&state, 2822400));
    frames = dsd2pcm_convert(&state, input, sizeof(input),
                             output_a, MAX_FRAMES);
    assert(frames == MAX_FRAMES);
    assert(output_a[(frames - 1) * 2] > 250000000);
    assert(output_a[(frames - 1) * 2 + 1] > 250000000);

    memset(input, 0x00, sizeof(input));
    assert(dsd2pcm_init(&state, 2822400));
    frames = dsd2pcm_convert(&state, input, sizeof(input),
                             output_a, MAX_FRAMES);
    assert(output_a[(frames - 1) * 2] < -250000000);
    assert(output_a[(frames - 1) * 2 + 1] < -250000000);

    for (size_t i = 0; i < sizeof(input); i += 2)
    {
        input[i] = 0xff;
        input[i + 1] = 0x00;
    }
    assert(dsd2pcm_init(&state, 2822400));
    frames = dsd2pcm_convert(&state, input, sizeof(input),
                             output_a, MAX_FRAMES);
    assert(output_a[(frames - 1) * 2] > 250000000);
    assert(output_a[(frames - 1) * 2 + 1] < -250000000);
}

int main(void)
{
    struct dsd2pcm_state state;

    assert(!dsd2pcm_init(&state, 0));
    assert(!dsd2pcm_init(&state, 1411200));
    assert(!dsd2pcm_init(&state, 22579200)); /* DSD512 */
    assert(!dsd2pcm_init(&state, 3000000));

    test_rate(2822400);
    test_rate(5644800);
    test_rate(11289600);
    test_levels();

    puts("DSD-to-PCM tests passed");
    return 0;
}
