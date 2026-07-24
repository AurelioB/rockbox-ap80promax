/***************************************************************************
 * Native DSD output contract.
 *
 * Codecs pass interleaved, MSB-first stereo DSD bytes:
 *     L0 R0 L1 R1 ...
 ***************************************************************************/

#ifndef ROCKBOX_DSD_H
#define ROCKBOX_DSD_H

#include <stddef.h>
#include <sys/types.h>

enum dsd_transport
{
    DSD_TRANSPORT_ERROR  = -1,
    DSD_TRANSPORT_DOP    = 1,
    DSD_TRANSPORT_NATIVE = 2,
};

#define DSD_RATE_64       2822400u
#define DSD_RATE_128      5644800u
#define DSD_RATE_256     11289600u
#define DSD_RATE_512     22579200u

#endif /* ROCKBOX_DSD_H */
