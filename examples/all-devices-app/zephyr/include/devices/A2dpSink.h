/*
 *
 *    Copyright (c) 2026 Project CHIP Authors
 *    All rights reserved.
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

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// Registers the SBC sink endpoint and the Audio Sink SDP record, then makes the
/// device connectable and discoverable over Bluetooth Classic.
///
/// Requires an enabled Bluetooth stack. Returns 0 or a negative errno; calling it
/// again after a success returns 0 without doing anything.
int A2dpSinkStart(void);

/// Sets the playback volume, 0 (silent) to 254.
void A2dpSinkSetVolume(uint8_t volume);

#ifdef __cplusplus
}
#endif
