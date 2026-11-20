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

#include "devices/ZephyrSpeaker.h"

#include "devices/A2dpSink.h"

#include <app/server/Server.h>
#include <lib/support/CodeUtils.h>
#include <lib/support/logging/CHIPLogging.h>
#include <platform/CHIPDeviceLayer.h>

namespace chip::app::AllDevices {

namespace {

// The fabric table is loaded by Server::Init(), which runs after the device is created,
// so a node commissioned before this boot is first recognised at kServerReady.
void StartSinkOnceOnFabric(const DeviceLayer::ChipDeviceEvent * event, intptr_t)
{
    const bool joinedNow = event->Type == DeviceLayer::DeviceEventType::kCommissioningComplete;
    const bool alreadyOnAFabric =
        event->Type == DeviceLayer::DeviceEventType::kServerReady && Server::GetInstance().GetFabricTable().FabricCount() > 0;
    VerifyOrReturn(joinedNow || alreadyOnAFabric);

    int err = A2dpSinkStart();
    VerifyOrReturn(err == 0, ChipLogError(DeviceLayer, "ZephyrSpeaker: A2DP sink start failed: %d", err));
}

} // namespace

ZephyrSpeaker::ZephyrSpeaker(const Context & context) : Speaker(*this, *this, context.timerDelegate)
{
    LogErrorOnFailure(DeviceLayer::PlatformMgr().AddEventHandler(StartSinkOnceOnFabric, 0));
}

ZephyrSpeaker::~ZephyrSpeaker()
{
    DeviceLayer::PlatformMgr().RemoveEventHandler(StartSinkOnceOnFabric, 0);
}

void ZephyrSpeaker::OnLevelChanged(uint8_t value)
{
    UpdateSinkVolume();
}

void ZephyrSpeaker::OnOffStartup(bool on)
{
    UpdateSinkVolume();
}

void ZephyrSpeaker::OnOnOffChanged(bool on)
{
    UpdateSinkVolume();
}

void ZephyrSpeaker::UpdateSinkVolume()
{
    const bool muted     = !OnOffCluster().GetOnOff();
    const uint8_t level  = LevelControlCluster().GetCurrentLevel().ValueOr(Clusters::LevelControlCluster::kMaxLevel);
    const uint8_t volume = muted ? 0 : level;

    A2dpSinkSetVolume(volume);
    ChipLogProgress(DeviceLayer, "ZephyrSpeaker: volume %u (muted %d, level %u)", volume, muted, level);
}

} // namespace chip::app::AllDevices
