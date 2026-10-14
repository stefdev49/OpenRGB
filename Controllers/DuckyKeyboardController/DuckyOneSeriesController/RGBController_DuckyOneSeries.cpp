/*---------------------------------------------------------*\
| RGBController_DuckyOneSeries.cpp                          |
|                                                           |
|   RGBController for the Ducky One Series RGB keyboard     |
|   (DKON1708ST, VID:PID 04d9:0245).                        |
|                                                           |
|   Supports the US 108-key (ANSI) and EU 109-key (ISO)     |
|   layouts. US is the default; the layout is selectable    |
|   in OpenRGB.json under the "DuckyOneSeriesDevices"       |
|   section:                                                |
|       { "DuckyOneSeriesDevices": { "layout": "EU" } }     |
|   ("EU" or "ISO" selects the ISO matrix; anything else    |
|   selects the US ANSI matrix). OpenRGB must be restarted  |
|   after changing the setting.                             |
|                                                           |
|   Wire protocol: see DuckyOneSeriesController.cpp         |
|                                                           |
|   stef.dev@free.fr                            13 Oct 2026 |
|                                                           |
|   This file is part of the OpenRGB project                |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#include <cstring>
#include "RGBControllerKeyNames.h"
#include "RGBController_DuckyOneSeries.h"
#include "ResourceManager.h"
#include "SettingsManager.h"

/*---------------------------------------------------------*\
| 0xFFFFFFFF indicates an unused entry in matrix            |
\*---------------------------------------------------------*/
#define NA  0xFFFFFFFF

/*---------------------------------------------------------*\
| Total LED count: 120 wired FWIndex slots (0-119)          |
\*---------------------------------------------------------*/
#define DUCKY_ONE_SERIES_LED_COUNT  120

/*---------------------------------------------------------*\
| Sentinel mode value for the live-paint Direct mode.       |
| Device effect codes fit in a byte; 0xFFFF cannot          |
| collide.                                                  |
\*---------------------------------------------------------*/
#define DUCKY_MODE_DIRECT  0xFFFF

/*---------------------------------------------------------*\
| EU ISO 109-key matrix map (6x22, FWIndex values)          |
|                                                           |
| Grid positions follow the vendor software's own           |
| KeyboardButtons.xml layout (Skins/1800x1125/EU):          |
| full-size body with numpad and P1-P4 macro keys           |
| above it.                                                 |
\*---------------------------------------------------------*/
static unsigned int matrix_map_eu[6][22] =
    { {  96,  NA,  97,  98,  99, 104, 105, 106, 112, 113,  NA, 114,  67,  68,  69, 102, 103, 107, 110, 111, 109, 108 },
      {   0,   1,   8,   9,  16,  17,  24,  25,  32,  33,  40,  41,  48,  NA,  49,  56,  57,  64,  72,  73,  80,  81 },
      {   2,   3,  10,  11,  18,  19,  26,  27,  34,  35,  42,  43,  50,  NA,  52,  58,  59,  66,  74,  75,  82,  83 },
      {   4,   5,  12,  13,  20,  21,  28,  29,  36,  37,  44,  45, 119,  NA,  NA,  NA,  NA,  NA,  76,  77,  84,  NA },
      {   6, 118,   7,  14,  15,  22,  23,  30,  31,  38,  39,  46,  NA,  NA,  47,  NA,  61,  NA,  78,  79,  86,  85 },
      {  91,  90,  92,  NA,  NA,  NA,  93,  NA,  NA,  NA,  94,  95,  60,  NA,  54,  63,  62,  70,  71,  NA,  87,  NA } };

/*---------------------------------------------------------*\
| US ANSI 108-key matrix map (6x22, FWIndex values)         |
|                                                           |
| Same grid as the EU map; ANSI backslash (51) takes        |
| the ISO enter slot and the ISO-only keys (118, 119)       |
| are unused.                                               |
\*---------------------------------------------------------*/
static unsigned int matrix_map_us[6][22] =
    { {  96,  NA,  97,  98,  99, 104, 105, 106, 112, 113,  NA, 114,  67,  68,  69, 102, 103, 107, 110, 111, 109, 108 },
      {   0,   1,   8,   9,  16,  17,  24,  25,  32,  33,  40,  41,  48,  NA,  49,  56,  57,  64,  72,  73,  80,  81 },
      {   2,   3,  10,  11,  18,  19,  26,  27,  34,  35,  42,  43,  50,  NA,  51,  58,  59,  66,  74,  75,  82,  83 },
      {   4,   5,  12,  13,  20,  21,  28,  29,  36,  37,  44,  45,  NA,  NA,  52,  NA,  NA,  NA,  76,  77,  84,  NA },
      {   6,  NA,   7,  14,  15,  22,  23,  30,  31,  38,  39,  46,  NA,  NA,  47,  NA,  61,  NA,  78,  79,  86,  85 },
      {  91,  90,  92,  NA,  NA,  NA,  93,  NA,  NA,  NA,  94,  95,  60,  NA,  54,  63,  62,  70,  71,  NA,  87,  NA } };

