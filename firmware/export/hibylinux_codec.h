#ifndef __HIBYLINUX_CODEC__
#define __HIBYLINUX_CODEC__

#ifdef HAVE_RECORDING
#define AUDIOHW_CAPS (FILTER_ROLL_OFF_CAP | MIC_GAIN_CAP)
#else
#define AUDIOHW_CAPS (FILTER_ROLL_OFF_CAP)
#endif
AUDIOHW_SETTING(VOLUME, "dB", 1, 5, -102*10,  0, -30*10)
#endif

//#define AUDIOHW_MUTE_ON_STOP
#define AUDIOHW_MUTE_ON_SRATE_CHANGE
//#define AUDIOHW_NEEDS_INITIAL_UNMUTE

AUDIOHW_SETTING(FILTER_ROLL_OFF, "", 0, 1, 0, 4, 0)
#define AUDIOHW_HAVE_SHORT2_ROLL_OFF

#ifdef HAVE_RECORDING
/*
 * The stock digital-microphone path has no verified gain control. A fixed
 * zero-width setting keeps Rockbox's recorder UI and sound API consistent.
 */
AUDIOHW_SETTING(MIC_GAIN, "dB", 0, 1, 0, 0, 0)
#endif

void audiohw_mute(int mute);
void hiby_set_output(int ps);
int hiby_get_outputs(void);

#ifdef HAVE_HIBY_OUTPUT_SELECT
enum hiby_output_mode
{
    HIBY_OUTPUT_AUTO = 0,
    HIBY_OUTPUT_LINEOUT,
    HIBY_OUTPUT_SPDIF,
    HIBY_OUTPUT_NUM_MODES
};

int audiohw_set_output_mode(int mode);
#endif
