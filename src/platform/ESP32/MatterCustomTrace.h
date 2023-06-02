/*
 *    Copyright (c) 2023 Project CHIP Authors
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

#include <stdint.h>

namespace myspace {
class ESP32Backend {
public:
    ESP32Backend(const char* str, ...);
    ~ESP32Backend();

private:
    long unsigned int start_time;
    const char* var1;
    const char* var2;
};
}

#define MATTER_TRACE_EVENT_SCOPE(...) \
    do { \
        myspace::ESP32Backend backend(__VA_ARGS__); \
    } while (0)


