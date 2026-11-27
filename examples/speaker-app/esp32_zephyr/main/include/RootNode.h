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

#include "DeviceEndpoint.h"

#include <app/DeviceLoadStatusProvider.h>
#include <app/TestEventTriggerDelegate.h>
#include <app/clusters/access-control-server/access-control-cluster.h>
#include <app/clusters/administrator-commissioning-server/AdministratorCommissioningCluster.h>
#include <app/clusters/basic-information/BasicInformationCluster.h>
#include <app/clusters/general-commissioning-server/GeneralCommissioningCluster.h>
#include <app/clusters/general-diagnostics-server/GeneralDiagnosticsCluster.h>
#include <app/clusters/group-key-mgmt-server/GroupKeyManagementCluster.h>
#include <app/clusters/groupcast/GroupcastCluster.h>
#include <app/clusters/network-commissioning/NetworkCommissioningCluster.h>
#include <app/clusters/operational-credentials-server/OperationalCredentialsCluster.h>
#include <app/clusters/software-diagnostics-server/SoftwareDiagnosticsCluster.h>
#include <app/clusters/wifi-network-diagnostics-server/WiFiNetworkDiagnosticsCluster.h>
#include <credentials/GroupDataProvider.h>
#include <lib/support/TimerDelegate.h>
#include <platform/DiagnosticDataProvider.h>
#include <platform/NetworkCommissioning.h>

namespace chip::app::SpeakerApp {

/// Root Node endpoint of a Wi-Fi node: the clusters that every Matter node has,
/// plus Network Commissioning and Wi-Fi Network Diagnostics.
class RootNode : public DeviceEndpoint
{
public:
    struct Context
    {
        CommissioningWindowManager & commissioningWindowManager;
        DeviceLayer::ConfigurationManager & configurationManager;
        DeviceLayer::DeviceControlServer & deviceControlServer;
        FabricTable & fabricTable;
        Access::AccessControl & accessControl;
        PersistentStorageDelegate & persistentStorage;
        FailSafeContext & failSafeContext;
        DeviceLayer::DeviceInstanceInfoProvider & deviceInstanceInfoProvider;
        DeviceLayer::PlatformManager & platformManager;
        Credentials::GroupDataProvider & groupDataProvider;
        SessionManager & sessionManager;
        DnssdServer & dnssdServer;
        DeviceLoadStatusProvider & deviceLoadStatusProvider;
        DeviceLayer::DiagnosticDataProvider & diagnosticDataProvider;
        TestEventTriggerDelegate * testEventTriggerDelegate;
        Credentials::DeviceAttestationCredentialsProvider & dacProvider;
        EventManagement & eventManagement;
        TimerDelegate & timerDelegate;
        DeviceLayer::NetworkCommissioning::WiFiDriver & wifiDriver;
        uint16_t minGuaranteedSubscriptionsPerFabric;
    };

    explicit RootNode(const Context & context);
    ~RootNode() override = default;

    CHIP_ERROR Register(EndpointId endpoint, CodeDrivenDataModelProvider & provider) override;
    void Unregister(CodeDrivenDataModelProvider & provider) override;

private:
    CHIP_ERROR RegisterNodeClusters(EndpointId endpoint, CodeDrivenDataModelProvider & provider);
    CHIP_ERROR RegisterWifiClusters(EndpointId endpoint, CodeDrivenDataModelProvider & provider);

    Context mContext;

    LazyRegisteredServerCluster<Clusters::BasicInformationCluster> mBasicInformationCluster;
    LazyRegisteredServerCluster<Clusters::GeneralCommissioningCluster> mGeneralCommissioningCluster;
    LazyRegisteredServerCluster<Clusters::AdministratorCommissioningWithBasicCommissioningWindowCluster>
        mAdministratorCommissioningCluster;
    LazyRegisteredServerCluster<Clusters::GeneralDiagnosticsCluster> mGeneralDiagnosticsCluster;
    LazyRegisteredServerCluster<Clusters::GroupKeyManagementCluster> mGroupKeyManagementCluster;
#if CHIP_CONFIG_ENABLE_GROUPCAST
    LazyRegisteredServerCluster<Clusters::GroupcastCluster> mGroupcastCluster;
#endif // CHIP_CONFIG_ENABLE_GROUPCAST
    LazyRegisteredServerCluster<Clusters::SoftwareDiagnosticsServerCluster> mSoftwareDiagnosticsServerCluster;
    LazyRegisteredServerCluster<Clusters::AccessControlCluster> mAccessControlCluster;
    LazyRegisteredServerCluster<Clusters::OperationalCredentialsCluster> mOperationalCredentialsCluster;
    LazyRegisteredServerCluster<Clusters::WiFiDiagnosticsServerCluster> mWifiDiagnosticsCluster;
    LazyRegisteredServerCluster<Clusters::NetworkCommissioningCluster> mNetworkCommissioningCluster;
};

} // namespace chip::app::SpeakerApp
