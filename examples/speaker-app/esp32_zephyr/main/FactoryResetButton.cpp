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

#include "FactoryResetButton.h"

#include <app/server/Server.h>
#include <lib/support/CodeUtils.h>
#include <lib/support/logging/CHIPLogging.h>

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>

namespace chip::app::SpeakerApp::FactoryResetButton {

#if DT_NODE_EXISTS(DT_ALIAS(sw0))

namespace {

constexpr uint16_t kHoldMs = 5000;

const struct gpio_dt_spec sButton = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);
struct gpio_callback sButtonCallback;
int64_t sPressedAtMs = -1;

void PressedWorkHandler(struct k_work *)
{
    ChipLogProgress(DeviceLayer, "Factory reset button pressed; release after holding for %dms to reset.", kHoldMs);
}
K_WORK_DEFINE(sPressedWork, PressedWorkHandler);

void FactoryResetWorkHandler(struct k_work *)
{
    ChipLogProgress(DeviceLayer, "Factory reset button held; resetting");
    Server::GetInstance().ScheduleFactoryReset();
}
K_WORK_DEFINE(sFactoryResetWork, FactoryResetWorkHandler);

void OnButtonEdge(const struct device *, struct gpio_callback *, gpio_port_pins_t)
{
    const int pin = gpio_pin_get_dt(&sButton);
    if (pin < 0)
    {
        ChipLogError(DeviceLayer, "Factory reset button read failed: %d", pin);
        sPressedAtMs = -1;
        return;
    }

    if (pin > 0)
    {
        sPressedAtMs = k_uptime_get();
        k_work_submit(&sPressedWork);
    }
    else if (sPressedAtMs >= 0 && (k_uptime_get() - sPressedAtMs) >= kHoldMs)
    {
        sPressedAtMs = -1;
        k_work_submit(&sFactoryResetWork);
    }
    else
    {
        sPressedAtMs = -1;
    }
}

} // namespace

CHIP_ERROR Init()
{
    VerifyOrReturnError(gpio_is_ready_dt(&sButton), CHIP_ERROR_INTERNAL, ChipLogError(DeviceLayer, "Button GPIO not ready"));
    VerifyOrReturnError(gpio_pin_configure_dt(&sButton, GPIO_INPUT) == 0, CHIP_ERROR_INTERNAL,
                        ChipLogError(DeviceLayer, "Button GPIO configure failed"));
    VerifyOrReturnError(gpio_pin_interrupt_configure_dt(&sButton, GPIO_INT_EDGE_BOTH) == 0, CHIP_ERROR_INTERNAL,
                        ChipLogError(DeviceLayer, "Button GPIO interrupt configure failed"));

    gpio_init_callback(&sButtonCallback, OnButtonEdge, BIT(sButton.pin));
    VerifyOrReturnError(gpio_add_callback(sButton.port, &sButtonCallback) == 0, CHIP_ERROR_INTERNAL,
                        ChipLogError(DeviceLayer, "Button GPIO add callback failed"));

    return CHIP_NO_ERROR;
}

#else // !DT_NODE_EXISTS(DT_ALIAS(sw0))

CHIP_ERROR Init()
{
    return CHIP_NO_ERROR;
}

#endif // DT_NODE_EXISTS(DT_ALIAS(sw0))

} // namespace chip::app::SpeakerApp::FactoryResetButton
