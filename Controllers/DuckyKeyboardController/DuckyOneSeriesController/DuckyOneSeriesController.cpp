/*---------------------------------------------------------*\
| DuckyOneSeriesController.cpp                              |
|                                                           |
|   Low-level HID driver for the Ducky One Series RGB       |
|   keyboard (DKON1708ST, VID:PID 04d9:0245).               |
|                                                           |
|   All sequences in this file are ported from the          |
|   hardware-verified reference implementation              |
|                                                           |
|   stef.dev@free.fr                            13 Oct 2026 |
|                                                           |
|   This file is part of the OpenRGB project                |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#include <cstring>
#include <thread>
#include <chrono>
#include "DuckyOneSeriesController.h"
#include "StringUtils.h"
#include "LogManager.h"

/*---------------------------------------------------------*\
| Read timeout for a normal request/response pair.          |
| The device answers in ~2 ms; 100 ms only bounds the       |
| worst case.                                               |
\*---------------------------------------------------------*/
#define DUCKY_READ_TIMEOUT_MS       100

/*---------------------------------------------------------*\
| Timeout used when draining an async 0x42 0x20 event       |
| notification.                                             |
\*---------------------------------------------------------*/
#define DUCKY_ASYNC_TIMEOUT_MS      200

DuckyOneSeriesController::DuckyOneSeriesController(hid_device* dev_handle, const char* path, const unsigned short pid, std::string dev_name)
{
    dev             = dev_handle;
    location        = path;
    name            = dev_name;
    usb_pid         = pid;
    active_profile  = 0;
    paint_mode      = false;

    /*-----------------------------------------------------*\
    | Bring the device into a known state without           |
    | disturbing the active profile. The reference          |
    | init sequence's profile-select step is                |
    | deliberately skipped: it would switch the             |
    | user's active profile at OpenRGB startup.             |
    \*-----------------------------------------------------*/
    RunInitSequence();
}

DuckyOneSeriesController::~DuckyOneSeriesController()
{
    /*-----------------------------------------------------*\
    | Leave the session mode untouched on teardown:         |
    | clearing paint mode would re-render the active        |
    | profile's effect over a painted frame, and            |
    | clearing the config session reloads the profile       |
    | blob. The reference tool behaves the same way.        |
    \*-----------------------------------------------------*/
    hid_close(dev);
}

/*---------------------------------------------------------*\
| Getters                                                   |
\*---------------------------------------------------------*/

std::string DuckyOneSeriesController::GetLocationString()
{
    return("HID: " + location);
}

std::string DuckyOneSeriesController::GetNameString()
{
    return(name);
}

unsigned short DuckyOneSeriesController::GetUSBPID()
{
    return(usb_pid);
}

std::string DuckyOneSeriesController::GetSerialString()
{
    wchar_t serial_string[128];
    int ret = hid_get_serial_number_string(dev, serial_string, 128);

    if(ret != 0)
    {
        return("");
    }

    return(StringUtils::wstring_to_string(serial_string));
}

std::string DuckyOneSeriesController::GetFirmwareVersion()
{
    unsigned char usb_buf[DUCKY_ONE_SERIES_REPORT_SIZE];
    std::string   version;

    memset(usb_buf, 0x00, sizeof(usb_buf));

    usb_buf[0x00]   = DUCKY_RPT_DEVICE_QUERY;
    usb_buf[0x01]   = 0x20;

    if(!SendCommand(usb_buf))
    {
        return("");
    }

    /*-----------------------------------------------------*\
    | UTF-16LE version string starts at byte 8              |
    \*-----------------------------------------------------*/
    for(unsigned int idx = 8; (idx + 1) < DUCKY_ONE_SERIES_REPORT_SIZE; idx += 2)
    {
        if(usb_buf[idx] == 0x00 && usb_buf[idx + 1] == 0x00)
        {
            break;
        }

        version += (char)usb_buf[idx];
    }

    return(version);
}

unsigned char DuckyOneSeriesController::GetActiveProfile()
{
    unsigned char usb_buf[DUCKY_ONE_SERIES_REPORT_SIZE];

    memset(usb_buf, 0x00, sizeof(usb_buf));

    usb_buf[0x00]   = DUCKY_RPT_EFFECT_QUERY;
    usb_buf[0x01]   = 0x63;

    if(!SendCommand(usb_buf))
    {
        return(active_profile);
    }

    active_profile = usb_buf[0x08];

    return(active_profile);
}

