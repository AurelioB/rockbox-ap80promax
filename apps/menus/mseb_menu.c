/***************************************************************************
 * MSEB settings menu
 *
 * Copyright (C) 2026 The Rockbox Developers
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 ****************************************************************************/
#include "config.h"
#include "lang.h"
#include "mseb.h"
#include "mseb_menu.h"
#include "option_select.h"
#include "pcmbuf.h"
#include "settings.h"
#include "settings_list.h"
#include "splash.h"
#include "talk.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static enum mseb_parameter editing_parameter;

static const int parameter_lang_ids[MSEB_NUM_PARAMETERS] =
{
    LANG_MSEB_TEMPERATURE,
    LANG_MSEB_BASS_EXTENSION,
    LANG_MSEB_BASS_TEXTURE,
    LANG_MSEB_NOTE_THICKNESS,
    LANG_MSEB_VOCALS,
    LANG_MSEB_FEMALE_OVERTONES,
    LANG_MSEB_SIBILANCE_LF,
    LANG_MSEB_SIBILANCE_HF,
    LANG_MSEB_IMPULSE_RESPONSE,
    LANG_MSEB_AIR,
};

static const int negative_lang_ids[MSEB_NUM_PARAMETERS] =
{
    LANG_MSEB_COOL_BRIGHT,
    LANG_MSEB_LIGHT,
    LANG_MSEB_FAST,
    LANG_MSEB_CRISP,
    LANG_MSEB_RECESSED_CRISP,
    LANG_MSEB_DETOX,
    LANG_MSEB_SOFT,
    LANG_MSEB_SOFT,
    LANG_MSEB_SLOW_MUSICAL,
    LANG_MSEB_SOFT,
};

static const int positive_lang_ids[MSEB_NUM_PARAMETERS] =
{
    LANG_MSEB_WARM_DARK,
    LANG_MSEB_DEEP,
    LANG_MSEB_THUMPY,
    LANG_MSEB_THICK,
    LANG_MSEB_FORWARD_RADIO,
    LANG_MSEB_VIVID,
    LANG_MSEB_CRISP,
    LANG_MSEB_CRISP,
    LANG_MSEB_FAST_HARD,
    LANG_MSEB_CRISP,
};

static void set_parameter(enum mseb_parameter parameter, int value)
{
    value = mseb_clamp_for_range(value, global_settings.mseb.range);
    global_settings.mseb.values[parameter] = value;
    dsp_mseb_set_value(parameter, value);
}

#define MSEB_OPTION_CALLBACK(name, parameter) \
    void name(int value) { set_parameter(parameter, value); }

MSEB_OPTION_CALLBACK(mseb_temperature_option_callback, MSEB_TEMPERATURE)
MSEB_OPTION_CALLBACK(mseb_bass_extension_option_callback, MSEB_BASS_EXTENSION)
MSEB_OPTION_CALLBACK(mseb_bass_texture_option_callback, MSEB_BASS_TEXTURE)
MSEB_OPTION_CALLBACK(mseb_note_thickness_option_callback, MSEB_NOTE_THICKNESS)
MSEB_OPTION_CALLBACK(mseb_vocals_option_callback, MSEB_VOCALS)
MSEB_OPTION_CALLBACK(mseb_female_overtones_option_callback,
                     MSEB_FEMALE_OVERTONES)
MSEB_OPTION_CALLBACK(mseb_sibilance_lf_option_callback, MSEB_SIBILANCE_LF)
MSEB_OPTION_CALLBACK(mseb_sibilance_hf_option_callback, MSEB_SIBILANCE_HF)
MSEB_OPTION_CALLBACK(mseb_impulse_response_option_callback,
                     MSEB_IMPULSE_RESPONSE)
MSEB_OPTION_CALLBACK(mseb_air_option_callback, MSEB_AIR)

void mseb_enabled_option_callback(bool enabled)
{
    dsp_mseb_enable(enabled);
}

void mseb_range_option_callback(int range)
{
    if (range < 0 || range >= MSEB_NUM_RANGES)
        range = MSEB_RANGE_FINE;

    global_settings.mseb.range = range;
    mseb_normalize_settings(&global_settings.mseb);

    dsp_mseb_apply(&global_settings.mseb);
    /* option_screen() persists the selected range before invoking this
     * callback. Save once more so values clamped by the new range are also
     * canonical on disk immediately. */
    settings_save();
}

static void current_parameter_callback(int value)
{
    set_parameter(editing_parameter, value);
}

