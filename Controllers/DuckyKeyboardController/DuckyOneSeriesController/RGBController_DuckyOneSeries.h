/*---------------------------------------------------------*\
| RGBController_DuckyOneSeries.h                            |
|                                                           |
|   RGBController for the Ducky One Series RGB keyboard     |
|   (DKON1708ST, VID:PID 04d9:0245).                        |
|                                                           |
|   stef.dev@free.fr                            13 Oct 2026 |
|                                                           |
|   This file is part of the OpenRGB project                |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#pragma once

#include "RGBController.h"
#include "DuckyOneSeriesController.h"

class RGBController_DuckyOneSeries : public RGBController
{
public:
    RGBController_DuckyOneSeries(DuckyOneSeriesController* controller_ptr);
    ~RGBController_DuckyOneSeries();

    void        SetAllColors(RGBColor color);

    void        SetupZones();

    void        ResizeZone(int zone, int new_size);

    void        DeviceUpdateLEDs();
    void        DeviceUpdateZoneLEDs(int zone);
    void        DeviceUpdateSingleLED(int led);

    void        DeviceUpdateMode();

    void        DeviceSaveMode();

private:
    DuckyOneSeriesController* controller;
    unsigned int              keyboard_layout;  /* 0 = EU ISO, 1 = US ANSI */
    bool                      fwindex_wired[120];/* FWIndex -> wired LED for the layout */

    void        ComputeModeParameters
                    (
                    unsigned char   effect_code,
                    unsigned char&  speed,
                    unsigned char&  flag,
                    unsigned char&  param,
                    RGBColor&       fg,
                    RGBColor&       bg
                    );
    void        BuildColorBuffer(unsigned char* color_buf_384);
    void        PaintDirectColors();
};
