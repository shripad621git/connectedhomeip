/*
 *
 *    Copyright (c) 2020 Project CHIP Authors
 *    Copyright (c) 2016-2017 Nest Labs, Inc.
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

#include <lib/support/CHIPCounter.h>
#include <lib/core/CHIPError.h>
#include <esp_diagnostics_variables.h>
#include <string.h>
#include <esp_err.h>
#include <esp_log.h>

#define PATH "mtr.cntr"
#define space_conc(str1,str2) #str1 " " #str2

namespace chip{
/**
 * @class MonotonicallyIncreasingCounter
 *
 * @brief
 *   A class for managing a monotonically-increasing counter as an integer value.
 */
template <typename T>
class AdvanceCounter : public Counter<T>
{
public:
    AdvanceCounter(const char *label=NULL,const char *group=NULL) : mCounterValue(0)
    {
        if(label)
        {
            mlabel = label;
        }
        if(group)
        {
            mgroup = group;
        }
    }
    ~AdvanceCounter() override{};


    /**
     *  @brief
     *    Initialize a MonotonicallyIncreasingCounter object.
     *
     *  @param[in] aStartValue  The starting value of the counter.
     *
     *  @return A CHIP error code if something fails, CHIP_NO_ERROR otherwise
     */
    CHIP_ERROR Init(T aStartValue)
    {
        CHIP_ERROR err = CHIP_NO_ERROR;

        mCounterValue = aStartValue;

        return err;
    }

    /**
     *  @brief
     *  Advance the value of the counter.
     *
     *  @return A CHIP error code if something fails, CHIP_NO_ERROR otherwise
     */
    CHIP_ERROR Advance() override
    {
        CHIP_ERROR err = CHIP_NO_ERROR;

        mCounterValue++;
        if(!isRegistered())
        {
          if(mlabel&&mgroup)
          {

            esp_err_t  err1 = esp_diag_variable_register(mgroup,mlabel,mlabel,PATH,ESP_DIAG_DATA_TYPE_INT);
            if(err1 == ESP_OK)
            {
                printf("Success");
                registered = true;
            }
            else
            {
              ESP_LOGE("CNT", "Registration failed, err: %d",err1);
              printf("Fail");
            }
            esp_diag_variable_add_int(mlabel,(uint32_t)mCounterValue);
          }

        }

        printf("registerd %d",registered);
        if(mlabel)
        {
          esp_diag_variable_add_int(mlabel,(uint32_t)mCounterValue);
        }

        return err;
    }

    /**
     *  @brief
     *  Get the current value of the counter.
     *
     *  @return The current value of the counter.
     */
    T GetValue() override { return mCounterValue; }

protected:
    T mCounterValue;
    const char* mlabel;
    const char* mgroup;
    bool registered =false;
    bool isRegistered()
    {
      return registered;
    }
};
}
 // namespace chip