unsigned char DuckyOneSeriesController::GetActiveEffect()
{
    unsigned char usb_buf[DUCKY_ONE_SERIES_REPORT_SIZE];

    memset(usb_buf, 0x00, sizeof(usb_buf));

    usb_buf[0x00]   = DUCKY_RPT_EFFECT_QUERY;
    usb_buf[0x01]   = 0x63;

    if(!SendCommand(usb_buf))
    {
        return(0x00);
    }

    return(usb_buf[0x09]);
}

/*---------------------------------------------------------*\
| Low-level I/O                                             |
|                                                           |
| Every report is exactly 64 bytes with the command byte    |
| at offset 0 - the interface's HID report descriptor has   |
| no Report ID items. On Linux the 64-byte buffer is        |
| forwarded to the interrupt OUT endpoint as-is; on         |
| Windows hidapi requires the classic 0x00 report-ID        |
| prefix for unnumbered output reports.                     |
\*---------------------------------------------------------*/

void DuckyOneSeriesController::SendReport(const unsigned char* buf)
{
#ifdef _WIN32
    unsigned char usb_buf[DUCKY_ONE_SERIES_REPORT_SIZE + 1];

    usb_buf[0x00]   = 0x00;
    memcpy(&usb_buf[1], buf, DUCKY_ONE_SERIES_REPORT_SIZE);

    int bytes_written = hid_write(dev, usb_buf, DUCKY_ONE_SERIES_REPORT_SIZE + 1);
#else
    int bytes_written = hid_write(dev, buf, DUCKY_ONE_SERIES_REPORT_SIZE);
#endif

    if(bytes_written < 0)
    {
        LOG_DEBUG("[Ducky One Series] hid_write failed: %ls", hid_error(dev));
    }
}

bool DuckyOneSeriesController::ReadResponse(unsigned char* buf, int timeout_ms)
{
    int bytes_read = hid_read_timeout(dev, buf, DUCKY_ONE_SERIES_REPORT_SIZE, timeout_ms);

    if(bytes_read < DUCKY_ONE_SERIES_REPORT_SIZE)
    {
        return(false);
    }

    /*-----------------------------------------------------*\
    | 0xFF 0xAA at bytes 0-1 = command rejected             |
    \*-----------------------------------------------------*/
    if(buf[0x00] == 0xFF && buf[0x01] == 0xAA)
    {
        LOG_DEBUG("[Ducky One Series] command rejected (FF AA)");
        return(false);
    }

    return(true);
}

bool DuckyOneSeriesController::SendCommand(unsigned char* buf)
{
    SendReport(buf);

    if(!ReadResponse(buf, DUCKY_READ_TIMEOUT_MS))
    {
        return(false);
    }

    return(true);
}

void DuckyOneSeriesController::ConsumeAsyncReport(int timeout_ms)
{
    unsigned char read_buf[DUCKY_ONE_SERIES_REPORT_SIZE];

    /*-----------------------------------------------------*\
    | Discard one async 0x42 0x20 event notification        |
    | if one is pending. A timeout is not an error.         |
    \*-----------------------------------------------------*/
    if(ReadResponse(read_buf, timeout_ms))
    {
        LOG_DEBUG("[Ducky One Series] consumed async report 0x%02X 0x%02X",
                  read_buf[0x00], read_buf[0x01]);
    }
}

void DuckyOneSeriesController::SleepMs(int ms)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

/*---------------------------------------------------------*\
| Session modes (report 0x41)                               |
|                                                           |
| Bit 0 (0x01): config session - gates 0x50/0x51-family     |
| Bit 1 (0x02): paint mode - gates 0xc0 and silences the    |
|               0x50/0x51 family while set                  |
\*---------------------------------------------------------*/

void DuckyOneSeriesController::LeaveAllModes()
{
    unsigned char usb_buf[DUCKY_ONE_SERIES_REPORT_SIZE];

    memset(usb_buf, 0x00, sizeof(usb_buf));

    usb_buf[0x00]   = DUCKY_RPT_MODE_SELECT;
    usb_buf[0x01]   = DUCKY_MODE_LEAVE_ALL;

    SendCommand(usb_buf);

    paint_mode = false;
}

