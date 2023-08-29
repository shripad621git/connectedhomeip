#include "counter.h"

namespace Insights{
InstantObject* InstantObject::m_head = nullptr;

InstantObject* InstantObject::getInstance(const char* label, const char* group) {

        InstantObject* current = m_head;
        InstantObject* previous = nullptr;

        while (current != nullptr) {
            if (strcmp(current->label, label) == 0 && strcmp(current->group, group) == 0) {
                current->instanceCount++;
                return current;
            }
            previous = current;
            current = current->m_next;
        }

        InstantObject* newInstance = new InstantObject(label, group);
        newInstance->m_next = m_head;
        m_head = newInstance;
        return newInstance;
}

int InstantObject::getInstanceCount() const {
    return instanceCount;
}

}
