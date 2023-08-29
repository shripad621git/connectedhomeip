/*
 *    Copyright (c) 2023 Project CHIP Authors
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

/* Ensure we do not have double tracing macros defined */
#if defined(MATTER_TRACE_BEGIN)
#error "Tracing macros seem to be double defined"
#endif

#include "counter.h"
namespace Insights {

static unsigned int hash(const char *str) {
        unsigned int hash = 5381;
        int c;
        while ((c = *str++)) {
            hash = ((hash << 5) + hash) + c;
        }
        return hash;
}

class ESP32Backend
{
public:
    ESP32Backend(const char * str, ...);
    ~ESP32Backend();
    static constexpr int BLACKLIST_SIZE = 5;
    static unsigned int blacklistHashes[BLACKLIST_SIZE];

    bool isBlacklisted(const char *input) {
       unsigned int inputHash = hash(input);

       for (int i = 0; i < BLACKLIST_SIZE; i++) {
          if (inputHash == blacklistHashes[i]) {
               return true;
           }
        }
        return false;
    }

private:
    const char * mlabel;
    const char * mgroup;
};

} // namespace Insights

#define MATTER_TRACE_SCOPE(...)                                                                                                    \
    do                                                                                                                             \
    {                                                                                                                              \
        Insights::ESP32Backend backend(__VA_ARGS__);                                                                               \
    } while (0)

#define _MATTER_TRACE_DISABLE(...)                                                                                                 \
    do                                                                                                                             \
    {                                                                                                                              \
    } while (false)

#define MATTER_TRACE_BEGIN(...) _MATTER_TRACE_DISABLE(__VA_ARGS__)
#define MATTER_TRACE_END(...) _MATTER_TRACE_DISABLE(__VA_ARGS__)
#define MATTER_TRACE_INSTANT(label, group) Insights::InstantObject::getInstance(label, group)->traceInstant()