/*---------------------------------------------------------*\
| LED names indexed by FWIndex (0-119)                      |
|                                                           |
| Entries 53, 55, 65, 88, 89, 100, 101, 115-117 are         |
| unused in all layouts. Entry 51 is US BACKSLASH only.     |
| Entries 118, 119 are EU ISO keys only.                    |
| Entries 108-111 are macro keys (P1-P4).                   |
|                                                           |
| The LED vector is created in FWIndex order, so the        |
| LED index equals the FWIndex on the wire.                 |
\*---------------------------------------------------------*/
static const char *led_names[] =
{
    KEY_EN_BACK_TICK,       /*   0: GRAVE          */
    KEY_EN_1,               /*   1: 1              */
    KEY_EN_TAB,             /*   2: TAB            */
    KEY_EN_Q,               /*   3: Q              */
    KEY_EN_CAPS_LOCK,       /*   4: CAPSLOCK       */
    KEY_EN_A,               /*   5: A              */
    KEY_EN_LEFT_SHIFT,      /*   6: LSHIFT         */
    KEY_EN_Z,               /*   7: Z              */
    KEY_EN_2,               /*   8: 2              */
    KEY_EN_3,               /*   9: 3              */
    KEY_EN_W,               /*  10: W              */
    KEY_EN_E,               /*  11: E              */
    KEY_EN_S,               /*  12: S              */
    KEY_EN_D,               /*  13: D              */
    KEY_EN_X,               /*  14: X              */
    KEY_EN_C,               /*  15: C              */
    KEY_EN_4,               /*  16: 4              */
    KEY_EN_5,               /*  17: 5              */
    KEY_EN_R,               /*  18: R              */
    KEY_EN_T,               /*  19: T              */
    KEY_EN_F,               /*  20: F              */
    KEY_EN_G,               /*  21: G              */
    KEY_EN_V,               /*  22: V              */
    KEY_EN_B,               /*  23: B              */
    KEY_EN_6,               /*  24: 6              */
    KEY_EN_7,               /*  25: 7              */
    KEY_EN_Y,               /*  26: Y              */
    KEY_EN_U,               /*  27: U              */
    KEY_EN_H,               /*  28: H              */
    KEY_EN_J,               /*  29: J              */
    KEY_EN_N,               /*  30: N              */
    KEY_EN_M,               /*  31: M              */
    KEY_EN_8,               /*  32: 8              */
    KEY_EN_9,               /*  33: 9              */
    KEY_EN_I,               /*  34: I              */
    KEY_EN_O,               /*  35: O              */
    KEY_EN_K,               /*  36: K              */
    KEY_EN_L,               /*  37: L              */
    KEY_EN_COMMA,           /*  38: COMMA          */
    KEY_EN_PERIOD,          /*  39: DOT            */
    KEY_EN_0,               /*  40: 0              */
    KEY_EN_MINUS,           /*  41: MINUS          */
    KEY_EN_P,               /*  42: P              */
    KEY_EN_LEFT_BRACKET,    /*  43: LBRACKET       */
    KEY_EN_SEMICOLON,       /*  44: SEMICOLON      */
    KEY_EN_QUOTE,           /*  45: APOSTROPHE     */
    KEY_EN_FORWARD_SLASH,   /*  46: SLASH          */
    KEY_EN_RIGHT_SHIFT,     /*  47: RSHIFT         */
    KEY_EN_EQUALS,          /*  48: EQUAL          */
    KEY_EN_BACKSPACE,       /*  49: BACKSPACE      */
    KEY_EN_RIGHT_BRACKET,   /*  50: RBRACKET       */
    KEY_EN_ANSI_BACK_SLASH, /*  51: BACKSLASH (US) */
    KEY_EN_ANSI_ENTER,      /*  52: ENTER          */
    KEY_EN_UNUSED,          /*  53: unused         */
    KEY_EN_RIGHT_CONTROL,   /*  54: RCTRL          */
    KEY_EN_UNUSED,          /*  55: unused         */
    KEY_EN_INSERT,          /*  56: INSERT         */
    KEY_EN_HOME,            /*  57: HOME           */
    KEY_EN_DELETE,          /*  58: DEL            */
    KEY_EN_END,             /*  59: END           */
    KEY_EN_MENU,            /*  60: APP            */
    KEY_EN_UP_ARROW,        /*  61: UP             */
    KEY_EN_DOWN_ARROW,      /*  62: DOWN           */
    KEY_EN_LEFT_ARROW,      /*  63: LEFT           */
    KEY_EN_PAGE_UP,         /*  64: PGUP           */
    KEY_EN_UNUSED,          /*  65: unused         */
    KEY_EN_PAGE_DOWN,       /*  66: PGDN           */
    KEY_EN_F10,             /*  67: F10            */
    KEY_EN_F11,             /*  68: F11            */
    KEY_EN_F12,             /*  69: F12            */
    KEY_EN_RIGHT_ARROW,     /*  70: RIGHT          */
    KEY_EN_NUMPAD_0,        /*  71: NUM0           */
    KEY_EN_NUMPAD_LOCK,     /*  72: NUMLOCK        */
    KEY_EN_NUMPAD_DIVIDE,   /*  73: NUMDIV         */
    KEY_EN_NUMPAD_7,        /*  74: NUM7           */
    KEY_EN_NUMPAD_8,        /*  75: NUM8           */
    KEY_EN_NUMPAD_4,        /*  76: NUM4           */
    KEY_EN_NUMPAD_5,        /*  77: NUM5           */
    KEY_EN_NUMPAD_1,        /*  78: NUM1           */
    KEY_EN_NUMPAD_2,        /*  79: NUM2           */
    KEY_EN_NUMPAD_TIMES,    /*  80: NUMSTAR        */
    KEY_EN_NUMPAD_MINUS,    /*  81: NUMMINUS       */
    KEY_EN_NUMPAD_9,        /*  82: NUM9           */
    KEY_EN_NUMPAD_PLUS,     /*  83: NUMPLUS        */
    KEY_EN_NUMPAD_6,        /*  84: NUM6           */
    KEY_EN_NUMPAD_ENTER,    /*  85: NUMENTER       */
    KEY_EN_NUMPAD_3,        /*  86: NUM3           */
    KEY_EN_NUMPAD_PERIOD,   /*  87: NUMDEL         */
    KEY_EN_UNUSED,          /*  88: unused         */
    KEY_EN_UNUSED,          /*  89: unused         */
    KEY_EN_LEFT_WINDOWS,    /*  90: LWIN           */
    KEY_EN_LEFT_CONTROL,    /*  91: LCTRL          */
    KEY_EN_LEFT_ALT,        /*  92: LALT           */
    KEY_EN_SPACE,           /*  93: SPACE          */
    KEY_EN_RIGHT_ALT,       /*  94: RALT           */
    KEY_EN_RIGHT_WINDOWS,   /*  95: RWIN           */
    KEY_EN_ESCAPE,          /*  96: ESC            */
    KEY_EN_F1,              /*  97: F1             */
    KEY_EN_F2,              /*  98: F2             */
    KEY_EN_F3,              /*  99: F3             */
    KEY_EN_UNUSED,          /* 100: unused         */
    KEY_EN_UNUSED,          /* 101: unused         */
    KEY_EN_PRINT_SCREEN,    /* 102: PRT_SC         */
    KEY_EN_SCROLL_LOCK,     /* 103: SCR_LK         */
    KEY_EN_F4,              /* 104: F4             */
    KEY_EN_F5,              /* 105: F5             */
    KEY_EN_F6,              /* 106: F6             */
    KEY_EN_PAUSE_BREAK,     /* 107: PAU_BRK        */
    "Key: P4",              /* 108: P4 (macro)     */
    "Key: P3",              /* 109: P3 (macro)     */
    "Key: P1",              /* 110: P1 (macro)     */
    "Key: P2",              /* 111: P2 (macro)     */
    KEY_EN_F7,              /* 112: F7             */
    KEY_EN_F8,              /* 113: F8             */
    KEY_EN_F9,              /* 114: F9             */
    KEY_EN_UNUSED,          /* 115: unused         */
    KEY_EN_UNUSED,          /* 116: unused         */
    KEY_EN_UNUSED,          /* 117: unused         */
    KEY_EN_ISO_BACK_SLASH,  /* 118: EU key (LSHIFT) */
    KEY_EN_POUND,           /* 119: EU key (ENTER)  */
};

