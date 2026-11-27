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

#include <app/clusters/descriptor/DescriptorCluster.h>
#include <data-model-providers/codedriven/CodeDrivenDataModelProvider.h>
#include <data-model-providers/codedriven/endpoint/EndpointInterfaceRegistry.h>
#include <lib/core/DataModelTypes.h>
#include <lib/support/Span.h>

namespace chip::app::SpeakerApp {

/// One endpoint of the node: its device types, its Descriptor cluster and the
/// clusters that the subclass adds.
class DeviceEndpoint : public EndpointInterface
{
public:
    ~DeviceEndpoint() override = default;

    /// Creates the clusters of the device and adds them and the endpoint to `provider`.
    /// On failure nothing stays registered.
    virtual CHIP_ERROR Register(EndpointId endpoint, CodeDrivenDataModelProvider & provider) = 0;

    /// Removes the endpoint and its clusters from `provider` and destroys the clusters.
    virtual void Unregister(CodeDrivenDataModelProvider & provider) = 0;

    // EndpointInterface
    CHIP_ERROR DeviceTypes(ReadOnlyBufferBuilder<DataModel::DeviceTypeEntry> & out) const override;
    CHIP_ERROR ClientClusters(ReadOnlyBufferBuilder<ClusterId> & out) const override;

protected:
    /// `deviceTypes` must stay valid for the lifetime of the device.
    explicit DeviceEndpoint(Span<const DataModel::DeviceTypeEntry> deviceTypes) : mDeviceTypes(deviceTypes), mEndpointRegistration(*this, {})
    {}

    /// Adds the Descriptor cluster and fills in the endpoint entry. The subclass adds its
    /// own clusters next and then calls `provider.AddEndpoint(mEndpointRegistration)`.
    CHIP_ERROR RegisterDescriptor(EndpointId endpoint, CodeDrivenDataModelProvider & provider,
                                  DataModel::EndpointCompositionPattern pattern);

    /// Removes the endpoint and the Descriptor cluster. A started provider refuses to
    /// remove a cluster of a registered endpoint, so `Unregister` calls this first.
    void UnregisterDescriptor(CodeDrivenDataModelProvider & provider);

    Span<const DataModel::DeviceTypeEntry> mDeviceTypes;
    EndpointInterfaceRegistration mEndpointRegistration;
    EndpointId mEndpointId = kInvalidEndpointId;

private:
    LazyRegisteredServerCluster<Clusters::DescriptorCluster> mDescriptorCluster;
};

/// Calls `DeviceEndpoint::Unregister` when a `Register` call returns before `Commit()`.
class DeviceRegistrationTransaction
{
public:
    DeviceRegistrationTransaction(DeviceEndpoint & device, CodeDrivenDataModelProvider & provider) : mDevice(device), mProvider(provider) {}

    ~DeviceRegistrationTransaction()
    {
        if (!mCommitted)
        {
            mDevice.Unregister(mProvider);
        }
    }

    void Commit() { mCommitted = true; }

private:
    DeviceEndpoint & mDevice;
    CodeDrivenDataModelProvider & mProvider;
    bool mCommitted = false;
};

} // namespace chip::app::SpeakerApp
