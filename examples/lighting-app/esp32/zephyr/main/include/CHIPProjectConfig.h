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

/**
 *    @file
 *      Project-specific CHIP configuration for the ESP32-C6 Zephyr lighting app.
 *
 *      This build uses the *example* Device Attestation Credentials and the
 *      standard test setup passcode (20202021) / discriminator (3840), so it
 *      commissions against chip-tool out of the box with NO factory data.
 *      Do NOT ship this configuration on a production device.
 */

#pragma once

// Use the example (test) DAC/PAI/CD provider. No factory-data partition needed.
#define CHIP_DEVICE_CONFIG_ENABLE_EXAMPLE_CREDENTIALS 1

// Test onboarding payload (matches chip-tool defaults).
#define CHIP_DEVICE_CONFIG_USE_TEST_SETUP_PIN_CODE 20202021
#define CHIP_DEVICE_CONFIG_USE_TEST_SETUP_DISCRIMINATOR 0x0F00

// Reported software version.
#define CHIP_DEVICE_CONFIG_DEVICE_SOFTWARE_VERSION 1
#define CHIP_DEVICE_CONFIG_DEVICE_SOFTWARE_VERSION_STRING "1.0"

// Keep the in-RAM event queue small — this device has a single light endpoint
// and no high-rate event producers. (RAM is the binding budget on the C6.)
#define CHIP_DEVICE_CONFIG_MAX_EVENT_QUEUE_SIZE 16

// CHIP packet-buffer pool. Default is 15 buffers (~24 KB static on the C6). A
// single On/Off light needs far fewer; 8 is comfortable and frees ~11 KB SRAM.
#define CHIP_SYSTEM_CONFIG_PACKETBUFFER_POOL_SIZE 8
