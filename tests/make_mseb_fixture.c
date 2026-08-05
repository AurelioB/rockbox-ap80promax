#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void put_u16(FILE *file, unsigned int value)
{
    fputc(value, file);
    fputc(value >> 8, file);
}

static void put_u32(FILE *file, uint32_t value)
{
    fputc(value, file);
    fputc(value >> 8, file);
    fputc(value >> 16, file);
    fputc(value >> 24, file);
}

int main(int argc, char **argv)
{
    enum { SAMPLE_RATE = 44100, FRAMES = 16384, CHANNELS = 2 };

    if (argc < 2 || argc > 4)
        return 1;

    bool make_signal = argc >= 3;
    bool make_noise = make_signal && strcmp(argv[2], "noise") == 0;
    double frequency = make_signal && !make_noise ? atof(argv[2]) : 0.0;
    int amplitude = argc >= 4 ? atoi(argv[3]) : 4096;

    if (amplitude < 1 || amplitude > 32767 || frequency < 0.0 ||
        frequency >= SAMPLE_RATE / 2.0)
        return 1;

    FILE *file = fopen(argv[1], "wb");
    if (file == NULL)
        return 1;

    const uint32_t data_size = FRAMES * CHANNELS * sizeof(int16_t);
    fwrite("RIFF", 1, 4, file);
    put_u32(file, 36 + data_size);
    fwrite("WAVEfmt ", 1, 8, file);
    put_u32(file, 16);
    put_u16(file, 1);
    put_u16(file, CHANNELS);
    put_u32(file, SAMPLE_RATE);
    put_u32(file, SAMPLE_RATE * CHANNELS * sizeof(int16_t));
    put_u16(file, CHANNELS * sizeof(int16_t));
    put_u16(file, 16);
    fwrite("data", 1, 4, file);
    put_u32(file, data_size);

    uint32_t noise_state = 0x4d534542;
    for (int i = 0; i < FRAMES; i++)
    {
        int16_t sample;

        if (make_noise)
        {
            noise_state = noise_state * 1664525u + 1013904223u;
            int value = (int)((noise_state >> 16) & 0xffffu) - 32768;
            sample = (int16_t)(value * amplitude / 32768);
        }
        else if (make_signal)
        {
            const double pi = 3.14159265358979323846;
            sample = (int16_t)lrint(amplitude *
                          sin(2.0 * pi * frequency * i / SAMPLE_RATE));
        }
        else
        {
            /* A low-level impulse leaves ample room for every extreme profile
             * and exercises the complete filter response. */
            sample = i == 64 ? 4096 : 0;
        }

        put_u16(file, (uint16_t)sample);
        put_u16(file, (uint16_t)sample);
    }

    return fclose(file) == 0 ? 0 : 1;
}
