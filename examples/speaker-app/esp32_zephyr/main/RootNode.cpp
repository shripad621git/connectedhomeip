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

#include "RootNode.h"

#include <app-common/zap-generated/ids/Attributes.h>
#include <app/clusters/groupcast/GroupcastContext.h>
#include <devices/Types.h>
#include <lib/support/CodeUtils.h>
#include <platform/CHIPDeviceLayer.h>

using namespace chip::app::Clusters;

namespace chip::app::SpeakerApp {

namespace {

constexpr DataModel::DeviceTypeEntry kDeviceTypes[] = { Device::Type::kRootNode };

} // namespace

RootNode::RootNode(const Context & context) :
    DeviceEndpoint(Span<const DataModel::DeviceTypeEntry>(kDeviceTypes)), mContext(context)
{}

CHIP_ERROR RootNode::Register(EndpointId endpoint, CodeDrivenDataModelProvider & provider)
{
    DeviceRegistrationTransaction transaction(*this, provider);

    ReturnErrorOnFailure(RegisterDescriptor(endpoint, provider, DataModel::EndpointCompositionPattern::kFullFamily));
    ReturnErrorOnFailure(RegisterNodeClusters(endpoint, provider));
    ReturnErrorOnFailure(RegisterWifiClusters(endpoint, provider));
    ReturnErrorOnFailure(provider.AddEndpoint(mEndpointRegistration));

    transaction.Commit();
    return CHIP_NO_ERROR;
}

CHIP_ERROR RootNode::RegisterNodeClusters(EndpointId endpoint, CodeDrivenDataModelProvider & provider)
{
    const BasicInformationOptionalAttributesSet optionalAttributeSet =
        BasicInformationOptionalAttributesSet()
            .template Set<BasicInformation::Attributes::ManufacturingDate::Id>()
            .template Set<BasicInformation::Attributes::PartNumber::Id>()
            .template Set<BasicInformation::Attributes::ProductURL::Id>()
            .template Set<BasicInformation::Attributes::ProductLabel::Id>()
            .template Set<BasicInformation::Attributes::SerialNumber::Id>()
            .template Set<BasicInformation::Attributes::LocalConfigDisabled::Id>()
            .template Set<BasicInformation::Attributes::Reachable::Id>();

    mBasicInformationCluster.Create(optionalAttributeSet, mContext.deviceInstanceInfoProvider, mContext.configurationManager,
                                    mContext.platformManager, mContext.minGuaranteedSubscriptionsPerFabric);
    ReturnErrorOnFailure(provider.AddCluster(mBasicInformationCluster.Registration()));

    mGeneralCommissioningCluster.Create(
        GeneralCommissioningCluster::Context{
            .commissioningWindowManager = mContext.commissioningWindowManager,
            .configurationManager       = mContext.configurationManager,
            .deviceControlServer        = mContext.deviceControlServer,
            .fabricTable                = mContext.fabricTable,
            .failSafeContext            = mContext.failSafeContext,
            .platformManager            = mContext.platformManager,
        },
        GeneralCommissioningCluster::OptionalAttributes());
    ReturnErrorOnFailure(provider.AddCluster(mGeneralCommissioningCluster.Registration()));

    mAdministratorCommissioningCluster.Create(
        endpoint, BitFlags<AdministratorCommissioning::Feature>{},
        AdministratorCommissioningCluster::Context{ .commissioningWindowManager = mContext.commissioningWindowManager,
                                                    .fabricTable                = mContext.fabricTable,
                                                    .failSafeContext            = mContext.failSafeContext });
    ReturnErrorOnFailure(provider.AddCluster(mAdministratorCommissioningCluster.Registration()));

    mGeneralDiagnosticsCluster.Create(GeneralDiagnosticsCluster::OptionalAttributeSet{}, BitFlags<GeneralDiagnostics::Feature>{},
                                      GeneralDiagnosticsCluster::Context{
                                          .deviceLoadStatusProvider = mContext.deviceLoadStatusProvider,
                                          .diagnosticDataProvider   = mContext.diagnosticDataProvider,
                                          .testEventTriggerDelegate = mContext.testEventTriggerDelegate,
                                      });
    ReturnErrorOnFailure(provider.AddCluster(mGeneralDiagnosticsCluster.Registration()));

    mGroupKeyManagementCluster.Create(GroupKeyManagementCluster::Context{
        .fabricTable       = mContext.fabricTable,
        .groupDataProvider = mContext.groupDataProvider,
    });
    ReturnErrorOnFailure(provider.AddCluster(mGroupKeyManagementCluster.Registration()));

#if CHIP_CONFIG_ENABLE_GROUPCAST
    if (mContext.groupDataProvider.IsGroupcastEnabled())
    {
        mGroupcastCluster.Create(GroupcastContext{ .fabricTable       = mContext.fabricTable,
                                                   .groupDataProvider = mContext.groupDataProvider,
                                                   .timerDelegate     = mContext.timerDelegate,
                                                   .accessControl     = mContext.accessControl,
                                                   .testing           = Groupcast::GetTesting() },
                                 BitFlags<Clusters::Groupcast::Feature>(Clusters::Groupcast::Feature::kListener,
                                                                        Clusters::Groupcast::Feature::kSender,
                                                                        Clusters::Groupcast::Feature::kPerGroup));
        ReturnErrorOnFailure(provider.AddCluster(mGroupcastCluster.Registration()));
    }
#endif // CHIP_CONFIG_ENABLE_GROUPCAST

    mSoftwareDiagnosticsServerCluster.Create(SoftwareDiagnosticsServerCluster::OptionalAttributeSet{},
                                             mContext.diagnosticDataProvider);
    ReturnErrorOnFailure(provider.AddCluster(mSoftwareDiagnosticsServerCluster.Registration()));

    mAccessControlCluster.Create(AccessControlCluster::Context{
        .persistentStorage = mContext.persistentStorage,
        .fabricTable       = mContext.fabricTable,
        .accessControl     = mContext.accessControl,
    });
    ReturnErrorOnFailure(provider.AddCluster(mAccessControlCluster.Registration()));

    BitFlags<OperationalCredentials::Feature> operationalCredentialsFeatures;
    operationalCredentialsFeatures.Set(OperationalCredentials::Feature::kPQCDeviceAttestation,
                                       mContext.dacProvider.HasRequiredPqcCredentials());
    mOperationalCredentialsCluster.Create(endpoint,
                                          OperationalCredentialsCluster::Context{
                                              .fabricTable                = mContext.fabricTable,
                                              .failSafeContext            = mContext.failSafeContext,
                                              .sessionManager             = mContext.sessionManager,
                                              .dnssdServer                = mContext.dnssdServer,
                                              .commissioningWindowManager = mContext.commissioningWindowManager,
                                              .dacProvider                = mContext.dacProvider,
                                              .groupDataProvider          = mContext.groupDataProvider,
                                              .accessControl              = mContext.accessControl,
                                              .platformManager            = mContext.platformManager,
                                              .eventManagement            = mContext.eventManagement,
                                              .featureMap                 = operationalCredentialsFeatures,
                                          });
    return provider.AddCluster(mOperationalCredentialsCluster.Registration());
}

CHIP_ERROR RootNode::RegisterWifiClusters(EndpointId endpoint, CodeDrivenDataModelProvider & provider)
{
    mWifiDiagnosticsCluster.Create(endpoint, mContext.diagnosticDataProvider, WiFiDiagnosticsServerCluster::OptionalAttributeSet{},
                                   BitFlags<WiFiNetworkDiagnostics::Feature>{});
    ReturnErrorOnFailure(provider.AddCluster(mWifiDiagnosticsCluster.Registration()));

    mNetworkCommissioningCluster.Create(endpoint, &mContext.wifiDriver,
                                        NetworkCommissioningCluster::Context{
                                            .breadcrumbTracker   = mGeneralCommissioningCluster.Cluster(),
                                            .failSafeContext     = mContext.failSafeContext,
                                            .platformManager     = mContext.platformManager,
                                            .deviceControlServer = mContext.deviceControlServer,
                                        });
    ReturnErrorOnFailure(mNetworkCommissioningCluster.Cluster().Init());
    return provider.AddCluster(mNetworkCommissioningCluster.Registration());
}

void RootNode::Unregister(CodeDrivenDataModelProvider & provider)
{
    UnregisterDescriptor(provider);

    // Reverse order of registration: a later cluster can use an earlier one.
    if (mNetworkCommissioningCluster.IsConstructed())
    {
        LogErrorOnFailure(provider.RemoveCluster(&mNetworkCommissioningCluster.Cluster()));
        mNetworkCommissioningCluster.Destroy();
    }
    if (mWifiDiagnosticsCluster.IsConstructed())
    {
        LogErrorOnFailure(provider.RemoveCluster(&mWifiDiagnosticsCluster.Cluster()));
        mWifiDiagnosticsCluster.Destroy();
    }
    if (mOperationalCredentialsCluster.IsConstructed())
    {
        LogErrorOnFailure(provider.RemoveCluster(&mOperationalCredentialsCluster.Cluster()));
        mOperationalCredentialsCluster.Destroy();
    }
    if (mAccessControlCluster.IsConstructed())
    {
        LogErrorOnFailure(provider.RemoveCluster(&mAccessControlCluster.Cluster()));
        mAccessControlCluster.Destroy();
    }
    if (mSoftwareDiagnosticsServerCluster.IsConstructed())
    {
        LogErrorOnFailure(provider.RemoveCluster(&mSoftwareDiagnosticsServerCluster.Cluster()));
        mSoftwareDiagnosticsServerCluster.Destroy();
    }
#if CHIP_CONFIG_ENABLE_GROUPCAST
    if (mGroupcastCluster.IsConstructed())
    {
        LogErrorOnFailure(provider.RemoveCluster(&mGroupcastCluster.Cluster()));
        mGroupcastCluster.Destroy();
    }
#endif // CHIP_CONFIG_ENABLE_GROUPCAST
    if (mGroupKeyManagementCluster.IsConstructed())
    {
        LogErrorOnFailure(provider.RemoveCluster(&mGroupKeyManagementCluster.Cluster()));
        mGroupKeyManagementCluster.Destroy();
    }
    if (mGeneralDiagnosticsCluster.IsConstructed())
    {
        LogErrorOnFailure(provider.RemoveCluster(&mGeneralDiagnosticsCluster.Cluster()));
        mGeneralDiagnosticsCluster.Destroy();
    }
    if (mAdministratorCommissioningCluster.IsConstructed())
    {
        LogErrorOnFailure(provider.RemoveCluster(&mAdministratorCommissioningCluster.Cluster()));
        mAdministratorCommissioningCluster.Destroy();
    }
    if (mGeneralCommissioningCluster.IsConstructed())
    {
        LogErrorOnFailure(provider.RemoveCluster(&mGeneralCommissioningCluster.Cluster()));
        mGeneralCommissioningCluster.Destroy();
    }
    if (mBasicInformationCluster.IsConstructed())
    {
        LogErrorOnFailure(provider.RemoveCluster(&mBasicInformationCluster.Cluster()));
        mBasicInformationCluster.Destroy();
    }
}

} // namespace chip::app::SpeakerApp