/*---------------------------------------------------------*\
| Color Cycle brightness levels (G channel of the           |
| bytes 8-10 triple; R=0xFF, B=0x00 fixed)                  |
\*---------------------------------------------------------*/
static const unsigned char DUCKY_COLOR_CYCLE_BRIGHTNESS[] = { 0x33, 0x46, 0x59, 0x6C, 0x7F };

/*---------------------------------------------------------*\
| Color Wave alternate speed table (vertical only:          |
| F->B flag 0x02 / B->F flag 0x06 -- NOT for R->L 0x04)     |
\*---------------------------------------------------------*/
static const unsigned char DUCKY_COLOR_WAVE_ALT_SPEED[] = { 0x33, 0x2F, 0x2B, 0x27, 0x23, 0x1F, 0x1B };

/*---------------------------------------------------------*\
| Effect default parameters (speed, flag, param)            |
| from all-animations.pcap / the reference tool             |
|                                                           |
| Rain's param is the drop-spawn modulus, INVERTED on       |
| the wire: smaller = more drops. 0x33 (51) is the          |
| medium default. OpenRGB has no dedicated coverage         |
| control, so the density stays at this default.            |
\*---------------------------------------------------------*/
struct ducky_effect_defaults
{
    unsigned char speed;
    unsigned char flag;
    unsigned char param;
};

static const ducky_effect_defaults effect_defaults[] =
{
    { 0x00, 0x00, 0x00 },   /* 0x00: Static (no entry - 0xFE marker only) */
    { 0x00, 0x80, 0xFF },   /* 0x01: Full Random (speed unused)           */
    { 0x31, 0x20, 0xFF },   /* 0x02: Breathing                            */
    { 0x31, 0x00, 0xFF },   /* 0x03: Color Cycle                          */
    { 0x38, 0x80, 0xFF },   /* 0x04: Reactive                             */
    { 0x3C, 0x02, 0x0A },   /* 0x05: Color Wave                           */
    { 0x78, 0x80, 0xFF },   /* 0x06: Aurora                               */
    { 0x6C, 0x80, 0xFF },   /* 0x07: Ripple                               */
    { 0x38, 0x80, 0x33 },   /* 0x08: Rain (medium coverage)               */
    { 0x2D, 0x80, 0x04 },   /* 0x09: Snake                                */
    { 0x00, 0x00, 0x00 },   /* 0x0A: unused (device rejects)              */
    { 0x00, 0x00, 0x00 },   /* 0x0B: unused (dead code)                   */
};

/*---------------------------------------------------------*\
| Speed tables: per-effect discrete timing values           |
| (from FUN_004425e0 decompiled firmware code)              |
|                                                           |
| OpenRGB speed (0-255) maps to a table index.              |
| Index 0 = slowest, last index = fastest.                  |
\*---------------------------------------------------------*/
struct ducky_speed_table
{
    const unsigned char*  values;
    unsigned char         count;
};

static const unsigned char speed_vals_breathing[]    = { 0x46, 0x3C, 0x37, 0x31, 0x2C, 0x26, 0x1C };
static const unsigned char speed_vals_color_cycle[]  = { 0x5A, 0x50, 0x3C, 0x31, 0x28, 0x1E, 0x14 };
static const unsigned char speed_vals_reactive[]     = { 0x50, 0x46, 0x41, 0x38, 0x2D, 0x14 };
static const unsigned char speed_vals_color_wave[]   = { 0x50, 0x48, 0x42, 0x3C, 0x36, 0x30, 0x2C };
static const unsigned char speed_vals_aurora[]       = { 0xB4, 0xB3, 0x96, 0x78, 0x6C, 0x66, 0x62 };
static const unsigned char speed_vals_ripple[]       = { 0xB3, 0x96, 0x78, 0x6C, 0x66, 0x62, 0x60 };
static const unsigned char speed_vals_rain[]         = { 0x46, 0x41, 0x3C, 0x38, 0x32, 0x28, 0x1E };
static const unsigned char speed_vals_snake[]        = { 0x41, 0x3B, 0x36, 0x31, 0x2D, 0x29, 0x25 };

