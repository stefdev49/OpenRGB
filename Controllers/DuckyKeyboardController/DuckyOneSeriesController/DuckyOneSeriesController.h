/*---------------------------------------------------------*\
| DuckyOneSeriesController.h                                |
|                                                           |
|   Low-level HID driver for the Ducky One Series RGB       |
|   keyboard (DKON1708ST, VID:PID 04d9:0245).               |
|                                                           |
|   Protocol: 64-byte reports, command byte at offset 0.    |
|   0x41 session-mode handshake, 0x51/0x52 effect config,   |
|   0xc0 live frame painting, 0x50 profile/persist family.  |
|                                                           |
|   stef.dev@free.fr                            13 Oct 2026 |
|                                                           |
|   This file is part of the OpenRGB project                |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#pragma once

#include <string>
#include <vector>
#include <utility>
#include <hidapi.h>
#include "RGBController.h"

/*---------------------------------------------------------*\
| Ducky vendor ID (shared with the Ducky One 2              |
| controller in Controllers/DuckyKeyboardController;        |
| guard against a duplicate define)                         |
\*---------------------------------------------------------*/
#ifndef DUCKY_VID
#define DUCKY_VID                       0x04D9
#endif

/*---------------------------------------------------------*\
| Keyboard product IDs                                      |
\*---------------------------------------------------------*/
#define DUCKY_ONE_SERIES_RGB_PID        0x0245

/*---------------------------------------------------------*\
| Report size                                               |
\*---------------------------------------------------------*/
#define DUCKY_ONE_SERIES_REPORT_SIZE    64

/*---------------------------------------------------------*\
| Report IDs                                                |
\*---------------------------------------------------------*/
#define DUCKY_RPT_DEVICE_QUERY          0x12
#define DUCKY_RPT_EFFECT_QUERY          0x40
#define DUCKY_RPT_MODE_SELECT           0x41
#define DUCKY_RPT_EVENT_SUB             0x42
#define DUCKY_RPT_PROFILE_FAMILY        0x50
#define DUCKY_RPT_COLOR_DATA            0x51
#define DUCKY_RPT_COLOR_WRITE           0x52
#define DUCKY_RPT_LIVE_PAINT            0xc0

/*---------------------------------------------------------*\
| Session mode bytes (report 0x41 payload)                  |
\*---------------------------------------------------------*/
#define DUCKY_MODE_LEAVE_ALL            0x00
#define DUCKY_MODE_CONFIG_SESSION       0x01
#define DUCKY_MODE_PAINT                0x02
#define DUCKY_MODE_QUERY                0xFF

/*---------------------------------------------------------*\
| Effect codes                                              |
\*---------------------------------------------------------*/
#define DUCKY_EFFECT_STATIC             0x00
#define DUCKY_EFFECT_FULL_RANDOM        0x01
#define DUCKY_EFFECT_BREATHING          0x02
#define DUCKY_EFFECT_COLOR_CYCLE        0x03
#define DUCKY_EFFECT_REACTIVE           0x04
#define DUCKY_EFFECT_COLOR_WAVE         0x05
#define DUCKY_EFFECT_AURORA             0x06
#define DUCKY_EFFECT_RIPPLE             0x07
#define DUCKY_EFFECT_RAIN               0x08
#define DUCKY_EFFECT_SNAKE              0x09
#define DUCKY_EFFECT_CUSTOM_PER_KEY     0xB0
#define DUCKY_EFFECT_OFF                0xFE

/*---------------------------------------------------------*\
| Live paint (0xc0) limits                                  |
\*---------------------------------------------------------*/
#define DUCKY_PAINT_MAX_KEYS_PER_REPORT  15

/*---------------------------------------------------------*\
| Per-key color buffer: 128 FWIndex slots x 3 bytes RGB     |
\*---------------------------------------------------------*/
#define DUCKY_ONE_SERIES_COLOR_BUF_SIZE 384

class DuckyOneSeriesController
{
public:
    DuckyOneSeriesController(hid_device* dev_handle, const char* path, const unsigned short pid, std::string dev_name);
    ~DuckyOneSeriesController();

    std::string     GetLocationString();
    std::string     GetNameString();
    std::string     GetSerialString();
    unsigned short  GetUSBPID();

    /*-----------------------------------------------------*\
    | Queries                                               |
    \*-----------------------------------------------------*/
    std::string     GetFirmwareVersion();       /* 0x12 0x20, UTF-16LE at byte 8  */
    unsigned char   GetActiveProfile();         /* 0x40 0x63 response byte 8      */
    unsigned char   GetActiveEffect();          /* 0x40 0x63 response byte 9      */

    /*-----------------------------------------------------*\
    | Effects (live 5-step protocol)                        |
    |                                                       |
    | fg/bg are RGB triples. For Color Cycle (0x03) fg      |
    | carries the brightness encoding instead:              |
    | fg = { 0xFF, brightness G level, 0x00 }.              |
    \*-----------------------------------------------------*/
    void            SetEffect
                        (
                        unsigned char   effect_code,
                        unsigned char   speed,
                        unsigned char   flag,
                        unsigned char   param,
                        RGBColor        fg,
                        RGBColor        bg
                        );
    void            SetOff();                    /* 0x41 0x01 + 0x28 SET 0xFE    */
    void            SetStatic(RGBColor color);   /* stop animation + paint fill  */

    /*-----------------------------------------------------*\
    | Live paint (0xc0) - Direct mode                       |
    \*-----------------------------------------------------*/
    void            EnterPaintMode();            /* stop animation + 0x41 0x02 + 0xc0 0xF0 commit */
    void            PaintFill(RGBColor color);   /* 0xc0 0x00, R/G/B at bytes 4/5/6              */
    void            PaintKeys(const std::vector<std::pair<unsigned char, RGBColor>>& colors);
    void            LeavePaintMode();            /* 0x41 0x00, frame stays as painted            */

    /*-----------------------------------------------------*\
    | Persistent per-key mode (0xB0 multi-layer upload)     |
    \*-----------------------------------------------------*/
    void            SendPerKeyColors(const unsigned char* color_buf_384);

    /*-----------------------------------------------------*\
    | Profiles (report 0x50 family)                         |
    \*-----------------------------------------------------*/
    void            SelectProfile(unsigned char index);
    void            SaveActiveProfile();          /* 0x50 0x55 */
    void            ReloadActiveProfile();         /* 0x50 0x00 */

    /*-----------------------------------------------------*\
    | Init sequence                                         |
    \*-----------------------------------------------------*/
    void            RunInitSequence();

private:
    hid_device*             dev;
    std::string             location;
    std::string             name;
    unsigned short          usb_pid;
    unsigned char           active_profile;
    bool                    paint_mode;

    void            SendReport(const unsigned char* buf);
    bool            ReadResponse(unsigned char* buf, int timeout_ms);
    bool            SendCommand(unsigned char* buf);
    void            ConsumeAsyncReport(int timeout_ms);
    void            SleepMs(int ms);
    void            LeaveAllModes();
    void            EnterConfigSession();
    unsigned char   QuerySessionMode();
    void            EnsureConfigSession();
    void            SendCommit(unsigned char slot);
    void            SendMultiLayer(unsigned char layer, const unsigned char* data48);
    void            SendActivate(unsigned char marker, unsigned char effect_code);
    void            SendEffectEntry(unsigned char effect_code, unsigned char speed,
                                    unsigned char flag, unsigned char param,
                                    RGBColor fg, RGBColor bg);
    void            StopAnimation();
};
