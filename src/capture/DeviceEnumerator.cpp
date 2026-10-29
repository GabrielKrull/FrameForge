#include "capture/DeviceEnumerator.h"
#include "diagnostics/Logger.h"

#include <format>

namespace frameforge {

HRESULT DeviceEnumerator::EnumerateVideo(std::vector<DeviceInfo>& devices) {
    devices.clear();
    ReleaseActivators();

    // Create attributes object specifying video capture device type
    ComPtr<IMFAttributes> attrs;
    HRESULT hr = MFCreateAttributes(&attrs, 1);
    if (FAILED(hr)) {
        Logger::Global().Error("DeviceEnumerator",
            std::format("MFCreateAttributes failed: {}", HResultToString(hr)));
        return hr;
    }

    hr = attrs->SetGUID(
        MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
        MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
    if (FAILED(hr)) {
        Logger::Global().Error("DeviceEnumerator",
            std::format("SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE) failed: {}",
                HResultToString(hr)));
        return hr;
    }

    IMFActivate** rawActivators = nullptr;
    UINT32 count = 0;

    hr = MFEnumDeviceSources(attrs.Get(), &rawActivators, &count);
    if (FAILED(hr)) {
        Logger::Global().Error("DeviceEnumerator",
            std::format("MFEnumDeviceSources failed: {}", HResultToString(hr)));
        return hr;
    }

    Logger::Global().Info("DeviceEnumerator",
        std::format("MFEnumDeviceSources returned {} device(s)", count));

    for (UINT32 i = 0; i < count; ++i) {
        if (rawActivators[i] == nullptr) continue;

        ComPtr<IMFActivate> act(rawActivators[i]);

        // Read friendly name
        WCHAR* friendlyName = nullptr;
        UINT32 nameLen = 0;
        HRESULT hrName = act->GetAllocatedString(
            MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &friendlyName, &nameLen);

        std::string name = "<unknown>";
        if (SUCCEEDED(hrName) && friendlyName) {
            name = WideToUtf8(friendlyName);
            CoTaskMemFree(friendlyName);
            friendlyName = nullptr;
        }

        // Read symbolic link (useful for debugging / identification)
        WCHAR* symlink = nullptr;
        UINT32 symlinkLen = 0;
        HRESULT hrSym = act->GetAllocatedString(
            MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK,
            &symlink, &symlinkLen);

        std::string sym;
        if (SUCCEEDED(hrSym) && symlink) {
            sym = WideToUtf8(symlink);
            CoTaskMemFree(symlink);
            symlink = nullptr;
        }

        DeviceInfo info;
        info.index       = static_cast<int>(devices.size());
        info.friendlyName = name;
        info.symbolicLink = sym;
        info.activate    = act.Get();

        m_activators.push_back(act);
        devices.push_back(info);

        Logger::Global().Info("DeviceEnumerator",
            std::format("Device [{}]: \"{}\"", info.index, name));
    }

    // MFEnumDeviceSources allocates the array itself with CoTaskMemAlloc;
    // the activators were adopted by ComPtr above and are released when
    // m_activators is cleared.
    if (rawActivators) {
        CoTaskMemFree(rawActivators);
        rawActivators = nullptr;
    }

    Logger::Global().Info("DeviceEnumerator",
        std::format("Found {} video capture device(s)", devices.size()));

    return S_OK;
}

void DeviceEnumerator::ReleaseActivators() {
    m_activators.clear();
}

} // namespace frameforge
