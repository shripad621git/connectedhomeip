#include <iostream>
#include <string.h>
#include <esp_diagnostics_metrics.h>


#define PATH1 "sys.cnt"
namespace Insights{
class InstantObject {
private:
    static InstantObject* m_head;
    char label[50];
    char group[50];
    int instanceCount;
    InstantObject* m_next;
    bool registered =false;

    InstantObject(const char* labelParam, const char* groupParam)
        : instanceCount(1), m_next(nullptr) {
        strncpy(label, labelParam, sizeof(label));
        strncpy(group, groupParam, sizeof(group));
    }

public:
    static InstantObject* getInstance(const char* label, const char* group);

    int getInstanceCount() const;

    void traceInstant() {
        std::cout << "Trace instant: Label=" << label << ", Group=" << group
                  << ", Instance Count=" << instanceCount << std::endl;
        if(!registered)
        {
            esp_diag_metrics_register("SYS_CNT",label,label,PATH1,ESP_DIAG_DATA_TYPE_UINT);
            registered = true;
        }
        esp_diag_metrics_add_uint(label,instanceCount);

    }
};

}

