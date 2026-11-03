#pragma once

#include <mutex>

namespace AudioDevices {
inline std::recursive_mutex& controlMutex() {
    static std::recursive_mutex mutex;
    return mutex;
}
}
