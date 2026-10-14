/*---------------------------------------------------------*\
| DuckyOneSeriesControllerDetect.cpp                        |
|                                                           |
|   Detector for the Ducky One Series RGB keyboard          |
|   (DKON1708ST, VID:PID 04d9:0245).                        |
|                                                           |
|   The RGB configuration interface is USB interface 1      |
|   (vendor-specific HID, usage page 0xFF00). The IAP       |
|   firmware-flashing PIDs (normal PID + 0x1000) are a      |
|   different protocol and must never be matched here.      |
|                                                           |
|   stef.dev@free.fr                            13 Oct 2026 |
|                                                           |
|   This file is part of the OpenRGB project                |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#include <hidapi.h>
#include "DetectionManager.h"
#include "DuckyOneSeriesController.h"
#include "RGBController_DuckyOneSeries.h"

DetectedControllers DetectDuckyOneSeriesControllers(hid_device_info* info, const std::string& name)
{
    DetectedControllers    detected_controllers;
    hid_device*            dev;

    dev = hid_open_path(info->path);

    if(dev)
    {
        DuckyOneSeriesController*     controller     = new DuckyOneSeriesController(dev, info->path, info->product_id, name);
        RGBController_DuckyOneSeries* rgb_controller = new RGBController_DuckyOneSeries(controller);

        detected_controllers.push_back(rgb_controller);
    }

    return(detected_controllers);
}

/*---------------------------------------------------------*\
| Ducky One Series RGB (DKON1708ST) - verified on hardware  |
\*---------------------------------------------------------*/
REGISTER_HID_DETECTOR_IP("Ducky One Series RGB", DetectDuckyOneSeriesControllers, DUCKY_VID, DUCKY_ONE_SERIES_RGB_PID, 1, 0xFF00);

/*---------------------------------------------------------*\
| Sibling models sharing the Windows software and firmware  |
| family, but NOT live-verified. Registered disabled until  |
| someone confirms them on hardware:                        |
|                                                           |
| REGISTER_HID_DETECTOR_IP_DISABLED("Ducky (0x0251)",       |
|     DetectDuckyOneSeriesControllers, DUCKY_VID, 0x0251,   |
|     1, 0xFF00);                                           |
|                                                           |
| REGISTER_HID_DETECTOR_IP_DISABLED("Ducky (0x0288)",       |
|     DetectDuckyOneSeriesControllers, DUCKY_VID, 0x0288,   |
|     1, 0xFF00);                                           |
\*---------------------------------------------------------*/