static const ducky_speed_table speed_tables[] =
{
    { NULL,                   0 },   /* 0x00: Static       */
    { NULL,                   0 },   /* 0x01: Full Random  */
    { speed_vals_breathing,   7 },   /* 0x02: Breathing    */
    { speed_vals_color_cycle, 7 },   /* 0x03: Color Cycle  */
    { speed_vals_reactive,    6 },   /* 0x04: Reactive     */
    { speed_vals_color_wave,  7 },   /* 0x05: Color Wave   */
    { speed_vals_aurora,      7 },   /* 0x06: Aurora       */
    { speed_vals_ripple,      7 },   /* 0x07: Ripple       */
    { speed_vals_rain,        7 },   /* 0x08: Rain         */
    { speed_vals_snake,       7 },   /* 0x09: Snake        */
    { NULL,                   0 },   /* 0x0A: unused       */
    { NULL,                   0 },   /* 0x0B: unused       */
};

static unsigned char speed_to_timing(unsigned char effect_code, unsigned char openrgb_speed)
{
    if(effect_code >= sizeof(speed_tables) / sizeof(speed_tables[0]))
    {
        return(0x31);
    }

    const ducky_speed_table* table = &speed_tables[effect_code];

    if(table->count == 0 || table->values == NULL)
    {
        return(0x31);
    }

    int index = (int)openrgb_speed * (table->count - 1) / 255;

    if(index < 0)
    {
        index = 0;
    }

    if(index >= table->count)
    {
        index = table->count - 1;
    }

    return(table->values[index]);
}

/**------------------------------------------------------------------*\
    @name Ducky One Series RGB
    @category Keyboard
    @type USB
    @save :white_check_mark:
    @direct :white_check_mark:
    @effects :white_check_mark:
    @detectors DetectDuckyOneSeriesControllers
    @comment
\*-------------------------------------------------------------------*/

