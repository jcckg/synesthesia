#include "audio/input/device_identity.h"
#include <algorithm>
#include <fstream>
#include <functional>
#include <sstream>
#include <vector>
#include <cstdlib>
#ifdef __APPLE__
#include <CoreAudio/CoreAudio.h>
#include <CoreFoundation/CoreFoundation.h>
#elif defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mmdeviceapi.h>
#else
#include <alsa/asoundlib.h>
#endif


namespace AudioDevices {
std::string topologyFingerprint() {
    std::vector<std::string> entries;
#ifdef __APPLE__
    AudioObjectPropertyAddress address{kAudioHardwarePropertyDevices, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
    UInt32 bytes = 0;
    if (AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &address, 0, nullptr, &bytes) != noErr) return {};
    std::vector<AudioDeviceID> devices(bytes / sizeof(AudioDeviceID));
    if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &address, 0, nullptr, &bytes, devices.data()) != noErr) return {};
    for (const auto device : devices) {
        entries.push_back(std::to_string(device));
        AudioObjectPropertyAddress alive{kAudioDevicePropertyDeviceIsAlive, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
        UInt32 value = 0, size = sizeof(value);
        if (AudioObjectGetPropertyData(device, &alive, 0, nullptr, &size, &value) == noErr)
            entries.push_back(std::to_string(device) + ":" + std::to_string(value));
    }
#elif defined(_WIN32)
    const HRESULT initialised = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    IMMDeviceEnumerator* enumerator = nullptr;
    if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator)))) {
        IMMDeviceCollection* devices = nullptr;
        if (SUCCEEDED(enumerator->EnumAudioEndpoints(eAll, DEVICE_STATEMASK_ALL, &devices))) {
            UINT count = 0;
            devices->GetCount(&count);
            for (UINT i = 0; i < count; ++i) {
                IMMDevice* device = nullptr;
                if (SUCCEEDED(devices->Item(i, &device))) {
                    LPWSTR id = nullptr;
                    DWORD state = 0;
                    if (SUCCEEDED(device->GetId(&id))) {
                        device->GetState(&state);
                        entries.push_back(std::to_string(std::hash<std::wstring>{}(id)) + ":" + std::to_string(state));
                        CoTaskMemFree(id);
                    }
                    device->Release();
                }
            }
            devices->Release();
        }
        enumerator->Release();
    }
    if (SUCCEEDED(initialised)) CoUninitialize();
#else
    for (const auto* path : {"/proc/asound/cards", "/proc/asound/pcm"}) {
        std::ifstream file(path);
        std::ostringstream data;
        data << file.rdbuf();
        entries.push_back(data.str());
    }
    void** hints = nullptr;
    if (snd_device_name_hint(-1, "pcm", &hints) == 0) {
        for (auto** hint = hints; hint && *hint; ++hint) {
            char* name = snd_device_name_get_hint(*hint, "NAME");
            if (name) { entries.emplace_back(name); free(name); }
        }
        snd_device_name_free_hint(hints);
    }
#endif
    std::sort(entries.begin(), entries.end());
    std::string result;
    for (const auto& entry : entries) { result += entry; result.push_back('\n'); }
    return result;
}

}
