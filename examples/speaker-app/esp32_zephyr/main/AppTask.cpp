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

#include "AppTask.h"

#include "FactoryResetButton.h"

#include <app/EventManagement.h>
#include <app/InteractionModelEngine.h>
#include <app/server/Dnssd.h>
#include <credentials/DeviceAttestationCredsProvider.h>
#include <credentials/examples/DeviceAttestationCredsExample.h>
#include <lib/support/CHIPMem.h>
#include <lib/support/CodeUtils.h>
#include <platform/CHIPDeviceLayer.h>
#include <platform/DeviceControlServer.h>
#include <platform/DiagnosticDataProvider.h>
#include <platform/Zephyr/DeviceInstanceInfoProviderImpl.h>
#include <platform/Zephyr/wifi/ZephyrWifiDriver.h>
#include <setup_payload/OnboardingCodesUtil.h>

namespace chip::app::SpeakerApp {

namespace {

AppTask sAppTask;

} // namespace

AppTask & GetAppTask()
{
    return sAppTask;
}

CHIP_ERROR AppTask::Start()
{
    ReturnErrorOnFailure(Platform::MemoryInit());
    ReturnErrorOnFailure(DeviceLayer::PlatformMgr().InitChipStack());

    // Test credentials. A product replaces both providers with its own.
    Credentials::SetDeviceAttestationCredentialsProvider(Credentials::Examples::GetExampleDACProvider());
    DeviceLayer::SetDeviceInstanceInfoProvider(&DeviceLayer::DeviceInstanceInfoProviderMgrImpl());

    ReturnErrorOnFailure(InitPersistence());
    ReturnErrorOnFailure(InitDataModel());
    ReturnErrorOnFailure(Server::GetInstance().Init(mInitParams));
    ReturnErrorOnFailure(FactoryResetButton::Init());

    DeviceLayer::ConfigurationMgr().LogDeviceConfig();
    PrintOnboardingCodes(RendezvousInformationFlags(RendezvousInformationFlag::kBLE));

    return DeviceLayer::PlatformMgr().StartEventLoopTask();
}

CHIP_ERROR AppTask::InitPersistence()
{
    ReturnErrorOnFailure(mInitParams.InitializeStaticResourcesBeforeServerInit());
    ReturnErrorOnFailure(mAttributePersistenceProvider.Init(mInitParams.persistentStorageDelegate));

    mInitParams.groupDataProvider = &mGroupDataProvider;
    mGroupDataProvider.SetStorageDelegate(mInitParams.persistentStorageDelegate);
    mGroupDataProvider.SetSessionKeystore(mInitParams.sessionKeystore);
    ReturnErrorOnFailure(mGroupDataProvider.Init());
    Credentials::SetGroupDataProvider(&mGroupDataProvider);

    return CHIP_NO_ERROR;
}

CHIP_ERROR AppTask::InitDataModel()
{
    mDataModelProvider.emplace(*mInitParams.persistentStorageDelegate, mAttributePersistenceProvider);
    mInitParams.dataModelProvider        = &mDataModelProvider.value();
    mInitParams.testEventTriggerDelegate = &mTestEventTriggerDelegate;

    DeviceLayer::DeviceInstanceInfoProvider * deviceInstanceInfoProvider = DeviceLayer::GetDeviceInstanceInfoProvider();
    VerifyOrReturnError(deviceInstanceInfoProvider != nullptr, CHIP_ERROR_INCORRECT_STATE);

    mRootNode.emplace(RootNode::Context{
        .commissioningWindowManager = Server::GetInstance().GetCommissioningWindowManager(),
        .configurationManager       = DeviceLayer::ConfigurationMgr(),
        .deviceControlServer        = DeviceLayer::DeviceControlServer::DeviceControlSvr(),
        .fabricTable                = Server::GetInstance().GetFabricTable(),
        .accessControl              = Server::GetInstance().GetAccessControl(),
        .persistentStorage          = *mInitParams.persistentStorageDelegate,
        .failSafeContext            = Server::GetInstance().GetFailSafeContext(),
        .deviceInstanceInfoProvider = *deviceInstanceInfoProvider,
        .platformManager            = DeviceLayer::PlatformMgr(),
        .groupDataProvider          = mGroupDataProvider,
        .sessionManager             = Server::GetInstance().GetSecureSessionManager(),
        .dnssdServer                = DnssdServer::Instance(),
        .deviceLoadStatusProvider   = *InteractionModelEngine::GetInstance(),
        .diagnosticDataProvider     = DeviceLayer::GetDiagnosticDataProvider(),
        .testEventTriggerDelegate   = &mTestEventTriggerDelegate,
        .dacProvider                = *Credentials::GetDeviceAttestationCredentialsProvider(),
        .eventManagement            = EventManagement::GetInstance(),
        .timerDelegate              = mTimerDelegate,
        // The Wi-Fi manager reports to this singleton, so no other driver instance works.
        .wifiDriver                          = DeviceLayer::NetworkCommissioning::ZephyrWifiDriver::Instance(),
        .minGuaranteedSubscriptionsPerFabric = InteractionModelEngine::GetInstance()->GetMinGuaranteedSubscriptionsPerFabric(),
    });
    ReturnErrorOnFailure(mRootNode->Register(kRootNodeEndpointId, *mDataModelProvider));

    mSpeaker.emplace(mTimerDelegate);
    return mSpeaker->Register(kSpeakerEndpointId, *mDataModelProvider);
}

} // namespace chip::app::SpeakerApp