RGBController_DuckyOneSeries::RGBController_DuckyOneSeries(DuckyOneSeriesController* controller_ptr)
{
    controller      = controller_ptr;

    /*-----------------------------------------------------*\
    | Select the key matrix layout: US ANSI (default) or    |
    | EU ISO, from the "DuckyOneSeriesDevices" section in   |
    | OpenRGB.json: { "layout": "US" }                      |
    \*-----------------------------------------------------*/
    keyboard_layout = 1;

    json ducky_settings = ResourceManager::get()->GetSettingsManager()->GetSettings("DuckyOneSeriesDevices");

    if(ducky_settings.contains("layout") && ducky_settings["layout"].is_string())
    {
        std::string layout = ducky_settings["layout"];

        if(layout == "EU" || layout == "ISO")
        {
            keyboard_layout = 0;
        }
    }

    name        = controller->GetNameString();
    vendor      = "Ducky";
    type        = DEVICE_TYPE_KEYBOARD;
    description = "Ducky One Series RGB Device";
    location    = controller->GetLocationString();
    serial      = controller->GetSerialString();

    /*-----------------------------------------------------*\
    | Build the wired-LED mask for the selected layout from |
    | its matrix map. Unwired FWIndex slots must stay black |
    | in per-key uploads (OPENRGB_INTEGRATION.md 7a) and    |
    | are skipped by the 0xc0 paint path.                   |
    \*-----------------------------------------------------*/
    memset(fwindex_wired, 0x00, sizeof(fwindex_wired));

    for(unsigned int row = 0; row < 6; row++)
    {
        for(unsigned int col = 0; col < 22; col++)
        {
            unsigned int fwindex = (keyboard_layout == 0) ? matrix_map_eu[row][col]
                                                          : matrix_map_us[row][col];

            if(fwindex != NA && fwindex < DUCKY_ONE_SERIES_LED_COUNT)
            {
                fwindex_wired[fwindex] = true;
            }
        }
    }

    /*-----------------------------------------------------*\
    | Enrich the description with firmware version and      |
    | the device's active profile                           |
    \*-----------------------------------------------------*/
    std::string firmware = controller->GetFirmwareVersion();

    if(!firmware.empty())
    {
        description += " (FW " + firmware + ", profile " + std::to_string(controller->GetActiveProfile()) + ")";
    }

    /*-----------------------------------------------------*\
    | Direct mode - 0xc0 live frame painting                |
    |                                                       |
    | Named "Direct" so RGBController::SetCustomMode() finds|
    | it. RAM-only: the frame is lost on effect switch or   |
    | reboot. Saving switches to the persistent 0xB0 path.  |
    \*-----------------------------------------------------*/
    mode Direct;
    Direct.name       = "Direct";
    Direct.value      = DUCKY_MODE_DIRECT;
    Direct.flags      = MODE_FLAG_HAS_PER_LED_COLOR | MODE_FLAG_MANUAL_SAVE;
    Direct.color_mode = MODE_COLORS_PER_LED;
    modes.push_back(Direct);

    /*-----------------------------------------------------*\
    | Static                                                |
    \*-----------------------------------------------------*/
    mode Static;
    Static.name           = "Static";
    Static.value          = DUCKY_EFFECT_STATIC;
    Static.flags          = MODE_FLAG_HAS_MODE_SPECIFIC_COLOR | MODE_FLAG_MANUAL_SAVE;
    Static.color_mode     = MODE_COLORS_MODE_SPECIFIC;
    Static.colors_min     = 1;
    Static.colors_max     = 1;
    Static.colors.push_back(ToRGBColor(0xFF, 0xFF, 0xFF));
    modes.push_back(Static);

    /*-----------------------------------------------------*\
    | Off                                                   |
    \*-----------------------------------------------------*/
    mode Off;
    Off.name       = "Off";
    Off.value      = DUCKY_EFFECT_OFF;
    Off.flags      = MODE_FLAG_MANUAL_SAVE;
    Off.color_mode = MODE_COLORS_NONE;
    modes.push_back(Off);

    /*-----------------------------------------------------*\
    | Full Random (hidden firmware effect, fixed pace)      |
    \*-----------------------------------------------------*/
    mode FullRandom;
    FullRandom.name       = "Full Random";
    FullRandom.value      = DUCKY_EFFECT_FULL_RANDOM;
    FullRandom.flags      = MODE_FLAG_MANUAL_SAVE;
    FullRandom.color_mode = MODE_COLORS_NONE;
    modes.push_back(FullRandom);

    /*-----------------------------------------------------*\
    | Breathing                                             |
    \*-----------------------------------------------------*/
    mode Breathing;
    Breathing.name           = "Breathing";
    Breathing.value          = DUCKY_EFFECT_BREATHING;
    Breathing.flags          = MODE_FLAG_HAS_MODE_SPECIFIC_COLOR | MODE_FLAG_HAS_SPEED | MODE_FLAG_HAS_RANDOM_COLOR | MODE_FLAG_MANUAL_SAVE;
    Breathing.color_mode     = MODE_COLORS_MODE_SPECIFIC;
    Breathing.colors_min     = 1;
    Breathing.colors_max     = 1;
    Breathing.speed_min      = 0;
    Breathing.speed_max      = 255;
    Breathing.speed          = 127;
    Breathing.colors.push_back(ToRGBColor(0xFF, 0xFF, 0xFF));
    modes.push_back(Breathing);

    /*-----------------------------------------------------*\
    | Color Cycle - brightness is the 5-level G encoding    |
    \*-----------------------------------------------------*/
    mode ColorCycle;
    ColorCycle.name           = "Color Cycle";
    ColorCycle.value          = DUCKY_EFFECT_COLOR_CYCLE;
    ColorCycle.flags          = MODE_FLAG_HAS_SPEED | MODE_FLAG_HAS_BRIGHTNESS | MODE_FLAG_MANUAL_SAVE;
    ColorCycle.color_mode     = MODE_COLORS_NONE;
    ColorCycle.speed_min      = 0;
    ColorCycle.speed_max      = 255;
    ColorCycle.speed          = 127;
    ColorCycle.brightness_min = 0;
    ColorCycle.brightness_max = 4;
    ColorCycle.brightness     = 2;
    modes.push_back(ColorCycle);

    /*-----------------------------------------------------*\
    | Reactive                                              |
    \*-----------------------------------------------------*/
    mode Reactive;
    Reactive.name           = "Reactive";
    Reactive.value          = DUCKY_EFFECT_REACTIVE;
    Reactive.flags          = MODE_FLAG_HAS_MODE_SPECIFIC_COLOR | MODE_FLAG_HAS_SPEED | MODE_FLAG_HAS_RANDOM_COLOR | MODE_FLAG_MANUAL_SAVE;
    Reactive.color_mode     = MODE_COLORS_MODE_SPECIFIC;
    Reactive.colors_min     = 2;
    Reactive.colors_max     = 2;
    Reactive.speed_min      = 0;
    Reactive.speed_max      = 255;
    Reactive.speed          = 127;
    Reactive.colors.push_back(ToRGBColor(0xFF, 0xFF, 0xFF));
    Reactive.colors.push_back(ToRGBColor(0x00, 0x00, 0x00));
    modes.push_back(Reactive);

    /*-----------------------------------------------------*\
    | Color Wave - 4 directions, alternate speed table      |
    | for front-to-back / back-to-front                     |
    \*-----------------------------------------------------*/
    mode ColorWave;
    ColorWave.name           = "Color Wave";
    ColorWave.value          = DUCKY_EFFECT_COLOR_WAVE;
    ColorWave.flags          = MODE_FLAG_HAS_MODE_SPECIFIC_COLOR | MODE_FLAG_HAS_SPEED | MODE_FLAG_HAS_DIRECTION_LR | MODE_FLAG_HAS_DIRECTION_UD | MODE_FLAG_HAS_RANDOM_COLOR | MODE_FLAG_MANUAL_SAVE;
    ColorWave.color_mode     = MODE_COLORS_MODE_SPECIFIC;
    ColorWave.colors_min     = 1;
    ColorWave.colors_max     = 1;
    ColorWave.speed_min      = 0;
    ColorWave.speed_max      = 255;
    ColorWave.speed          = 127;
    ColorWave.direction      = MODE_DIRECTION_RIGHT;
    ColorWave.colors.push_back(ToRGBColor(0xFF, 0xFF, 0xFF));
    modes.push_back(ColorWave);

    /*-----------------------------------------------------*\
    | Aurora                                                |
    \*-----------------------------------------------------*/
    mode Aurora;
    Aurora.name           = "Aurora";
    Aurora.value          = DUCKY_EFFECT_AURORA;
    Aurora.flags          = MODE_FLAG_HAS_MODE_SPECIFIC_COLOR | MODE_FLAG_HAS_SPEED | MODE_FLAG_HAS_RANDOM_COLOR | MODE_FLAG_MANUAL_SAVE;
    Aurora.color_mode     = MODE_COLORS_MODE_SPECIFIC;
    Aurora.colors_min     = 2;
    Aurora.colors_max     = 2;
    Aurora.speed_min      = 0;
    Aurora.speed_max      = 255;
    Aurora.speed          = 127;
    Aurora.colors.push_back(ToRGBColor(0xFF, 0xFF, 0xFF));
    Aurora.colors.push_back(ToRGBColor(0x00, 0x00, 0x00));
    modes.push_back(Aurora);

    /*-----------------------------------------------------*\
    | Ripple                                                |
    \*-----------------------------------------------------*/
    mode Ripple;
    Ripple.name           = "Ripple";
    Ripple.value          = DUCKY_EFFECT_RIPPLE;
    Ripple.flags          = MODE_FLAG_HAS_MODE_SPECIFIC_COLOR | MODE_FLAG_HAS_SPEED | MODE_FLAG_HAS_RANDOM_COLOR | MODE_FLAG_MANUAL_SAVE;
    Ripple.color_mode     = MODE_COLORS_MODE_SPECIFIC;
    Ripple.colors_min     = 2;
    Ripple.colors_max     = 2;
    Ripple.speed_min      = 0;
    Ripple.speed_max      = 255;
    Ripple.speed          = 127;
    Ripple.colors.push_back(ToRGBColor(0xFF, 0xFF, 0xFF));
    Ripple.colors.push_back(ToRGBColor(0x00, 0x00, 0x00));
    modes.push_back(Ripple);

    /*-----------------------------------------------------*\
    | Rain                                                  |
    \*-----------------------------------------------------*/
    mode Rain;
    Rain.name           = "Rain";
    Rain.value          = DUCKY_EFFECT_RAIN;
    Rain.flags          = MODE_FLAG_HAS_MODE_SPECIFIC_COLOR | MODE_FLAG_HAS_SPEED | MODE_FLAG_HAS_RANDOM_COLOR | MODE_FLAG_MANUAL_SAVE;
    Rain.color_mode     = MODE_COLORS_MODE_SPECIFIC;
    Rain.colors_min     = 2;
    Rain.colors_max     = 2;
    Rain.speed_min      = 0;
    Rain.speed_max      = 255;
    Rain.speed          = 127;
    Rain.colors.push_back(ToRGBColor(0xFF, 0xFF, 0xFF));
    Rain.colors.push_back(ToRGBColor(0x00, 0x00, 0x00));
    modes.push_back(Rain);

    /*-----------------------------------------------------*\
    | Snake - 2 vertical directions (bottom->top flag is    |
    | 0x02, not 0x04; UI shows Up/Down, not Left/Right)     |
    \*-----------------------------------------------------*/
    mode Snake;
    Snake.name           = "Snake";
    Snake.value          = DUCKY_EFFECT_SNAKE;
    Snake.flags          = MODE_FLAG_HAS_MODE_SPECIFIC_COLOR | MODE_FLAG_HAS_SPEED | MODE_FLAG_HAS_DIRECTION_UD | MODE_FLAG_HAS_RANDOM_COLOR | MODE_FLAG_MANUAL_SAVE;
    Snake.color_mode     = MODE_COLORS_MODE_SPECIFIC;
    Snake.colors_min     = 2;
    Snake.colors_max     = 2;
    Snake.speed_min      = 0;
    Snake.speed_max      = 255;
    Snake.speed          = 127;
    Snake.direction      = MODE_DIRECTION_DOWN;
    Snake.colors.push_back(ToRGBColor(0xFF, 0xFF, 0xFF));
    Snake.colors.push_back(ToRGBColor(0x00, 0x00, 0x00));
    modes.push_back(Snake);

    SetupZones();
}