void DuckyOneSeriesController::EnterConfigSession()
{
    unsigned char usb_buf[DUCKY_ONE_SERIES_REPORT_SIZE];

    memset(usb_buf, 0x00, sizeof(usb_buf));

    usb_buf[0x00]   = DUCKY_RPT_MODE_SELECT;
    usb_buf[0x01]   = DUCKY_MODE_CONFIG_SESSION;

    SendCommand(usb_buf);
}

unsigned char DuckyOneSeriesController::QuerySessionMode()
{
    unsigned char usb_buf[DUCKY_ONE_SERIES_REPORT_SIZE];

    memset(usb_buf, 0x00, sizeof(usb_buf));

    usb_buf[0x00]   = DUCKY_RPT_MODE_SELECT;
    usb_buf[0x01]   = DUCKY_MODE_QUERY;

    if(!SendCommand(usb_buf))
    {
        return(0x00);
    }

    return(usb_buf[0x01]);
}

void DuckyOneSeriesController::EnsureConfigSession()
{
    /*-----------------------------------------------------*\
    | Paint mode must be left before any 0x50/0x51          |
    | command: while it is set the dispatcher               |
    | silently ignores the whole config family.             |
    \*-----------------------------------------------------*/
    if(paint_mode)
    {
        LeaveAllModes();
        SleepMs(20);
    }

    EnterConfigSession();
}

/*---------------------------------------------------------*\
| Animation stop and effects                                |
\*---------------------------------------------------------*/

void DuckyOneSeriesController::StopAnimation()
{
    unsigned char usb_buf[DUCKY_ONE_SERIES_REPORT_SIZE];

    /*-----------------------------------------------------*\
    | Steps 1-2 of the live protocol with the 0xFE          |
    | marker: stop any running animation.                   |
    \*-----------------------------------------------------*/
    memset(usb_buf, 0x00, sizeof(usb_buf));

    usb_buf[0x00]   = DUCKY_RPT_MODE_SELECT;
    usb_buf[0x01]   = DUCKY_MODE_CONFIG_SESSION;

    SendCommand(usb_buf);

    /*-----------------------------------------------------*\
    | Selecting the config session takes the device         |
    | out of paint mode - keep the local flag in            |
    | sync or a later EnterPaintMode() would skip           |
    | re-entering and the 0xc0 paint ops would be           |
    | ignored by the device.                                |
    \*-----------------------------------------------------*/
    paint_mode = false;

    SleepMs(10);

    memset(usb_buf, 0x00, sizeof(usb_buf));

    usb_buf[0x00]   = DUCKY_RPT_COLOR_DATA;
    usb_buf[0x01]   = 0x28;
    usb_buf[0x04]   = 0xFE;

    SendCommand(usb_buf);
}

void DuckyOneSeriesController::SetOff()
{
    StopAnimation();
}

/*---------------------------------------------------------*\
| SetEffect - live 5-step protocol                          |
|                                                           |
| 1: 0x41 0x01                    enter config session      |
| 2: 0x51 0x28 00 00 [code]       set effect type           |
| 3: 0x51 0x2b 02 00 [entry]      effect entry (layer 2)    |
| 4: 0x52 0x28 00 00               read color data header   |
| 5: 0x51 0x28 00 00 E1 [code@17]  activate                 |
|                                                           |
| Color Wave (0x05) and Rain (0x08) repeat steps 3-5 once   |
| more ~50 ms later - without the repeat the device does    |
| not render the full animation.                            |
\*---------------------------------------------------------*/

