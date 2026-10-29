#pragma once

#include "core/common.h"

namespace frameforge {

// ============================================================
// Information about an enumerated video capture device
// ============================================================
struct DeviceInfo {
    int         index       = 0;
    std::string friendlyName;
    std::string symbolicLink;

    // Internal: MF activate object used to create the device source
    IMFActivate* activate = nullptr; // owned by caller after EnumerateVideo
};

// ============================================================
// Enumerates video capture devices via MFEnumDeviceSources.
// Requires MFStartup to have been called.
// ============================================================
class DeviceEnumerator {
public:
    DeviceEnumerator() = default;
    ~DeviceEnumerator() = default;

    DeviceEnumerator(const DeviceEnumerator&) = delete;
    DeviceEnumerator& operator=(const DeviceEnumerator&) = delete;

    // Enumerate all video capture devices.
    // On success, fills 'devices' and returns S_OK.
    // If none found, returns S_OK with empty list.
    HRESULT EnumerateVideo(std::vector<DeviceInfo>& devices);

    // Release all IMFActivate objects held by the enumerator.
    // Call this after you are done with the devices.
    void ReleaseActivators();

private:
    std::vector<ComPtr<IMFActivate>> m_activators;
};

} // namespace frameforge