RGBController_DuckyOneSeries::~RGBController_DuckyOneSeries()
{
    Shutdown();

    delete controller;
}

/*---------------------------------------------------------*\
| SetAllColors - fill the flat color buffer directly        |
|                                                           |
| The keyboard is one fixed matrix controlled as "Entire    |
| Device"; the zone exists only to render the keyboard      |
| map in the LED view. Fill the flat buffer directly.       |
\*---------------------------------------------------------*/
void RGBController_DuckyOneSeries::SetAllColors(RGBColor color)
{
    AccessMutex.lock();

    for(std::size_t color_idx = 0; color_idx < colors.size(); color_idx++)
    {
        colors[color_idx] = color;
    }

    AccessMutex.unlock();
}

/*---------------------------------------------------------*\
| SetupZones - LEDs in FWIndex order, one matrix zone       |
\*---------------------------------------------------------*/

void RGBController_DuckyOneSeries::SetupZones()
{
    /*-----------------------------------------------------*\
    | Clear any existing zones and LEDs                     |
    |                                                       |
    | The keyboard is a single fixed matrix, controlled as  |
    | "Entire Device". The color buffer is still managed    |
    | directly (see SetAllColors) instead of through the    |
    | zone machinery, but the matrix zone below is what     |
    | lets the LED view draw the keyboard map: DeviceView   |
    | positions LEDs from the zone matrix map only.         |
    \*-----------------------------------------------------*/
    zones.clear();
    leds.clear();

    /*-----------------------------------------------------*\
    | LEDs are created in FWIndex order, so LED index ==    |
    | FWIndex on the wire                                   |
    \*-----------------------------------------------------*/
    for(unsigned int led_idx = 0; led_idx < DUCKY_ONE_SERIES_LED_COUNT; led_idx++)
    {
        led new_led;

        new_led.name = led_names[led_idx];

        leds.push_back(new_led);
    }

    /*-----------------------------------------------------*\
    | Single matrix zone covering the whole keyboard.       |
    | Matrix values are FWIndex values and the zone starts  |
    | at LED 0, so matrix value == LED index. Select the    |
    | US or EU grid the same way fwindex_wired is built.    |
    \*-----------------------------------------------------*/
    unsigned int* matrix_map_ptr = (unsigned int *)((keyboard_layout == 0) ? matrix_map_eu
                                                                           : matrix_map_us);

    zone new_zone;

    new_zone.name                   = ZONE_EN_KEYBOARD;
    new_zone.type                   = ZONE_TYPE_MATRIX;
    new_zone.leds_min               = DUCKY_ONE_SERIES_LED_COUNT;
    new_zone.leds_max               = DUCKY_ONE_SERIES_LED_COUNT;
    new_zone.leds_count             = DUCKY_ONE_SERIES_LED_COUNT;
    new_zone.matrix_map.Set(6, 22, matrix_map_ptr);

    zones.push_back(new_zone);

    /*-----------------------------------------------------*\
    | Size the color buffer and wire the zone LED/color     |
    | pointers                                              |
    \*-----------------------------------------------------*/
    SetupColors();
}

void RGBController_DuckyOneSeries::ResizeZone(int /*zone*/, int /*new_size*/)
{
    /*---------------------------------------------------------*\
    | This device does not support resizing zones               |
    \*---------------------------------------------------------*/
}

/*---------------------------------------------------------*\
| BuildColorBuffer - colors vector to the 384-byte FWIndex  |
| indexed buffer used by the 0xB0 persistent path           |
\*---------------------------------------------------------*/