void DuckyOneSeriesController::SetEffect
    (
    unsigned char   effect_code,
    unsigned char   speed,
    unsigned char   flag,
    unsigned char   param,
    RGBColor        fg,
    RGBColor        bg
    )
{
    unsigned char usb_buf[DUCKY_ONE_SERIES_REPORT_SIZE];

    EnsureConfigSession();
    SleepMs(10);

    /*-----------------------------------------------------*\
    | Step 2: set effect type                               |
    \*-----------------------------------------------------*/
    memset(usb_buf, 0x00, sizeof(usb_buf));

    usb_buf[0x00]   = DUCKY_RPT_COLOR_DATA;
    usb_buf[0x01]   = 0x28;
    usb_buf[0x04]   = effect_code;

    SendCommand(usb_buf);
    SleepMs(10);

    /*-----------------------------------------------------*\
    | Step 3: effect entry (0x2b)                           |
    \*-----------------------------------------------------*/
    SendEffectEntry(effect_code, speed, flag, param, fg, bg);
    SleepMs(10);

    /*-----------------------------------------------------*\
    | Step 4: read color data header                        |
    \*-----------------------------------------------------*/
    memset(usb_buf, 0x00, sizeof(usb_buf));

    usb_buf[0x00]   = DUCKY_RPT_COLOR_WRITE;
    usb_buf[0x01]   = 0x28;

    SendCommand(usb_buf);
    SleepMs(10);

    /*-----------------------------------------------------*\
    | Step 5: activate (0xE1 marker, code at byte 17)       |
    \*-----------------------------------------------------*/
    SendActivate(0xE1, effect_code);

    /*-----------------------------------------------------*\
    | Double activation for Color Wave and Rain             |
    \*-----------------------------------------------------*/
    if(effect_code == DUCKY_EFFECT_COLOR_WAVE || effect_code == DUCKY_EFFECT_RAIN)
    {
        SleepMs(50);

        SendEffectEntry(effect_code, speed, flag, param, fg, bg);
        SleepMs(10);

        memset(usb_buf, 0x00, sizeof(usb_buf));

        usb_buf[0x00]   = DUCKY_RPT_COLOR_WRITE;
        usb_buf[0x01]   = 0x28;

        SendCommand(usb_buf);
        SleepMs(10);

        SendActivate(0xE1, effect_code);
    }
}

/*---------------------------------------------------------*\
| 0x2b effect entry                                         |
|                                                           |
| Byte 2-3:   0x02 0x00 (layer 2 - the layer the 0xE1       |
|             activation points the renderer at)            |
| Byte 4:     effect code                                   |
| Byte 5:     speed (timing value)                          |
| Byte 6:     flag (bit 7 random, bits 0-5 direction)       |
| Byte 7:     per-effect PARAM (Wave color count, Rain      |
|             spawn modulus - smaller = denser)             |
| Bytes 8-9:  constants 0xFF 0xFF                           |
| Bytes 10-12: foreground RGB                               |
| Bytes 13-15: background RGB                               |
| Bytes 16+:  0xFF padding (zeroed in the device echo)      |
|                                                           |
| Color Cycle (0x03) special case: bytes 8-10 encode the    |
| 5-level brightness (R=0xFF, B=0x00 fixed, G = level) and  |
| bytes 10-15 are zeroed. The fg parameter carries the      |
| brightness triple.                                        |
\*---------------------------------------------------------*/

void DuckyOneSeriesController::SendEffectEntry
    (
    unsigned char   effect_code,
    unsigned char   speed,
    unsigned char   flag,
    unsigned char   param,
    RGBColor        fg,
    RGBColor        bg
    )
{
    unsigned char usb_buf[DUCKY_ONE_SERIES_REPORT_SIZE];

    memset(usb_buf, 0x00, sizeof(usb_buf));

    usb_buf[0x00]   = DUCKY_RPT_COLOR_DATA;
    usb_buf[0x01]   = 0x2b;
    usb_buf[0x02]   = 0x02;
    usb_buf[0x03]   = 0x00;
    usb_buf[0x04]   = effect_code;
    usb_buf[0x05]   = speed;
    usb_buf[0x06]   = flag;
    usb_buf[0x07]   = param;

    /*-----------------------------------------------------*\
    | 0xFF padding from byte 8 to the end of the            |
    | report (matches the reference implementation)         |
    \*-----------------------------------------------------*/
    memset(&usb_buf[0x08], 0xFF, DUCKY_ONE_SERIES_REPORT_SIZE - 0x08);

    if(effect_code == DUCKY_EFFECT_COLOR_CYCLE)
    {
        /*-------------------------------------------------*\
        | Brightness encoding in the bytes 8-10 triple;     |
        | the whole color area 10-15 is zeroed              |
        \*-------------------------------------------------*/
        usb_buf[0x08]   = RGBGetRValue(fg);
        usb_buf[0x09]   = RGBGetGValue(fg);
        usb_buf[0x0A]   = 0x00;
        usb_buf[0x0B]   = 0x00;
        usb_buf[0x0C]   = 0x00;
        usb_buf[0x0D]   = 0x00;
        usb_buf[0x0E]   = 0x00;
        usb_buf[0x0F]   = 0x00;
    }
    else
    {
        /*-------------------------------------------------*\
        | Constants at bytes 8-9, FG at 10-12, BG at 13-15  |
        \*-------------------------------------------------*/
        usb_buf[0x08]   = 0xFF;
        usb_buf[0x09]   = 0xFF;
        usb_buf[0x0A]   = RGBGetRValue(fg);
        usb_buf[0x0B]   = RGBGetGValue(fg);
        usb_buf[0x0C]   = RGBGetBValue(fg);
        usb_buf[0x0D]   = RGBGetRValue(bg);
        usb_buf[0x0E]   = RGBGetGValue(bg);
        usb_buf[0x0F]   = RGBGetBValue(bg);
    }

    SendCommand(usb_buf);
}

