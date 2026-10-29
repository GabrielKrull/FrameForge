#pragma once

// === Windows configuration ===
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

// COM smart pointers
#include <wrl/client.h>

// Media Foundation
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>

// Standard library
#include <string>
#include <string_view>
#include <vector>
#include <cstdint>
#include <format>
#include <memory>

namespace frameforge {

using Microsoft::WRL::ComPtr;

// ============================================================
// RAII wrapper for COM initialization
// ============================================================
class ComInitializer {
public:
    ComInitializer() {
        m_hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    }
    ~ComInitializer() {
        if (SUCCEEDED(m_hr)) {
            CoUninitialize();
        }
    }

    ComInitializer(const ComInitializer&) = delete;
    ComInitializer& operator=(const ComInitializer&) = delete;

    bool IsInitialized() const { return SUCCEEDED(m_hr); }
    HRESULT GetResult() const { return m_hr; }

private:
    HRESULT m_hr;
};

// ============================================================
// RAII wrapper for Media Foundation initialization
// ============================================================
class MFInitializer {
public:
    MFInitializer() {
        m_hr = MFStartup(MF_VERSION);
    }
    ~MFInitializer() {
        if (SUCCEEDED(m_hr)) {
            MFShutdown();
        }
    }

    MFInitializer(const MFInitializer&) = delete;
    MFInitializer& operator=(const MFInitializer&) = delete;

    bool IsInitialized() const { return SUCCEEDED(m_hr); }
    HRESULT GetResult() const { return m_hr; }

private:
    HRESULT m_hr;
};

// ============================================================
// Utility functions
// ============================================================

// Convert HRESULT to hex string for diagnostics
inline std::string HResultToString(HRESULT hr) {
    return std::format("0x{:08X}", static_cast<unsigned long>(hr));
}

// Convert wide string (UTF-16) to narrow string (UTF-8)
inline std::string WideToUtf8(std::wstring_view wide) {
    if (wide.empty()) return {};
    int size = WideCharToMultiByte(
        CP_UTF8, 0,
        wide.data(), static_cast<int>(wide.size()),
        nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string result(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(
        CP_UTF8, 0,
        wide.data(), static_cast<int>(wide.size()),
        result.data(), size, nullptr, nullptr);
    return result;
}

// Convert GUID to string representation
inline std::string GuidToString(const GUID& guid) {
    wchar_t buf[64];
    int len = StringFromGUID2(guid, buf, 64);
    if (len > 0) {
        return WideToUtf8(std::wstring_view(buf, static_cast<size_t>(len - 1)));
    }
    return "<unknown GUID>";
}

// Convert common video subtype GUIDs to human-readable names
inline std::string VideoSubtypeToString(const GUID& subtype) {
    if (subtype == MFVideoFormat_NV12)   return "NV12";
    if (subtype == MFVideoFormat_YUY2)   return "YUY2";
    if (subtype == MFVideoFormat_UYVY)   return "UYVY";
    if (subtype == MFVideoFormat_RGB24)  return "RGB24";
    if (subtype == MFVideoFormat_RGB32)  return "RGB32";
    if (subtype == MFVideoFormat_ARGB32) return "ARGB32";
    if (subtype == MFVideoFormat_MJPG)   return "MJPEG";
    if (subtype == MFVideoFormat_H264)   return "H264";
    if (subtype == MFVideoFormat_HEVC)   return "HEVC";
    return GuidToString(subtype);
}

} // namespace frameforge