void RGBController_DuckyOneSeries::BuildColorBuffer(unsigned char* color_buf_384)
{
    memset(color_buf_384, 0x00, DUCKY_ONE_SERIES_COLOR_BUF_SIZE);

    for(std::size_t color_idx = 0; color_idx < colors.size() && color_idx < DUCKY_ONE_SERIES_LED_COUNT; color_idx++)
    {
        /*-------------------------------------------------*\
        | Unwired FWIndex slots stay black                  |
        \*-------------------------------------------------*/
        if(!fwindex_wired[color_idx])
        {
            continue;
        }

        color_buf_384[(color_idx * 3) + 0] = RGBGetRValue(colors[color_idx]);
        color_buf_384[(color_idx * 3) + 1] = RGBGetGValue(colors[color_idx]);
        color_buf_384[(color_idx * 3) + 2] = RGBGetBValue(colors[color_idx]);
    }
}

/*---------------------------------------------------------*\
| PaintDirectColors - push the per-LED colors through the   |
| 0xc0 live paint path. Fill-all fast path when every LED   |
| has the same color, otherwise 15-key chunks.              |
\*---------------------------------------------------------*/

void RGBController_DuckyOneSeries::PaintDirectColors()
{
    if(colors.empty())
    {
        return;
    }

    /*-----------------------------------------------------*\
    | Fast path: all LEDs the same color = one fill packet  |
    \*-----------------------------------------------------*/
    bool same_color = true;

    for(std::size_t color_idx = 1; color_idx < colors.size(); color_idx++)
    {
        if(colors[color_idx] != colors[0])
        {
            same_color = false;
            break;
        }
    }

    if(same_color)
    {
        controller->PaintFill(colors[0]);
        return;
    }

    /*-----------------------------------------------------*\
    | Per-key list in 15-entry chunks (wired LEDs only)     |
    \*-----------------------------------------------------*/
    std::vector<std::pair<unsigned char, RGBColor>> entries;

    for(std::size_t color_idx = 0; color_idx < colors.size() && color_idx < DUCKY_ONE_SERIES_LED_COUNT; color_idx++)
    {
        if(!fwindex_wired[color_idx])
        {
            continue;
        }

        entries.push_back(std::make_pair((unsigned char)color_idx, colors[color_idx]));
    }

    controller->PaintKeys(entries);
}

/*---------------------------------------------------------*\
| DeviceUpdateLEDs - per-LED color updates (Direct mode     |
| only; effect modes ignore per-LED colors)                 |
\*---------------------------------------------------------*/

void RGBController_DuckyOneSeries::DeviceUpdateLEDs()
{
    unsigned int mode_value = modes[active_mode].value;

    if(mode_value == DUCKY_MODE_DIRECT)
    {
        controller->EnterPaintMode();
        PaintDirectColors();
    }
}

void RGBController_DuckyOneSeries::DeviceUpdateZoneLEDs(int /*zone*/)
{
    DeviceUpdateLEDs();
}

void RGBController_DuckyOneSeries::DeviceUpdateSingleLED(int /*led*/)
{
    DeviceUpdateLEDs();
}

/*---------------------------------------------------------*\
| ComputeModeParameters - map the active OpenRGB mode to    |
| device effect parameters (speed, flag, param, colors)     |
|                                                           |
| Direction flag bits are effect-specific:                  |
|   Color Wave (0x05): 0x00=L->R, 0x04=R->L, 0x02=F->B,     |
|                       0x06=B->F; F->B and B->F use the    |
|                       alternate speed table.              |
|                       The hardware renders F->B (0x02)    |
|                       as downward and B->F (0x06) as      |
|                       upward, so OpenRGB Up -> 0x06 and   |
|                       Down -> 0x02.                       |
|   Snake (0x09):      0x00=top->bottom, 0x02=bottom->top   |
|                       (vertical movement; UI shows        |
|                       Up/Down, not Left/Right)            |
|                                                           |
| Bit 7 of the flag byte is the random/rainbow switch.      |
\*---------------------------------------------------------*/

