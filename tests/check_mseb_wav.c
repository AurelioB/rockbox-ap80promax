#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static uint16_t get_u16(const unsigned char *value)
{
    return value[0] | (uint16_t)value[1] << 8;
}

int main(int argc, char **argv)
{
    if (argc != 2)
        return 1;

    FILE *file = fopen(argv[1], "rb");
    if (file == NULL)
        return 1;

    unsigned char header[46];
    if (fread(header, 1, sizeof(header), file) != sizeof(header) ||
        get_u16(header + 20) != 1 || get_u16(header + 22) != 2 ||
        get_u16(header + 34) != 16)
    {
        fclose(file);
        return 1;
    }

    /* Skip the first quarter so recursive filters have fully settled. */
    if (fseek(file, 0, SEEK_END) != 0)
        return 1;
    long data_bytes = ftell(file) - (long)sizeof(header);
    long skip_bytes = (data_bytes / 4) & ~3L;
    if (data_bytes <= 0 || fseek(file, sizeof(header) + skip_bytes,
                                 SEEK_SET) != 0)
        return 1;

    long double sum_squares = 0.0;
    unsigned long samples = 0;
    unsigned char encoded[2];
    while (fread(encoded, 1, sizeof(encoded), file) == sizeof(encoded))
    {
        int16_t sample = (int16_t)get_u16(encoded);
        sum_squares += (long double)sample * sample;
        samples++;
    }

    if (fclose(file) != 0 || samples == 0)
        return 1;

    printf("%.9Lf\n", sqrtl(sum_squares / samples));
    return 0;
}
