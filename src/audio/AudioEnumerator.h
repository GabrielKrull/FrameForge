#pragma once

#include "audio/AudioTypes.h"
#include "core/common.h"

#include <mmdeviceapi.h>
#include <string>
#include <string_view>
#include <vector>

namespace frameforge {

struct AudioDeviceInfo {
    int         index        = -1;
    std::string id;          // Endpoint ID string (e.g. "{0.0.1.00000000}...")
    std::string friendlyName;
    bool        isDefault    = false;
    AudioFormat mixFormat;
};

class AudioEnumerator {
public:
    AudioEnumerator() = default;
    ~AudioEnumerator() = default;

    AudioEnumerator(const AudioEnumerator&) = delete;
    AudioEnumerator& operator=(const AudioEnumerator&) = delete;

    // Enumerate active audio capture endpoints (microphones, line inputs, capture cards)
    HRESULT EnumerateCapture(std::vector<AudioDeviceInfo>& devices);

    // Enumerate active audio render endpoints (speakers, headphones)
    HRESULT EnumerateRender(std::vector<AudioDeviceInfo>& devices);

    // Helper to find a device by index or partial friendly name match
    static int FindDevice(const std::vector<AudioDeviceInfo>& devices,
                          int requestedIndex,
                          std::string_view preferredName = {});

private:
    HRESULT EnumerateEndpoints(EDataFlow dataFlow,
                               std::vector<AudioDeviceInfo>& devices);
};

} // namespace frameforge

