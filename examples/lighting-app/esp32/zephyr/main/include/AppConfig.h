/*
 *
 *    Copyright (c) 2024 Project CHIP Authors
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 */

#pragma once

// Matter endpoint that hosts the On/Off Light (endpoint 1 in lighting-app.zap).
#define LIGHT_ENDPOINT_ID 1

// The light is driven by the `led0` alias defined in the board overlay.
// On the ESP32-C6 the on-board LED is a WS2812 (addressable, GPIO8) which needs
// the led_strip API and is a strapping pin; this example instead expects a plain
// LED wired to a non-strapping GPIO via the overlay (see boards/*.overlay).
// Strapping pins to avoid: GPIO 4, 5, 8, 9, 15.
#define LIGHT_LED_ALIAS led0
