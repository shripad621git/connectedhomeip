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

#include <lib/core/CHIPError.h>

namespace chip::app::SpeakerApp::FactoryResetButton {

/// Configures the button of the `sw0` devicetree alias: holding it for 5 seconds and
/// releasing it resets the node to factory state. Does nothing on a board without `sw0`.
CHIP_ERROR Init();

} // namespace chip::app::SpeakerApp::FactoryResetButton