/*---------------------------------------------------------*\
| 0x51 0x28 activate packet: marker at byte 4, code at 17   |
\*---------------------------------------------------------*/

void DuckyOneSeriesController::SendActivate(unsigned char marker, unsigned char effect_code)
{
    unsigned char usb_buf[DUCKY_ONE_SERIES_REPORT_SIZE];

    memset(usb_buf, 0x00, sizeof(usb_buf));

    usb_buf[0x00]   = DUCKY_RPT_COLOR_DATA;
    usb_buf[0x01]   = 0x28;
    usb_buf[0x04]   = marker;
    usb_buf[0x11]   = effect_code;

    SendCommand(usb_buf);
}

/*---------------------------------------------------------*\
| Live paint (0xc0) - Direct mode                           |
|                                                           |
| Paint mode blanks the LED frame on entry and gates the    |
| 0xc0 family; the auto-refresh commit makes every paint    |
| op update the matrix immediately. The frame survives      |
| leaving paint mode.                                       |
\*---------------------------------------------------------*/

void DuckyOneSeriesController::EnterPaintMode()
{
    unsigned char usb_buf[DUCKY_ONE_SERIES_REPORT_SIZE];

    if(paint_mode)
    {
        return;
    }

    /*-----------------------------------------------------*\
    | 0xc0 paint ops require no animation running           |
    \*-----------------------------------------------------*/
    StopAnimation();
    SleepMs(20);

    /*-----------------------------------------------------*\
    | Enter paint mode (blanks the frame)                   |
    \*-----------------------------------------------------*/
    memset(usb_buf, 0x00, sizeof(usb_buf));

    usb_buf[0x00]   = DUCKY_RPT_MODE_SELECT;
    usb_buf[0x01]   = DUCKY_MODE_PAINT;

    SendCommand(usb_buf);
    SleepMs(20);

    paint_mode = true;

    /*-----------------------------------------------------*\
    | Commit with auto-refresh enabled                      |
    \*-----------------------------------------------------*/
    memset(usb_buf, 0x00, sizeof(usb_buf));

    usb_buf[0x00]   = DUCKY_RPT_LIVE_PAINT;
    usb_buf[0x01]   = 0xF0;
    usb_buf[0x04]   = 0x01;

    SendCommand(usb_buf);
}

void DuckyOneSeriesController::PaintFill(RGBColor color)
{
    unsigned char usb_buf[DUCKY_ONE_SERIES_REPORT_SIZE];

    memset(usb_buf, 0x00, sizeof(usb_buf));

    usb_buf[0x00]   = DUCKY_RPT_LIVE_PAINT;
    usb_buf[0x01]   = 0x00;
    usb_buf[0x04]   = RGBGetRValue(color);
    usb_buf[0x05]   = RGBGetGValue(color);
    usb_buf[0x06]   = RGBGetBValue(color);

    SendCommand(usb_buf);
}

void DuckyOneSeriesController::PaintKeys(const std::vector<std::pair<unsigned char, RGBColor>>& colors)
{
    unsigned char usb_buf[DUCKY_ONE_SERIES_REPORT_SIZE];

    for(unsigned int offset = 0; offset < colors.size(); offset += DUCKY_PAINT_MAX_KEYS_PER_REPORT)
    {
        unsigned int count = colors.size() - offset;

        if(count > DUCKY_PAINT_MAX_KEYS_PER_REPORT)
        {
            count = DUCKY_PAINT_MAX_KEYS_PER_REPORT;
        }

        memset(usb_buf, 0x00, sizeof(usb_buf));

        usb_buf[0x00]   = DUCKY_RPT_LIVE_PAINT;
        usb_buf[0x01]   = 0x01;
        usb_buf[0x02]   = count;

        for(unsigned int idx = 0; idx < count; idx++)
        {
            const std::pair<unsigned char, RGBColor>& entry = colors[offset + idx];

            usb_buf[0x04 + (idx * 4)] = entry.first;
            usb_buf[0x05 + (idx * 4)] = RGBGetRValue(entry.second);
            usb_buf[0x06 + (idx * 4)] = RGBGetGValue(entry.second);
            usb_buf[0x07 + (idx * 4)] = RGBGetBValue(entry.second);
        }

        SendCommand(usb_buf);
        SleepMs(5);
    }
}

