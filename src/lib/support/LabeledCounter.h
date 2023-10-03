#include <lib/support/CHIPCounter.h>

#if CONFIG_ENABLE_ESP_INSIGHTS_COUNTERS
#include <matter/tracing/ESP32Counter.h>
#else

namespace chip {

template <typename T>
class AdvanceCounter : public Counter<T>
{
public:
      AdvanceCounter(const char *label=NULL,const char *group=NULL) : mCounterValue(0) {}
      ~AdvanceCounter() override{};    
      CHIP_ERROR Init(T aStartValue)
      {
        return CHIP_NO_ERROR;
      }
      CHIP_ERROR Advance() override
      {
        return CHIP_NO_ERROR;
      }
      T GetValue() override { return mCounterValue; }

protected:
    T mCounterValue;
    const char* mlabel;
    const char* mgroup;
};

}
#endif
