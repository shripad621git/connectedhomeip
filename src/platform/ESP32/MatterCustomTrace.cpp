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


#include "MatterCustomTrace.h"
#include <esp_insights.h>
#include "esp_heap_caps.h"
#include <system/SystemClock.h>

namespace myspace {

ESP32Backend::ESP32Backend(const char * str, ...)
{
    va_list args;
    va_start(args, str);
    var1 = str;
    var2= va_arg(args,const char *);

    if (var2 != NULL)
    {
        ESP_DIAG_EVENT("MTR_TRC", "Entry - %s - %s", var1, var2);
    }
    else
    {
        ESP_DIAG_EVENT("MTR_TRC", "Entry - %s", var1);
    }

    chip::System::Clock::Timestamp start = chip::System::SystemClock().GetMonotonicTimestamp();
    start_time = chip::System::Clock::Milliseconds32(start).count();
    int start_min_heap =heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT);
    int largest_free_block = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    int start_free_heap = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    ESP_DIAG_EVENT("MTR_TRC","Start time - %5d  Start Min Free heap - %5d -  Largest Free Block - %5d  Start free heap -%5d ", start_time, start_min_heap, largest_free_block, start_free_heap);

}

ESP32Backend::~ESP32Backend()
{
    if(var2!=NULL)
    {
        ESP_DIAG_EVENT("MTR_TRC", "Exit - %s - %s", var1, var2);
    }
    else
    {
        ESP_DIAG_EVENT("MTR_TRC", "Exit - %s", var1);
    }

    chip::System::Clock::Timestamp end = chip::System::SystemClock().GetMonotonicTimestamp();
    long unsigned int end_time = chip::System::Clock::Milliseconds32(end).count();
    long unsigned int time_elapsed = end_time -start_time;
    int end_minimum_free_heap = heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT);
    int end_largest_free_block = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    int end_free_heap = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    ESP_DIAG_EVENT("MTR_TRC","Time Elapsed - %5lu - End Min Free heap - %5d - End Largest Free Block %5d - End free Heap - %5d",time_elapsed, end_minimum_free_heap, end_largest_free_block, end_free_heap);
}
}//myspace