void RGBController_DuckyOneSeries::ComputeModeParameters
    (
    unsigned char   effect_code,
    unsigned char&  speed,
    unsigned char&  flag,
    unsigned char&  param,
    RGBColor&       fg,
    RGBColor&       bg
    )
{
    /*-----------------------------------------------------*\
    | Effect default parameters                             |
    \*-----------------------------------------------------*/
    if(effect_code < (sizeof(effect_defaults) / sizeof(effect_defaults[0])))
    {
        speed   = effect_defaults[effect_code].speed;
        flag    = effect_defaults[effect_code].flag;
        param   = effect_defaults[effect_code].param;
    }
    else
    {
        speed   = 0x31;
        flag    = 0x00;
        param   = 0xFF;
    }

    /*-----------------------------------------------------*\
    | Map direction to flag bits (effect-specific)          |
    \*-----------------------------------------------------*/
    if(modes[active_mode].flags & (MODE_FLAG_HAS_DIRECTION_LR | MODE_FLAG_HAS_DIRECTION_UD))
    {
        if(effect_code == DUCKY_EFFECT_COLOR_WAVE)
        {
            /* Clear all direction bits (0x06) before setting */
            flag &= ~0x06;

            if(modes[active_mode].direction == MODE_DIRECTION_LEFT)
            {
                flag |= 0x04;   /* R->L */
            }
            else if(modes[active_mode].direction == MODE_DIRECTION_UP)
            {
                flag |= 0x06;   /* B->F (alternate speed table) */
            }
            else if(modes[active_mode].direction == MODE_DIRECTION_DOWN)
            {
                flag |= 0x02;   /* F->B (alternate speed table) */
            }
            /* MODE_DIRECTION_RIGHT = default 0x00 (L->R) */
        }
        else if(effect_code == DUCKY_EFFECT_SNAKE)
        {
            flag &= ~0x02;

            if(modes[active_mode].direction == MODE_DIRECTION_UP)
            {
                flag |= 0x02;   /* bottom->top (Snake-specific encoding) */
            }
            /* MODE_DIRECTION_DOWN = default 0x00 (top->bottom) */
        }
    }

    /*-----------------------------------------------------*\
    | Convert OpenRGB 0-255 speed to a device timing value  |
    |                                                       |
    | Color Wave vertical directions (F->B 0x02 / B->F      |
    | 0x06, i.e. flag bit 0x02 set) use the alternate       |
    | speed table; horizontal directions use the main one.  |
    \*-----------------------------------------------------*/
    if(modes[active_mode].flags & MODE_FLAG_HAS_SPEED)
    {
        if(effect_code == DUCKY_EFFECT_COLOR_WAVE && (flag & 0x02) != 0x00)
        {
            /* F->B (0x02) or B->F (0x06): alternate speed table.
               Bit 0x02 is set only for the vertical directions;
               0x04 (R->L) must use the main table. */
            int index = (int)modes[active_mode].speed * 6 / 255;

            if(index < 0)
            {
                index = 0;
            }

            if(index > 6)
            {
                index = 6;
            }

            speed = DUCKY_COLOR_WAVE_ALT_SPEED[index];
        }
        else
        {
            speed = speed_to_timing(effect_code, (unsigned char)modes[active_mode].speed);
        }
    }

    /*-----------------------------------------------------*\
    | Map random color flag to bit 7 of the flag byte       |
    \*-----------------------------------------------------*/
    if(modes[active_mode].flags & MODE_FLAG_HAS_RANDOM_COLOR)
    {
        if(modes[active_mode].color_mode == MODE_COLORS_RANDOM)
        {
            flag |= 0x80;
        }
        else
        {
            flag &= ~0x80;
        }
    }

    /*-----------------------------------------------------*\
    | Build color parameters                                |
    |                                                       |
    | Color Cycle (0x03): fg carries the 5-level brightness |
    | encoding (R=0xFF, G=level, B=0x00) from the mode's    |
    | brightness value.                                     |
    | Other effects: foreground from colors[0], background  |
    | from colors[1] (default black).                       |
    \*-----------------------------------------------------*/
    if(effect_code == DUCKY_EFFECT_COLOR_CYCLE)
    {
        unsigned char level = modes[active_mode].brightness;

        if(level > 4)
        {
            level = 4;
        }

        fg = ToRGBColor(0xFF, DUCKY_COLOR_CYCLE_BRIGHTNESS[level], 0x00);
        bg = ToRGBColor(0x00, 0x00, 0x00);
    }
    else
    {
        if(modes[active_mode].colors.size() > 0)
        {
            fg = modes[active_mode].colors[0];
        }
        else
        {
            fg = ToRGBColor(0xFF, 0xFF, 0xFF);
        }

        if(modes[active_mode].colors.size() > 1)
        {
            bg = modes[active_mode].colors[1];
        }
        else
        {
            bg = ToRGBColor(0x00, 0x00, 0x00);
        }
    }
}

/*---------------------------------------------------------*\
| DeviceUpdateMode - apply the active mode to the device    |
\*---------------------------------------------------------*/

void RGBController_DuckyOneSeries::DeviceUpdateMode()
{
    unsigned int mode_value = modes[active_mode].value;

    if(mode_value == DUCKY_MODE_DIRECT)
    {
        controller->EnterPaintMode();
        PaintDirectColors();
        return;
    }

    if(mode_value == DUCKY_EFFECT_STATIC)
    {
        RGBColor color = ToRGBColor(0xFF, 0xFF, 0xFF);

        if(modes[active_mode].colors.size() > 0)
        {
            color = modes[active_mode].colors[0];
        }

        controller->SetStatic(color);
        return;
    }

    if(mode_value == DUCKY_EFFECT_OFF)
    {
        controller->SetOff();
        return;
    }

    unsigned char effect_code = (unsigned char)mode_value;
    unsigned char speed;
    unsigned char flag;
    unsigned char param;
    RGBColor      fg;
    RGBColor      bg;

    ComputeModeParameters(effect_code, speed, flag, param, fg, bg);

    controller->SetEffect(effect_code, speed, flag, param, fg, bg);
}

/*---------------------------------------------------------*\
| DeviceSaveMode - persist the active mode to the device's  |
| active profile (0x50 0x55)                                |
|                                                           |
| The 0xc0 painted frame is RAM-only and is not part of the |
| device context, so Direct/Static saves go through the     |
| persistent 0xB0 per-key path first.                       |
\*---------------------------------------------------------*/

void RGBController_DuckyOneSeries::DeviceSaveMode()
{
    unsigned int mode_value = modes[active_mode].value;

    if(mode_value == DUCKY_MODE_DIRECT)
    {
        unsigned char color_buf[DUCKY_ONE_SERIES_COLOR_BUF_SIZE];

        BuildColorBuffer(color_buf);

        controller->SendPerKeyColors(color_buf);
    }
    else if(mode_value == DUCKY_EFFECT_STATIC)
    {
        unsigned char color_buf[DUCKY_ONE_SERIES_COLOR_BUF_SIZE];
        RGBColor      color = ToRGBColor(0xFF, 0xFF, 0xFF);

        if(modes[active_mode].colors.size() > 0)
        {
            color = modes[active_mode].colors[0];
        }

        memset(color_buf, 0x00, sizeof(color_buf));

        for(unsigned int led_idx = 0; led_idx < DUCKY_ONE_SERIES_LED_COUNT; led_idx++)
        {
            /*-------------------------------------------------*\
            | Unwired FWIndex slots stay black                  |
            \*-------------------------------------------------*/
            if(!fwindex_wired[led_idx])
            {
                continue;
            }

            color_buf[(led_idx * 3) + 0] = RGBGetRValue(color);
            color_buf[(led_idx * 3) + 1] = RGBGetGValue(color);
            color_buf[(led_idx * 3) + 2] = RGBGetBValue(color);
        }

        controller->SendPerKeyColors(color_buf);
    }

    controller->SaveActiveProfile();
}