void DuckyOneSeriesController::LeavePaintMode()
{
    LeaveAllModes();
}

/*---------------------------------------------------------*\
| SetStatic - solid color via the live paint path           |
|                                                           |
| Stop any animation, paint the whole frame with the mode   |
| color. The device is deliberately LEFT IN PAINT MODE -    |
| the frame only renders while paint mode is active, and    |
| the hardware-verified reference never send 0x41 0x00      |
| after painting: clearing the mode bits re-renders the     |
| active profile's effect over the painted frame.           |
| EnsureConfigSession() leaves paint mode before any        |
| 0x50/0x51-family command.                                 |
\*---------------------------------------------------------*/

void DuckyOneSeriesController::SetStatic(RGBColor color)
{
    EnterPaintMode();

    PaintFill(color);
}

/*---------------------------------------------------------*\
| Persistent per-key mode (0xB0) - multi-layer upload       |
|                                                           |
| The 384-byte buffer holds each key's RGB at FWIndex * 3   |
| and is sent as 8 layers of 48 bytes (layers 0, 2, 4, 6,   |
| 8, 10, 12, 13 - layer 13 carries a rearranged slice).     |
\*---------------------------------------------------------*/

void DuckyOneSeriesController::SendCommit(unsigned char slot)
{
    unsigned char usb_buf[DUCKY_ONE_SERIES_REPORT_SIZE];

    memset(usb_buf, 0x00, sizeof(usb_buf));

    usb_buf[0x00]   = DUCKY_RPT_COLOR_DATA;
    usb_buf[0x01]   = 0x30;
    usb_buf[0x04]   = slot;

    SendCommand(usb_buf);
}

void DuckyOneSeriesController::SendMultiLayer(unsigned char layer, const unsigned char* data48)
{
    unsigned char usb_buf[DUCKY_ONE_SERIES_REPORT_SIZE];

    memset(usb_buf, 0x00, sizeof(usb_buf));

    usb_buf[0x00]   = DUCKY_RPT_COLOR_DATA;
    usb_buf[0x01]   = 0xA8;
    usb_buf[0x02]   = layer;

    memcpy(&usb_buf[0x04], data48, 48);

    SendCommand(usb_buf);
}

