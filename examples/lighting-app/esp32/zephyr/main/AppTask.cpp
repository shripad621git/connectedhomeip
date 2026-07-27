/*
 *
 *    Copyright (c) 2024 Project CHIP Authors
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
#include "AppConfig.h"

#include <app/server/Server.h>
#include <app/util/attribute-storage.h>
#include <credentials/DeviceAttestationCredsProvider.h>
#include <credentials/examples/DeviceAttestationCredsExample.h>
#include <data-model-providers/codegen/Instance.h>
#include <lib/support/CHIPMem.h>
#include <platform/CHIPDeviceLayer.h>
#include <setup_payload/OnboardingCodesUtil.h>

#if CHIP_DEVICE_CONFIG_ENABLE_WIFI
#include <app/clusters/network-commissioning/network-commissioning.h>
#include <platform/Zephyr/wifi/ZephyrWifiDriver.h>
#endif

#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(app, LOG_LEVEL_INF);

using namespace ::chip;
using namespace ::chip::DeviceLayer;

namespace {

// Physical light. Resolved at build time from the `led0` alias in the board
// overlay; GPIO writes go straight to the ESP32 GPIO register (no RAM lookup).
const struct gpio_dt_spec sLightLed = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);

#if CHIP_DEVICE_CONFIG_ENABLE_WIFI
// The generic Zephyr WiFi driver drives the ESP32-native driver purely through
// net_mgmt (NET_REQUEST_WIFI_CONNECT / NET_EVENT_WIFI_*). It compiles WITHOUT
// the Zephyr WPA supplicant because every supplicant reference in WiFiManager is
// behind #ifdef CONFIG_WIFI_NM_WPA_SUPPLICANT — which we keep OFF (the ESP32
// supplicant lives in the hal_espressif blob). No custom driver is needed.
app::Clusters::NetworkCommissioning::Instance
    sWiFiCommissioningInstance(0 /* endpoint */, &(NetworkCommissioning::ZephyrWifiDriver::Instance()));
#endif

} // namespace

void AppTask::SetOnOffLED(bool on)
{
    if (!gpio_is_ready_dt(&sLightLed))
    {
        return;
    }
    gpio_pin_set_dt(&sLightLed, on ? 1 : 0);
}

CHIP_ERROR AppTask::Init()
{
    LOG_INF("Bringing up Matter stack for ESP32-C6 (Zephyr)");

    // 1. CHIP memory allocator (routed to the Zephyr sys-heap by Kconfig).
    ReturnErrorOnFailure(Platform::MemoryInit());

    // 2. Platform layer: entropy, ZephyrConfig (settings/NVS), the CHIP event
    //    queue and lock. Does NOT start the event-loop thread yet.
    ReturnErrorOnFailure(PlatformMgr().InitChipStack());

    // 3. Network commissioning driver. On this build the transport is WiFi; the
    //    generic ZephyrWifiDriver + WiFiManager talk to the ESP32 driver over
    //    net_mgmt. (Thread is intentionally disabled — see prj.conf.)
#if CHIP_DEVICE_CONFIG_ENABLE_WIFI
    ReturnErrorOnFailure(sWiFiCommissioningInstance.Init());
#endif

    // 4. Physical light output.
    if (gpio_is_ready_dt(&sLightLed))
    {
        gpio_pin_configure_dt(&sLightLed, GPIO_OUTPUT_INACTIVE);
    }
    else
    {
        LOG_WRN("led0 not ready — light state will not be shown on a GPIO");
    }

    // 5. Attestation credentials. This build uses the built-in EXAMPLE (test)
    //    DAC/PAI/CD — no factory-data partition. Not for production.
    SetDeviceAttestationCredentialsProvider(Credentials::Examples::GetExampleDACProvider());

    // 6. Bring up the Matter server: fabric table, PASE/CASE, minimal-mDNS,
    //    Interaction Model. SPAKE2+ for PASE is CHIP's portable implementation
    //    over mbedTLS P-256 (chip_crypto="mbedtls") — the PSA SPAKE2+ gap in
    //    tf-psa-crypto is never on this path.
    static CommonCaseDeviceServerInitParams initParams;
    ReturnErrorOnFailure(initParams.InitializeStaticResourcesBeforeServerInit());
    initParams.dataModelProvider = app::CodegenDataModelProviderInstance(initParams.persistentStorageDelegate);
    ReturnErrorOnFailure(Server::GetInstance().Init(initParams));

    // 7. Print the QR / manual pairing code. The rendezvous flag must match the
    //    radios actually compiled in: BLE (CHIPoBLE) when CONFIG_BT is set,
    //    otherwise on-network only (this build disables BLE — see prj.conf).
    ConfigurationMgr().LogDeviceConfig();
#if CHIP_DEVICE_CONFIG_ENABLE_CHIPOBLE
    PrintOnboardingCodes(RendezvousInformationFlags(RendezvousInformationFlag::kBLE));
#else
    PrintOnboardingCodes(RendezvousInformationFlags(RendezvousInformationFlag::kOnNetwork));
#endif

    // 8. Start the dedicated CHIP event-loop thread. All init above must be done
    //    before this to avoid races between the main and CHIP threads.
    ReturnErrorOnFailure(PlatformMgr().StartEventLoopTask());

    LOG_INF("Matter stack initialized");
    return CHIP_NO_ERROR;
}

CHIP_ERROR AppTask::StartApp()
{
    ReturnErrorOnFailure(Init());

    // No app-level UI queue in this minimal build (no buttons/status LED, to keep
    // the RAM footprint down). The CHIP event loop runs on its own thread; park
    // the main thread. Add a k_msgq-driven event loop here to wire buttons later.
    while (true)
    {
        k_sleep(K_FOREVER);
    }

    return CHIP_NO_ERROR;
}
