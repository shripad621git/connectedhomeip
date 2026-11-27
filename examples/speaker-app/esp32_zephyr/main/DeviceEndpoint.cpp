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

#include "DeviceEndpoint.h"

#include <lib/support/CodeUtils.h>

namespace chip::app::SpeakerApp {

CHIP_ERROR DeviceEndpoint::DeviceTypes(ReadOnlyBufferBuilder<DataModel::DeviceTypeEntry> & out) const
{
    VerifyOrReturnValue(mDescriptorCluster.IsConstructed(), CHIP_NO_ERROR);
    return out.ReferenceExisting(mDeviceTypes);
}

CHIP_ERROR DeviceEndpoint::ClientClusters(ReadOnlyBufferBuilder<ClusterId> & out) const
{
    return CHIP_NO_ERROR;
}

CHIP_ERROR DeviceEndpoint::RegisterDescriptor(EndpointId endpoint, CodeDrivenDataModelProvider & provider,
                                      DataModel::EndpointCompositionPattern pattern)
{
    VerifyOrReturnError(endpoint != kInvalidEndpointId, CHIP_ERROR_INVALID_ARGUMENT);
    VerifyOrReturnError(mEndpointId == kInvalidEndpointId, CHIP_ERROR_INCORRECT_STATE);

    mDescriptorCluster.Create(endpoint, Clusters::DescriptorCluster::OptionalAttributesSet(0),
                              Span<const Clusters::DescriptorCluster::SemanticTag>());
    CHIP_ERROR err = provider.AddCluster(mDescriptorCluster.Registration());
    if (err != CHIP_NO_ERROR)
    {
        mDescriptorCluster.Destroy();
        return err;
    }

    mEndpointId                         = endpoint;
    mEndpointRegistration.endpointEntry = DataModel::EndpointEntry{
        .id                 = endpoint,
        .parentId           = kInvalidEndpointId,
        .compositionPattern = pattern,
    };
    return CHIP_NO_ERROR;
}

void DeviceEndpoint::UnregisterDescriptor(CodeDrivenDataModelProvider & provider)
{
    VerifyOrReturn(mEndpointId != kInvalidEndpointId);

    LogErrorOnFailure(provider.RemoveEndpoint(mEndpointId, ClusterShutdownType::kClusterShutdown));
    if (mDescriptorCluster.IsConstructed())
    {
        LogErrorOnFailure(provider.RemoveCluster(&mDescriptorCluster.Cluster()));
        mDescriptorCluster.Destroy();
    }
    mEndpointId = kInvalidEndpointId;
}

} // namespace chip::app::SpeakerApp
