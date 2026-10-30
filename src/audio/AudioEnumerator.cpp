#include "audio/AudioEnumerator.h"
#include "audio/AudioFormatUtils.h"
#include "diagnostics/Logger.h"

#include <audioclient.h>
#include <functiondiscoverykeys_devpkey.h>
#include <mmdeviceapi.h>
#include <propsys.h>
#include <propvarutil.h>

#include <algorithm>
#include <format>

namespace frameforge {

HRESULT AudioEnumerator::EnumerateCapture(std::vector<AudioDeviceInfo>& devices) {
    return EnumerateEndpoints(eCapture, devices);
}

HRESULT AudioEnumerator::EnumerateRender(std::vector<AudioDeviceInfo>& devices) {
    return EnumerateEndpoints(eRender, devices);
}

HRESULT AudioEnumerator::EnumerateEndpoints(
    EDataFlow dataFlow,
    std::vector<AudioDeviceInfo>& devices) {

    devices.clear();

    ComPtr<IMMDeviceEnumerator> enumerator;
    HRESULT hr = CoCreateInstance(
        __uuidof(MMDeviceEnumerator),
        nullptr,
        CLSCTX_ALL,
        IID_PPV_ARGS(&enumerator));

    if (FAILED(hr)) {
        Logger::Global().Error("AudioEnumerator",
            std::format("CoCreateInstance(MMDeviceEnumerator) failed: {}",
                HResultToString(hr)));
        return hr;
    }

    // Determine default endpoint ID for comparison
    std::wstring defaultId;
    ComPtr<IMMDevice> defaultDevice;
    if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(dataFlow, eMultimedia, &defaultDevice))) {
        LPWSTR pDefaultStr = nullptr;
        if (SUCCEEDED(defaultDevice->GetId(&pDefaultStr)) && pDefaultStr) {
            defaultId = pDefaultStr;
            CoTaskMemFree(pDefaultStr);
        }
    }

    ComPtr<IMMDeviceCollection> collection;
    hr = enumerator->EnumAudioEndpoints(
        dataFlow, DEVICE_STATE_ACTIVE, &collection);

    if (FAILED(hr)) {
        Logger::Global().Error("AudioEnumerator",
            std::format("EnumAudioEndpoints failed: {}", HResultToString(hr)));
        return hr;
    }

    UINT count = 0;
    hr = collection->GetCount(&count);
    if (FAILED(hr)) {
        Logger::Global().Error("AudioEnumerator",
            std::format("IMMDeviceCollection::GetCount failed: {}",
                HResultToString(hr)));
        return hr;
    }

    const char* flowStr = (dataFlow == eCapture) ? "capture" : "render";
    Logger::Global().Info("AudioEnumerator",
        std::format("Found {} active audio {} endpoint(s)", count, flowStr));

    for (UINT i = 0; i < count; ++i) {
        ComPtr<IMMDevice> device;
        hr = collection->Item(i, &device);
        if (FAILED(hr) || !device) continue;

        AudioDeviceInfo info;
        info.index = static_cast<int>(devices.size());

        LPWSTR pId = nullptr;
        if (SUCCEEDED(device->GetId(&pId)) && pId) {
            info.id = WideToUtf8(pId);
            if (defaultId == pId) {
                info.isDefault = true;
            }
            CoTaskMemFree(pId);
        }

        ComPtr<IPropertyStore> props;
        if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &props)) && props) {
            PROPVARIANT var;
            PropVariantInit(&var);
            if (SUCCEEDED(props->GetValue(PKEY_Device_FriendlyName, &var))) {
                if (var.vt == VT_LPWSTR && var.pwszVal) {
                    info.friendlyName = WideToUtf8(var.pwszVal);
                }
            }
            PropVariantClear(&var);
        }

        // Query mix format
        ComPtr<IAudioClient> audioClient;
        if (SUCCEEDED(device->Activate(
                __uuidof(IAudioClient), CLSCTX_ALL, nullptr, &audioClient)) && audioClient) {
            WAVEFORMATEX* pMixFormat = nullptr;
            if (SUCCEEDED(audioClient->GetMixFormat(&pMixFormat)) && pMixFormat) {
                info.mixFormat = WaveFormatToAudioFormat(pMixFormat);
                CoTaskMemFree(pMixFormat);
            }
        }

        Logger::Global().Debug("AudioEnumerator",
            std::format("Audio [{}] \"{}\" default={} format=[{}]",
                info.index, info.friendlyName, info.isDefault ? "yes" : "no",
                info.mixFormat.ToString()));

        devices.push_back(std::move(info));
    }

    return S_OK;
}

int AudioEnumerator::FindDevice(
    const std::vector<AudioDeviceInfo>& devices,
    int requestedIndex,
    std::string_view preferredName) {

    if (devices.empty()) return -1;

    // 1. If explicit valid index
    if (requestedIndex >= 0 &&
        requestedIndex < static_cast<int>(devices.size())) {
        return requestedIndex;
    }

    // 2. Search by preferred friendly name (case-insensitive substring)
    if (!preferredName.empty()) {
        std::string search(preferredName);
        std::transform(search.begin(), search.end(), search.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

        for (std::size_t i = 0; i < devices.size(); ++i) {
            std::string name = devices[i].friendlyName;
            std::transform(name.begin(), name.end(), name.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (name.find(search) != std::string::npos) {
                return static_cast<int>(i);
            }
        }
    }

    // 3. Fallback to default device
    for (std::size_t i = 0; i < devices.size(); ++i) {
        if (devices[i].isDefault) {
            return static_cast<int>(i);
        }
    }

    // 4. Fallback to first available
    return 0;
}

} // namespace frameforge
