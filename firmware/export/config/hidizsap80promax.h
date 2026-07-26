/*
 * This config file is for the Hidizs AP80 Pro Max, based on the
 * Ingenic X1600E SoC and HiByOS hosted Linux platform.
 */

/* For Rolo and boot loader */
#define MODEL_NUMBER 125
#define MODEL_NAME   "HIDIZS AP80 PRO MAX"

#define PIVOT_ROOT "/data/mnt/sd_0"
#define MULTIDRIVE_DIR "/data/mnt/usb"

/*
 * The framebuffer scanout reports 480 columns, but the panel, stock UI, and
 * touchscreen all expose a 360x640 logical display.  Drawing a 480-pixel UI
 * makes the rightmost 120 columns invisible on the physical device.
 */
#define LCD_WIDTH 360
#define LCD_HEIGHT 640
#define LCD_DPI 249
#ifndef SIMULATOR
/*
 * Keep the vendor framebuffer mode untouched.  Rockbox renders a 360-pixel
 * logical canvas into the visible part of the 480-pixel scanout.
 */
#define LCD_FBDEV_WIDTH 480
#define LCD_FBDEV_HEIGHT 640
#endif
/* Keep touch-row sizing representative in the SDL simulator. */
#define SIMULATOR_LCD_DPI 249

#define TOUCHSCREEN_RAW_WIDTH 360
#define TOUCHSCREEN_RAW_HEIGHT 640

#define HAVE_LCD_SLEEP
#define LCD_SLEEP_TIMEOUT (2*HZ)

#define LCD_DEPTH 16
#define LCD_PIXELFORMAT RGB565

#define CPU_FREQ 1008000000

#ifndef SIMULATOR
#define HAVE_GENERAL_PURPOSE_LED
#endif

#define HAVE_QUICKSCREEN
#define HAVE_HOTKEY

#define HAVE_HEADPHONE_DETECTION
#define NO_BUTTON_LR

#ifndef BOOTLOADER
#define HAVE_BUTTON_DATA
#define HAVE_TOUCHSCREEN
#endif

#ifndef CONFIG_BACKLIGHT_FADING
#undef CONFIG_BACKLIGHT_FADING
#endif

/* Reuse the HiBy touchscreen/transport keymap. */
#define CONFIG_KEYPAD HIBY_R3PROII_PAD
#define HAVE_VOLUME_IN_LIST
#define HAVE_SCROLLWHEEL

/*
 * HiByOS exposes the encoder as a pair of momentary key events. Scan more
 * than the hosted default of five event nodes because their index depends on
 * which vendor input services registered first.
 */
#define BUTTON_DEVINPUT_COUNT 16

/* Battery and charger are exposed as "battery" and "usb". */
#define BATTERY_TYPES_COUNT 1
#define CONFIG_BATTERY_MEASURE \
    (VOLTAGE_MEASURE|PERCENTAGE_MEASURE|TIME_MEASURE)
#define BATTERY_CAPACITY_DEFAULT 1300
#define BATTERY_CAPACITY_MIN 1300
#define BATTERY_CAPACITY_MAX 1300
#define BATTERY_CAPACITY_INC 0

/* The vendor ES9219C driver exposes the standard HiBy ALSA controls. */
#define HAVE_HIBY_LINUX_CODEC
#define HAVE_SW_TONE_CONTROLS
/*
 * soc_aic.ko advertises S16_LE and S24_LE (format mask 0x44), not S32_LE.
 * S24_LE uses a 32-bit storage slot and is also the format expected by the
 * stock player's DoP packing.
 */
#define HAVE_ALSA_24BIT
#define HAVE_ALSA_POLLING
#define HW_SAMPR_CAPS SAMPR_CAP_ALL_384

/*
 * The stock output router maps line-out and I2S-to-S/PDIF to Output Port
 * Switch values 1 and 4. The digital endpoint is ALSA card 0/device 1.
 */
#ifndef SIMULATOR
#define HAVE_HIBY_OUTPUT_SELECT
#define DEFAULT_PLAYBACK_DEVICE "hw:0,0"
#define HIBY_ANALOG_PLAYBACK_DEVICE "hw:0,0"
#define HIBY_SPDIF_PLAYBACK_DEVICE "hw:0,1"
#endif

/*
 * The stock digital-microphone path opens hw:0,0 for interleaved S16_LE,
 * stereo capture at 16 kHz. Advertise only that reverse-engineered contract
 * until the configurable higher-rate recorder path is proven.
 */
#define HAVE_RECORDING
#define HAVE_RECORDING_WITHOUT_MONITORING
#define INPUT_SRC_CAPS SRC_CAP_MIC
#define REC_SAMPR_CAPS SAMPR_CAP_16
#define REC_FREQ_DEFAULT REC_FREQ_16
#define REC_SAMPR_DEFAULT SAMPR_16
#define DEFAULT_CAPTURE_DEVICE "hw:0,0"
#ifdef SIMULATOR
#define AUDIOHW_CAPS MIC_GAIN_CAP
#endif

/* Direct DSF/DSDIFF playback using the stock DoP/native transport rules. */
#if !defined(SIMULATOR) && !defined(BOOTLOADER)
#define HAVE_DSD
#endif

#define BACKLIGHT_HIBY
#define MIN_BRIGHTNESS_SETTING 1
#define MAX_BRIGHTNESS_SETTING 100
#define BRIGHTNESS_STEP 5
/* Match the stock boot/UI brightness instead of defaulting to full power. */
#define DEFAULT_BRIGHTNESS_SETTING 60

/* ROLO */
#define BOOTFILE_EXT "ap80promax"
#define BOOTFILE "rockbox." BOOTFILE_EXT
#define BOOTDIR "/.rockbox"

/* USB: the AP80 Pro Max firmware uses the same X1600E gadget stack. */
#define HAVE_USB_ADB
#define HAVE_HOST_USB_AUDIO
#define HAVE_USB_POWER
#define USB_VID_STR "32BB"
#define USB_PID_STR "0101"

#include "hibylinux.h"
