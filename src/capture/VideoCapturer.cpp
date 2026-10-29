#include "capture/VideoCapturer.h"
#include "diagnostics/Logger.h"

#include <algorithm>
#include <format>

namespace frameforge {

namespace {

// Score a media type for auto-selection. Higher is better.
double ScoreMediaType(const MediaTypeInfo& t) {
    double score = 0.0;

    // Prefer uncompressed formats
    if (t.subtype == MFVideoFormat_NV12)        score += 1000.0;
    else if (t.subtype == MFVideoFormat_UYVY)   score += 900.0;
    else if (t.subtype == MFVideoFormat_YUY2)   score += 900.0;
    else if (t.subtype == MFVideoFormat_RGB32)  score += 800.0;
    else if (t.subtype == MFVideoFormat_RGB24)  score += 800.0;
    else if (t.subtype == MFVideoFormat_ARGB32) score += 800.0;
    else if (t.subtype == MFVideoFormat_MJPG)   score += 200.0;
    else                                        score += 100.0;

    // Prefer higher resolution (up to 1080p)
    const double pixels =
        static_cast<double>(t.width) * static_cast<double>(t.height);
    const double maxDesiredPixels = 1920.0 * 1080.0;
    if (pixels <= maxDesiredPixels) {
        score += pixels / 10000.0;
    } else {
        score += maxDesiredPixels / 10000.0
                 - (pixels - maxDesiredPixels) / 100000.0;
    }

    // Prefer higher FPS (up to 60)
    double fps = t.Fps();
    if (fps > 60.0) fps = 60.0;
    score += fps;

    return score;
}

} // anonymous namespace

VideoCapturer::VideoCapturer() {
    LARGE_INTEGER freq{};
    QueryPerformanceFrequency(&freq);
    m_qpcFrequency = freq.QuadPart;
}

VideoCapturer::~VideoCapturer() {
    Close();
}

HRESULT VideoCapturer::Initialize(IMFActivate* activate) {
    Close();

    if (!activate) {
        Logger::Global().Error("VideoCapturer", "Activate object is null");
        return E_INVALIDARG;
    }

    // Activate the device as an IMFMediaSource
    HRESULT hr = activate->ActivateObject(
        IID_PPV_ARGS(&m_mediaSource));
    if (FAILED(hr)) {
        Logger::Global().Error("VideoCapturer",
            std::format("ActivateObject(IMFMediaSource) failed: {}",
                HResultToString(hr)));
        return hr;
    }

    // Create a source reader from the media source
    hr = MFCreateSourceReaderFromMediaSource(
        m_mediaSource.Get(), nullptr, &m_reader);
    if (FAILED(hr)) {
        Logger::Global().Error("VideoCapturer",
            std::format("MFCreateSourceReaderFromMediaSource failed: {}",
                HResultToString(hr)));
        m_mediaSource.Reset();
        return hr;
    }

    Logger::Global().Info("VideoCapturer",
        "Media source activated and source reader created");
    return S_OK;
}

HRESULT VideoCapturer::QueryMediaTypes(std::vector<MediaTypeInfo>& types) {
    types.clear();
    m_types.clear();

    if (!m_reader) {
        Logger::Global().Error("VideoCapturer",
            "QueryMediaTypes called before Initialize");
        return E_NOT_VALID_STATE;
    }

    for (DWORD index = 0; ; ++index) {
        IMFMediaType* type = nullptr;
        HRESULT hr = m_reader->GetNativeMediaType(
            static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), index, &type);
        if (FAILED(hr)) break;

        MediaTypeInfo info;
        info.index = static_cast<int>(m_types.size());

        GUID subtype{};
        if (SUCCEEDED(type->GetGUID(MF_MT_SUBTYPE, &subtype))) {
            info.subtype = subtype;
        }

        // MF_MT_FRAME_SIZE is packed as (width << 32) | height
        UINT64 frameSize = 0;
        if (SUCCEEDED(type->GetUINT64(MF_MT_FRAME_SIZE, &frameSize))) {
            info.width  = static_cast<UINT32>(frameSize >> 32);
            info.height = static_cast<UINT32>(frameSize & 0xFFFFFFFF);
        }

        UINT64 frameRate = 0;
        if (SUCCEEDED(type->GetUINT64(MF_MT_FRAME_RATE, &frameRate))) {
            info.fpsNum = static_cast<UINT32>(frameRate >> 32);
            info.fpsDen = static_cast<UINT32>(frameRate & 0xFFFFFFFF);
        }

        m_types.push_back(info);
        types.push_back(info);
        type->Release();
    }

    Logger::Global().Info("VideoCapturer",
        std::format("Found {} media type(s)", m_types.size()));

    return S_OK;
}

