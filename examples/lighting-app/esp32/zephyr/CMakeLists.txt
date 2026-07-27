#
#    Copyright (c) 2024 Project CHIP Authors
#
#    Licensed under the Apache License, Version 2.0 (the "License");
#    you may not use this file except in compliance with the License.
#    You may obtain a copy of the License at
#
#        http://www.apache.org/licenses/LICENSE-2.0
#
#    Unless required by applicable law or agreed to in writing, software
#    distributed under the License is distributed on an "AS IS" BASIS,
#    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#    See the License for the specific language governing permissions and
#    limitations under the License.
#

cmake_minimum_required(VERSION 3.20.0)

# This example lives in-tree, four levels below the connectedhomeip root
# (examples/lighting-app/esp32/zephyr). Resolve CHIP_ROOT directly by relative
# path — no third_party/connectedhomeip symlink required.
get_filename_component(CHIP_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../../.. REALPATH)
get_filename_component(GEN_DIR ${CHIP_ROOT}/zzz_generated/ REALPATH)
get_filename_component(LIGHTING_COMMON ${CHIP_ROOT}/examples/lighting-app/lighting-common REALPATH)
# Local ZAP: copy of lighting-common with the User Label cluster removed. That
# cluster requires a DeviceInfoProvider (VerifyOrDie in its init) which this
# minimal example does not register; dropping it avoids the dependency.
get_filename_component(LOCAL_ZAP ${CMAKE_CURRENT_SOURCE_DIR}/zap/lighting-app.zap REALPATH)

# Build CHIP as a Zephyr module. This reuses the GENERIC config/zephyr/chip-module
# (the same one NXP RW612 uses) — a GN-meta-build wrapped in a CMake ExternalProject.
# No ESP32-specific platform layer is required: chip_device_platform stays "zephyr".
list(APPEND ZEPHYR_EXTRA_MODULES ${CHIP_ROOT}/config/zephyr/chip-module)

find_package(Zephyr HINTS $ENV{ZEPHYR_BASE})

project(chip-esp32c6-zephyr-lighting-app)

include(${CHIP_ROOT}/config/zephyr/app/enable-gnu-std.cmake)
include(${CHIP_ROOT}/src/app/chip_data_model.cmake)

target_include_directories(app PRIVATE
    main/include
    ${GEN_DIR}/app-common
    ${LIGHTING_COMMON}/include
)

target_sources(app PRIVATE
    main/main.cpp
    main/AppTask.cpp
    main/ZclCallbacks.cpp
)

# Generate the cluster/data-model code from the LOCAL lighting-app ZAP
# (User Label removed — see above).
chip_configure_data_model(app
    ZAP_FILE ${LOCAL_ZAP}
)
