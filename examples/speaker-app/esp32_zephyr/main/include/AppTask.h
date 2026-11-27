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

#include "RootNode.h"
#include "Speaker.h"

#include <app/TestEventTriggerDelegate.h>
#include <app/persistence/DefaultAttributePersistenceProvider.h>
#include <app/server/Server.h>
#include <credentials/GroupDataProviderImpl.h>
#include <data-model-providers/codedriven/CodeDrivenDataModelProvider.h>
#include <lib/core/CHIPError.h>
#include <platform/DefaultTimerDelegate.h>

#include <optional>

namespace chip::app::SpeakerApp {

/// Starts the Matter stack with a data model of two endpoints: the Root Node on
/// endpoint 0 and the Speaker on endpoint 1.
class AppTask
{
public:
    /// Initializes the stack and the data model, then starts the Matter event loop.
    /// Returns the first error.
    CHIP_ERROR Start();

private:
    static constexpr EndpointId kRootNodeEndpointId = 0;
    static constexpr EndpointId kSpeakerEndpointId  = 1;

    CHIP_ERROR InitPersistence();
    CHIP_ERROR InitDataModel();

    CommonCaseDeviceServerInitParams mInitParams;
    Credentials::GroupDataProviderImpl mGroupDataProvider;
    SimpleTestEventTriggerDelegate mTestEventTriggerDelegate;
    DefaultTimerDelegate mTimerDelegate;
    DefaultAttributePersistenceProvider mAttributePersistenceProvider;

    // Created in InitDataModel(), because their constructors use objects that
    // exist only after InitPersistence().
    std::optional<CodeDrivenDataModelProvider> mDataModelProvider;
    std::optional<RootNode> mRootNode;
    std::optional<Speaker> mSpeaker;
};

AppTask & GetAppTask();

} // namespace chip::app::SpeakerApp