HRESULT VideoCapturer::SelectMediaType(int index) {
    if (index < 0 || index >= static_cast<int>(m_types.size())) {
        Logger::Global().Error("VideoCapturer",
            std::format("Media type index {} out of range (0..{})",
                index, static_cast<int>(m_types.size()) - 1));
        return E_INVALIDARG;
    }

    IMFMediaType* type = nullptr;
    HRESULT hr = m_reader->GetNativeMediaType(
        static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM),
        static_cast<DWORD>(index),
        &type);
    if (FAILED(hr)) {
        Logger::Global().Error("VideoCapturer",
            std::format("GetNativeMediaType({}) failed: {}",
                index, HResultToString(hr)));
        return hr;
    }

    hr = m_reader->SetCurrentMediaType(
        static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), nullptr, type);
    type->Release();

    if (FAILED(hr)) {
        Logger::Global().Error("VideoCapturer",
            std::format("SetCurrentMediaType failed: {}",
                HResultToString(hr)));
        return hr;
    }

    m_selectedIndex = index;
    m_open = true;

    const auto& info = m_types[index];
    Logger::Global().Info("VideoCapturer",
        std::format("Selected media type [{}]: {} {}x{} @ {:.2f} FPS",
            index, info.SubtypeName(), info.width, info.height, info.Fps()));

    return S_OK;
}

HRESULT VideoCapturer::SelectMediaTypeAuto() {
    if (m_types.empty()) {
        Logger::Global().Error("VideoCapturer",
            "SelectMediaTypeAuto called with no media types");
        return E_NOT_VALID_STATE;
    }

    int bestIndex = 0;
    double bestScore = -1.0;

    for (size_t i = 0; i < m_types.size(); ++i) {
        double score = ScoreMediaType(m_types[i]);
        if (score > bestScore) {
            bestScore = score;
            bestIndex = static_cast<int>(i);
        }
    }

    Logger::Global().Info("VideoCapturer",
        std::format("Auto-selected media type [{}] (score {:.1f})",
            bestIndex, bestScore));

    return SelectMediaType(bestIndex);
}

HRESULT VideoCapturer::OpenCapture() {
    if (!m_reader || m_selectedIndex < 0) {
        Logger::Global().Error("VideoCapturer",
            "OpenCapture called before a media type is selected");
        return E_NOT_VALID_STATE;
    }

    // Source reader is already configured with the selected type.
    m_open = true;
    Logger::Global().Info("VideoCapturer", "Capture session opened");
    return S_OK;
}

HRESULT VideoCapturer::RunCapture(
    double durationSec, Metrics& metrics, double expectedFPS) {

    if (!m_reader || !m_open) {
        Logger::Global().Error("VideoCapturer",
            "RunCapture called before OpenCapture");
        return E_NOT_VALID_STATE;
    }

    metrics.Reset();
    metrics.SetExpectedFPS(expectedFPS);

    LARGE_INTEGER start{}, now{}, freq{};
    QueryPerformanceCounter(&start);
    QueryPerformanceFrequency(&freq);

    const double ticksPerSec = static_cast<double>(freq.QuadPart);
    const double durationTicks = durationSec * ticksPerSec;

    uint64_t sampleCount = 0;
    uint64_t errorCount  = 0;

    Logger::Global().Info("VideoCapturer",
        std::format("Capture started ({:.1f}s)", durationSec));

    for (;;) {
        QueryPerformanceCounter(&now);
        double elapsedTicks =
            static_cast<double>(now.QuadPart - start.QuadPart);
        if (elapsedTicks >= durationTicks) break;

        IMFSample* sample = nullptr;
        DWORD streamIndex = 0, flags = 0;
        LONGLONG timestamp = 0;

        HRESULT hr = m_reader->ReadSample(
            static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM),
            0, &streamIndex, &flags, &timestamp, &sample);

        if (FAILED(hr)) {
            ++errorCount;
            Logger::Global().Warn("VideoCapturer",
                std::format("ReadSample failed: {}", HResultToString(hr)));
            if (sample) { sample->Release(); sample = nullptr; }
            if (errorCount >= 10) {
                Logger::Global().Error("VideoCapturer",
                    "Too many consecutive ReadSample errors; aborting");
                break;
            }
            continue;
        }

        if (sample) {
            // Record arrival time via QPC
            LARGE_INTEGER arrival{};
            QueryPerformanceCounter(&arrival);
            metrics.RecordFrame(arrival.QuadPart);
            ++sampleCount;
            sample->Release();
        }

        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) {
            Logger::Global().Info("VideoCapturer", "End of stream reached");
            break;
        }
        if (flags & MF_SOURCE_READERF_ERROR) {
            ++errorCount;
            Logger::Global().Warn("VideoCapturer",
                "Source reader reported stream error");
            if (errorCount >= 10) {
                Logger::Global().Error("VideoCapturer",
                    "Too many stream errors; aborting");
                break;
            }
        }
        if (flags & MF_SOURCE_READERF_NEWSTREAM) {
            Logger::Global().Debug("VideoCapturer", "New stream detected");
        }
        if (flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED) {
            Logger::Global().Debug("VideoCapturer", "Media type changed");
        }
    }

    QueryPerformanceCounter(&now);
    double actualDurationSec =
        static_cast<double>(now.QuadPart - start.QuadPart) / ticksPerSec;

    Logger::Global().Info("VideoCapturer",
        std::format("Capture ended: {} samples, {} errors, {:.2f}s",
            sampleCount, errorCount, actualDurationSec));

    return S_OK;
}

void VideoCapturer::Close() {
    if (m_reader) {
        m_reader.Reset();
        Logger::Global().Debug("VideoCapturer", "Source reader released");
    }
    if (m_mediaSource) {
        m_mediaSource->Shutdown();
        m_mediaSource.Reset();
        Logger::Global().Debug("VideoCapturer", "Media source shut down");
    }
    m_open = false;
    m_selectedIndex = -1;
    m_types.clear();
}

} // namespace frameforge
