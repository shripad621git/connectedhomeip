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

#include "Speaker.h"

#include "A2dpSink.h"

#include <app/server/Server.h>
#include <devices/Types.h>
#include <lib/support/CodeUtils.h>
#include <lib/support/logging/CHIPLogging.h>
#include <platform/CHIPDeviceLayer.h>

using namespace chip::app::Clusters;

namespace chip::app::SpeakerApp {

namespace {

// Volume of a speaker that was never set by a controller
constexpr uint8_t kInitialLevel = 100;

// The fabric table is loaded by Server::Init(), which runs after the device is created,
// so a node commissioned before this boot is first recognised at kServerReady.
void StartSinkOnceOnFabric(const DeviceLayer::ChipDeviceEvent * event, intptr_t)
{
    const bool joinedNow = event->Type == DeviceLayer::DeviceEventType::kCommissioningComplete;
    const bool alreadyOnAFabric =
        event->Type == DeviceLayer::DeviceEventType::kServerReady && Server::GetInstance().GetFabricTable().FabricCount() > 0;
    VerifyOrReturn(joinedNow || alreadyOnAFabric);

    int err = A2dpSinkStart();
    VerifyOrReturn(err == 0, ChipLogError(DeviceLayer, "Speaker: A2DP sink start failed: %d", err));
}

} // namespace

Speaker::Speaker(TimerDelegate & timerDelegate) :
    DeviceEndpoint(Span<const DataModel::DeviceTypeEntry>(&Device::Type::kSpeaker, 1)), mTimerDelegate(timerDelegate)
{
    LogErrorOnFailure(DeviceLayer::PlatformMgr().AddEventHandler(StartSinkOnceOnFabric, 0));
}

Speaker::~Speaker()
{
    DeviceLayer::PlatformMgr().RemoveEventHandler(StartSinkOnceOnFabric, 0);
}

CHIP_ERROR Speaker::Register(EndpointId endpoint, CodeDrivenDataModelProvider & provider)
{
    DeviceRegistrationTransaction transaction(*this, provider);

    ReturnErrorOnFailure(RegisterDescriptor(endpoint, provider, DataModel::EndpointCompositionPattern::kTree));

    // On/Off is the mute state: on means not muted
    OnOffCluster::Context onOffContext{ mTimerDelegate };
    onOffContext.defaults.onOff = true;
    mOnOffCluster.Create(endpoint, onOffContext);
    mOnOffCluster.Cluster().AddDelegate(this);
    ReturnErrorOnFailure(provider.AddCluster(mOnOffCluster.Registration()));

    // Level Control is the volume. The Lighting feature limits the level to 1..254,
    // which is the volume range of the A2DP sink.
    LevelControlCluster::Config levelConfig(mTimerDelegate, *this);
    levelConfig.WithOnOffCluster(mOnOffCluster.Cluster());
    levelConfig.WithInitialCurrentLevel(kInitialLevel);
    levelConfig.WithOnOffTransitionTime(0);
    levelConfig.WithOnTransitionTime(0);
    levelConfig.WithOffTransitionTime(0);
    levelConfig.WithLighting(DataModel::NullNullable);
    levelConfig.WithDefaultMoveRate(DataModel::NullNullable);
    mLevelControlCluster.Create(endpoint, levelConfig);
    ReturnErrorOnFailure(provider.AddCluster(mLevelControlCluster.Registration()));

    ReturnErrorOnFailure(provider.AddEndpoint(mEndpointRegistration));

    transaction.Commit();
    return CHIP_NO_ERROR;
}

void Speaker::Unregister(CodeDrivenDataModelProvider & provider)
{
    UnregisterDescriptor(provider);

    if (mLevelControlCluster.IsConstructed())
    {
        LogErrorOnFailure(provider.RemoveCluster(&mLevelControlCluster.Cluster()));
        mLevelControlCluster.Destroy();
    }
    if (mOnOffCluster.IsConstructed())
    {
        mOnOffCluster.Cluster().RemoveDelegate(this);
        LogErrorOnFailure(provider.RemoveCluster(&mOnOffCluster.Cluster()));
        mOnOffCluster.Destroy();
    }
}

void Speaker::OnLevelChanged(uint8_t value)
{
    UpdateSinkVolume();
}

void Speaker::OnOffStartup(bool on)
{
    UpdateSinkVolume();
}

void Speaker::OnOnOffChanged(bool on)
{
    UpdateSinkVolume();
}

void Speaker::UpdateSinkVolume()
{
    VerifyOrReturn(mOnOffCluster.IsConstructed() && mLevelControlCluster.IsConstructed());

    const bool muted     = !mOnOffCluster.Cluster().GetOnOff();
    const uint8_t level  = mLevelControlCluster.Cluster().GetCurrentLevel().ValueOr(LevelControlCluster::kMaxLevel);
    const uint8_t volume = muted ? 0 : level;

    A2dpSinkSetVolume(volume);
    ChipLogProgress(DeviceLayer, "Speaker: volume %u (muted %d, level %u)", volume, muted, level);
}

} // namespace chip::app::SpeakerApp