void DuckyOneSeriesController::SendPerKeyColors(const unsigned char* color_buf_384)
{
    unsigned char usb_buf[DUCKY_ONE_SERIES_REPORT_SIZE];
    unsigned char layer_data[48];

    EnsureConfigSession();
    SleepMs(10);

    /*-----------------------------------------------------*\
    | Commit previous state                                 |
    \*-----------------------------------------------------*/
    SendCommit(0x00);
    SleepMs(10);

    /*-----------------------------------------------------*\
    | 8 multi-layer packets from the 384-byte buffer        |
    |                                                       |
    | Layer 13 carries a rearranged slice: bytes 0-23       |
    | are the second half of layer 12's range, bytes        |
    | 24-47 are the first half of layer 13's own range      |
    \*-----------------------------------------------------*/
    static const unsigned char layer_indices[]  = { 0, 2, 4, 6, 8, 10, 12, 13 };
    static const unsigned int  buffer_offsets[] = { 0, 48, 96, 144, 192, 240, 288, 336 };

    for(unsigned int idx = 0; idx < sizeof(layer_indices); idx++)
    {
        if(layer_indices[idx] == 13)
        {
            memset(layer_data, 0x00, sizeof(layer_data));

            memcpy(&layer_data[0x00],  &color_buf_384[312], 24);
            memcpy(&layer_data[24],    &color_buf_384[336], 24);
        }
        else
        {
            memcpy(layer_data, &color_buf_384[buffer_offsets[idx]], 48);
        }

        SendMultiLayer(layer_indices[idx], layer_data);
        SleepMs(5);
    }

    /*-----------------------------------------------------*\
    | Commit multi-layer data (slot select byte 2)          |
    \*-----------------------------------------------------*/
    SendCommit(0x01);
    SleepMs(10);

    /*-----------------------------------------------------*\
    | Config header with effect type 0xB0                   |
    \*-----------------------------------------------------*/
    memset(usb_buf, 0x00, sizeof(usb_buf));

    usb_buf[0x00]   = DUCKY_RPT_COLOR_DATA;
    usb_buf[0x01]   = 0x29;
    usb_buf[0x02]   = 0x01;
    usb_buf[0x03]   = 0x00;

    for(unsigned int idx = 4; idx < 22; idx++)
    {
        usb_buf[idx] = 0xFF;
    }

    usb_buf[0x04]   = DUCKY_EFFECT_CUSTOM_PER_KEY;

    SendCommand(usb_buf);
    SleepMs(10);

    /*-----------------------------------------------------*\
    | 0x2b effect entry with effect type 0xB0               |
    \*-----------------------------------------------------*/
    memset(usb_buf, 0x00, sizeof(usb_buf));

    usb_buf[0x00]   = DUCKY_RPT_COLOR_DATA;
    usb_buf[0x01]   = 0x2b;
    usb_buf[0x02]   = 0x02;
    usb_buf[0x03]   = 0x00;

    for(unsigned int idx = 4; idx < 22; idx++)
    {
        usb_buf[idx] = 0xFF;
    }

    usb_buf[0x04]   = DUCKY_EFFECT_CUSTOM_PER_KEY;

    SendCommand(usb_buf);
    SleepMs(10);

    /*-----------------------------------------------------*\
    | Read color data header                                |
    \*-----------------------------------------------------*/
    memset(usb_buf, 0x00, sizeof(usb_buf));

    usb_buf[0x00]   = DUCKY_RPT_COLOR_WRITE;
    usb_buf[0x01]   = 0x28;

    SendCommand(usb_buf);
    SleepMs(10);

    /*-----------------------------------------------------*\
    | Activate the 0xB0 custom per-key mode                 |
    \*-----------------------------------------------------*/
    SendActivate(0xE1, DUCKY_EFFECT_CUSTOM_PER_KEY);
    SleepMs(10);

    /*-----------------------------------------------------*\
    | Effect status query - also drains pending             |
    | group-0 events                                        |
    \*-----------------------------------------------------*/
    memset(usb_buf, 0x00, sizeof(usb_buf));

    usb_buf[0x00]   = DUCKY_RPT_EFFECT_QUERY;
    usb_buf[0x01]   = 0x63;

    SendCommand(usb_buf);
}

/*---------------------------------------------------------*\
| Profiles (report 0x50 family)                             |
\*---------------------------------------------------------*/

void DuckyOneSeriesController::SelectProfile(unsigned char index)
{
    unsigned char usb_buf[DUCKY_ONE_SERIES_REPORT_SIZE];

    if(index > 5)
    {
        return;
    }

    EnsureConfigSession();
    SleepMs(10);

    memset(usb_buf, 0x00, sizeof(usb_buf));

    usb_buf[0x00]   = DUCKY_RPT_COLOR_DATA;
    usb_buf[0x01]   = 0x00;
    usb_buf[0x04]   = index;

    SendCommand(usb_buf);

    /*-----------------------------------------------------*\
    | The device emits an async 0x42 0x20                   |
    | (profile-switched) event right after this             |
    | command - it must be consumed or it will              |
    | desynchronize the next request/response pair          |
    \*-----------------------------------------------------*/
    ConsumeAsyncReport(DUCKY_ASYNC_TIMEOUT_MS);
    SleepMs(50);
}

void DuckyOneSeriesController::SaveActiveProfile()
{
    unsigned char usb_buf[DUCKY_ONE_SERIES_REPORT_SIZE];

    EnsureConfigSession();
    SleepMs(10);

    memset(usb_buf, 0x00, sizeof(usb_buf));

    usb_buf[0x00]   = DUCKY_RPT_PROFILE_FAMILY;
    usb_buf[0x01]   = 0x55;

    SendCommand(usb_buf);
}