static const char *mseb_value_format(char *buffer, size_t buffer_size,
                                     int value, const char *unit)
{
    int lang_id = LANG_MSEB_NEUTRAL;
    (void)unit;

    if (value < 0)
        lang_id = negative_lang_ids[editing_parameter];
    else if (value > 0)
        lang_id = positive_lang_ids[editing_parameter];

    snprintf(buffer, buffer_size, value == 0 ? "%d  %s" : "%+d  %s",
             value, str(lang_id));
    return buffer;
}

static int edit_parameter(void *param)
{
    editing_parameter = (enum mseb_parameter)(intptr_t)param;
    int limit = mseb_range_limit(global_settings.mseb.range);
    struct int_setting int_setting =
    {
        .option_callback = current_parameter_callback,
        .unit = UNIT_INT,
        .step = 1,
        .min = -limit,
        .max = limit,
        .formatter = mseb_value_format,
        .get_talk_id = NULL,
    };
    struct settings_list setting =
    {
        .flags = F_BANFROMQS | F_INT_SETTING | F_T_INT | F_NO_WRAP,
        .setting = &global_settings.mseb.values[editing_parameter],
        .lang_id = parameter_lang_ids[editing_parameter],
        .default_val = { .int_ = 0 },
        .cfg_name = NULL,
        .int_setting = &int_setting,
    };

    pcmbuf_set_low_latency(true);
    option_screen(&setting, NULL, false,
                  ID2P(parameter_lang_ids[editing_parameter]));
    pcmbuf_set_low_latency(false);
    return 0;
}

static char *mseb_parameter_name(int selected_item, void *data,
                                 char *buffer, size_t buffer_size)
{
    enum mseb_parameter parameter = (enum mseb_parameter)(intptr_t)data;
    int value = global_settings.mseb.values[parameter];

    snprintf(buffer, buffer_size, value == 0 ? "%s  %d" : "%s  %+d",
             str(parameter_lang_ids[parameter]), value);
    (void)selected_item;
    return buffer;
}

static int mseb_parameter_speak(int selected_item, void *data)
{
    enum mseb_parameter parameter = (enum mseb_parameter)(intptr_t)data;

    talk_id(parameter_lang_ids[parameter], false);
    talk_number(global_settings.mseb.values[parameter], true);
    (void)selected_item;
    return 0;
}

static int reset_mseb(void)
{
    memset(global_settings.mseb.values, 0,
           sizeof(global_settings.mseb.values));
    dsp_mseb_apply(&global_settings.mseb);
    splash(HZ, ID2P(LANG_RESET_DONE_CLEAR));
    return 0;
}

MENUITEM_SETTING(mseb_enabled, &global_settings.mseb.enabled, NULL);
MENUITEM_SETTING(mseb_range, &global_settings.mseb.range, NULL);

#define MSEB_MENU_ITEM(name, parameter) \
    MENUITEM_FUNCTION_DYNTEXT_W_PARAM(name, 0, edit_parameter, \
        (void *)(intptr_t)(parameter), mseb_parameter_name, \
        mseb_parameter_speak, (void *)(intptr_t)(parameter), NULL, \
        Icon_Menu_setting)

MSEB_MENU_ITEM(mseb_temperature, MSEB_TEMPERATURE);
MSEB_MENU_ITEM(mseb_bass_extension, MSEB_BASS_EXTENSION);
MSEB_MENU_ITEM(mseb_bass_texture, MSEB_BASS_TEXTURE);
MSEB_MENU_ITEM(mseb_note_thickness, MSEB_NOTE_THICKNESS);
MSEB_MENU_ITEM(mseb_vocals, MSEB_VOCALS);
MSEB_MENU_ITEM(mseb_female_overtones, MSEB_FEMALE_OVERTONES);
MSEB_MENU_ITEM(mseb_sibilance_lf, MSEB_SIBILANCE_LF);
MSEB_MENU_ITEM(mseb_sibilance_hf, MSEB_SIBILANCE_HF);
MSEB_MENU_ITEM(mseb_impulse_response, MSEB_IMPULSE_RESPONSE);
MSEB_MENU_ITEM(mseb_air, MSEB_AIR);

MENUITEM_FUNCTION(mseb_reset, 0, ID2P(LANG_RESET), reset_mseb, NULL,
                  Icon_NOICON);

MAKE_MENU(mseb_menu, ID2P(LANG_MSEB), NULL, Icon_EQ,
          &mseb_enabled, &mseb_range,
          &mseb_temperature, &mseb_bass_extension, &mseb_bass_texture,
          &mseb_note_thickness, &mseb_vocals, &mseb_female_overtones,
          &mseb_sibilance_lf, &mseb_sibilance_hf, &mseb_impulse_response,
          &mseb_air, &mseb_reset);