void DuckyOneSeriesController::ReloadActiveProfile()
{
    unsigned char usb_buf[DUCKY_ONE_SERIES_REPORT_SIZE];

    EnsureConfigSession();
    SleepMs(10);

    memset(usb_buf, 0x00, sizeof(usb_buf));

    usb_buf[0x00]   = DUCKY_RPT_PROFILE_FAMILY;
    usb_buf[0x01]   = 0x00;

    SendCommand(usb_buf);
}

/*---------------------------------------------------------*\
| Init sequence                                             |
|                                                           |
| Advisory state-sync setup, run once at construction.      |
| Subscribes to group-0 device events (profile switched,    |
| effect changed, auto-persisted) and primes the config     |
| context. The reference tool's profile-select step is      |
| skipped on purpose: it would switch the user's active     |
| profile at OpenRGB startup.                               |
\*---------------------------------------------------------*/

void DuckyOneSeriesController::RunInitSequence()
{
    unsigned char usb_buf[DUCKY_ONE_SERIES_REPORT_SIZE];

    /*-----------------------------------------------------*\
    | 1. Subscribe to group-0 events (bits 0, 3, 4)         |
    \*-----------------------------------------------------*/
    memset(usb_buf, 0x00, sizeof(usb_buf));

    usb_buf[0x00]   = DUCKY_RPT_EVENT_SUB;
    usb_buf[0x01]   = 0x00;
    usb_buf[0x04]   = 0x01;
    usb_buf[0x05]   = 0x00;
    usb_buf[0x06]   = 0x00;
    usb_buf[0x07]   = 0x01;
    usb_buf[0x08]   = 0x01;
    usb_buf[0x09]   = 0x00;
    usb_buf[0x0A]   = 0x00;
    usb_buf[0x0B]   = 0x00;

    SendCommand(usb_buf);
    SleepMs(10);

    /*-----------------------------------------------------*\
    | 2. Effect status query (ACKs pending group-0          |
    |    events; response byte 8 = active profile)          |
    \*-----------------------------------------------------*/
    memset(usb_buf, 0x00, sizeof(usb_buf));

    usb_buf[0x00]   = DUCKY_RPT_EFFECT_QUERY;
    usb_buf[0x01]   = 0x63;

    if(SendCommand(usb_buf))
    {
        active_profile = usb_buf[0x08];
    }

    SleepMs(10);

    /*-----------------------------------------------------*\
    | 3. Enter config session                               |
    \*-----------------------------------------------------*/
    memset(usb_buf, 0x00, sizeof(usb_buf));

    usb_buf[0x00]   = DUCKY_RPT_MODE_SELECT;
    usb_buf[0x01]   = DUCKY_MODE_CONFIG_SESSION;

    SendCommand(usb_buf);
    SleepMs(10);

    /*-----------------------------------------------------*\
    | 4. Color write init                                   |
    \*-----------------------------------------------------*/
    memset(usb_buf, 0x00, sizeof(usb_buf));

    usb_buf[0x00]   = DUCKY_RPT_COLOR_WRITE;
    usb_buf[0x01]   = 0x00;

    SendCommand(usb_buf);
    SleepMs(10);

    /*-----------------------------------------------------*\
    | 5. Color write init2                                  |
    \*-----------------------------------------------------*/
    memset(usb_buf, 0x00, sizeof(usb_buf));

    usb_buf[0x00]   = DUCKY_RPT_COLOR_WRITE;
    usb_buf[0x01]   = 0x01;
    usb_buf[0x02]   = 0x01;

    SendCommand(usb_buf);
    SleepMs(10);

    /*-----------------------------------------------------*\
    | 6. Config header read                                 |
    \*-----------------------------------------------------*/
    memset(usb_buf, 0x00, sizeof(usb_buf));

    usb_buf[0x00]   = DUCKY_RPT_COLOR_WRITE;
    usb_buf[0x01]   = 0x29;
    usb_buf[0x02]   = 0x01;

    SendCommand(usb_buf);
    SleepMs(10);

    /*-----------------------------------------------------*\
    | 7. Color data header read                             |
    \*-----------------------------------------------------*/
    memset(usb_buf, 0x00, sizeof(usb_buf));

    usb_buf[0x00]   = DUCKY_RPT_COLOR_WRITE;
    usb_buf[0x01]   = 0x28;

    SendCommand(usb_buf);
    SleepMs(10);
}
