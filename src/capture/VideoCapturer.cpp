#include "capture/VideoCapturer.h"
#include "diagnostics/Logger.h"

#include <algorithm>
#include <cstdlib>
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

// Default stride for a subtype, in bytes. Always non-negative.
uint32_t DefaultStride(const GUID& subtype, UINT32 width) {
    LONG stride = 0;
    if (SUCCEEDED(MFGetStrideForBitmapInfoHeader(
            subtype.Data1, width, &stride)) && stride != 0) {
        return static_cast<uint32_t>(stride < 0 ? -stride : stride);
    }

    if (subtype == MFVideoFormat_RGB32 ||
        subtype == MFVideoFormat_ARGB32) {
        return width * 4;
    }
    if (subtype == MFVideoFormat_RGB24) {
        return width * 3;
    }
    return width; // NV12 / YUY2-family: one stride unit per luma pixel
}

// Copy the sample payload into a contiguous frame buffer.
bool CopySampleToFrame(IMFSample* sample, Frame& frame) {
    ComPtr<IMFMediaBuffer> buffer;
    if (FAILED(sample->ConvertToContiguousBuffer(&buffer))) {
        if (FAILED(sample->GetBufferByIndex(0, &buffer))) {
            return false;
        }
    }

    BYTE* data = nullptr;
    DWORD maxLength = 0;
    DWORD currentLength = 0;
    if (FAILED(buffer->Lock(&data, &maxLength, &currentLength))) {
        return false;
    }

    frame.data.assign(data, data + currentLength);
    buffer->Unlock();

    return !frame.data.empty();
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
    m_outputSubtype = info.subtype;
    m_outputWidth   = info.width;
    m_outputHeight  = info.height;
    m_outputStride  = DefaultStride(info.subtype, info.width);
    m_outputFPS     = info.Fps();
    m_outputFormat  = (info.subtype == MFVideoFormat_NV12)
        ? FrameFormat::NV12
        : FrameFormat::BGRA;

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

HRESULT VideoCapturer::ConfigurePreviewOutput() {
    if (!m_reader || m_selectedIndex < 0) {
        Logger::Global().Error("VideoCapturer",
            "ConfigurePreviewOutput called before a media type is selected");
        return E_NOT_VALID_STATE;
    }

    // Prefer NV12 (GPU-friendly, smallest uncompressed payload).
    // Fall back to BGRA if the reader cannot negotiate NV12.
    const GUID candidates[] = {
        MFVideoFormat_NV12,
        MFVideoFormat_RGB32,
    };

    HRESULT lastError = E_FAIL;

    for (const GUID& subtype : candidates) {
        ComPtr<IMFMediaType> outputType;
        HRESULT hr = MFCreateMediaType(&outputType);
        if (FAILED(hr)) return hr;

        outputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        outputType->SetGUID(MF_MT_SUBTYPE, subtype);

        // Ask the reader to preserve the resolution/rate of the selected
        // native type instead of letting the converter pick defaults.
        const auto& native = m_types[m_selectedIndex];
        if (native.width > 0 && native.height > 0) {
            outputType->SetUINT64(MF_MT_FRAME_SIZE,
                (static_cast<UINT64>(native.width) << 32) | native.height);
        }
        if (native.fpsNum > 0 && native.fpsDen > 0) {
            outputType->SetUINT64(MF_MT_FRAME_RATE,
                (static_cast<UINT64>(native.fpsNum) << 32) | native.fpsDen);
        }

        hr = m_reader->SetCurrentMediaType(
            static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM),
            nullptr, outputType.Get());
        if (FAILED(hr)) {
            Logger::Global().Warn("VideoCapturer",
                std::format("Output type {} rejected: {}",
                    VideoSubtypeToString(subtype), HResultToString(hr)));
            lastError = hr;
            continue;
        }

        // Read back the type actually negotiated by the reader.
        ComPtr<IMFMediaType> current;
        hr = m_reader->GetCurrentMediaType(
            static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), &current);
        if (FAILED(hr)) {
            Logger::Global().Error("VideoCapturer",
                std::format("GetCurrentMediaType failed: {}",
                    HResultToString(hr)));
            return hr;
        }

        GUID actualSubtype{};
        current->GetGUID(MF_MT_SUBTYPE, &actualSubtype);

        UINT64 frameSize = 0;
        UINT32 width = 0, height = 0;
        if (SUCCEEDED(current->GetUINT64(MF_MT_FRAME_SIZE, &frameSize))) {
            width  = static_cast<UINT32>(frameSize >> 32);
            height = static_cast<UINT32>(frameSize & 0xFFFFFFFF);
        }
        if (width == 0 || height == 0) {
            Logger::Global().Error("VideoCapturer",
                "Negotiated output has no frame size");
            return E_FAIL;
        }

        UINT64 frameRate = 0;
        double fps = 0.0;
        if (SUCCEEDED(current->GetUINT64(MF_MT_FRAME_RATE, &frameRate))) {
            const UINT32 num = static_cast<UINT32>(frameRate >> 32);
            const UINT32 den = static_cast<UINT32>(frameRate & 0xFFFFFFFF);
            if (den != 0) fps = static_cast<double>(num) / den;
        }
        if (fps <= 0.0) {
            fps = m_types[m_selectedIndex].Fps();
        }

        FrameFormat format = FrameFormat::NV12;
        if (actualSubtype == MFVideoFormat_NV12) {
            format = FrameFormat::NV12;
        } else if (actualSubtype == MFVideoFormat_RGB32 ||
                   actualSubtype == MFVideoFormat_ARGB32) {
            format = FrameFormat::BGRA;
        } else {
            Logger::Global().Error("VideoCapturer",
                std::format("Unsupported negotiated output type: {}",
                    VideoSubtypeToString(actualSubtype)));
            return E_FAIL;
        }

        m_outputSubtype = actualSubtype;
        m_outputWidth   = width;
        m_outputHeight  = height;
        m_outputStride  = DefaultStride(actualSubtype, width);
        m_outputFPS     = fps;
        m_outputFormat  = format;
        m_open          = true;

        Logger::Global().Info("VideoCapturer",
            std::format("Preview output: {} {}x{} @ {:.2f} FPS",
                VideoSubtypeToString(actualSubtype), width, height, fps));
        return S_OK;
    }

    Logger::Global().Error("VideoCapturer",
        "Could not configure a preview-compatible output type");
    return lastError;
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
    double durationSec, Metrics& metrics, double expectedFPS,
    const FrameCallback& onFrame, const std::atomic_bool* stopFlag,
    bool resetMetrics) {

    if (!m_reader || !m_open) {
        Logger::Global().Error("VideoCapturer",
            "RunCapture called before OpenCapture");
        return E_NOT_VALID_STATE;
    }

    const bool useDuration = durationSec > 0.0;
    if (!useDuration && !stopFlag) {
        Logger::Global().Error("VideoCapturer",
            "RunCapture with no duration requires a stop flag");
        return E_INVALIDARG;
    }

    if (resetMetrics) {
        metrics.Reset();
    }
    metrics.SetExpectedFPS(expectedFPS);

    LARGE_INTEGER start{}, now{}, freq{};
    QueryPerformanceCounter(&start);
    QueryPerformanceFrequency(&freq);

    const double ticksPerSec = static_cast<double>(freq.QuadPart);
    const double durationTicks = durationSec * ticksPerSec;

    uint64_t sampleCount = 0;
    uint64_t errorCount  = 0;
    uint64_t delivered   = 0;

    HRESULT lastError = S_OK;
    bool abortedByError = false;

    Logger::Global().Info("VideoCapturer",
        useDuration
            ? std::format("Capture started ({:.1f}s)", durationSec)
            : "Capture started (until stopped)");

    for (;;) {
        if (stopFlag && stopFlag->load(std::memory_order_relaxed)) {
            Logger::Global().Info("VideoCapturer", "Stop requested");
            break;
        }

        QueryPerformanceCounter(&now);
        double elapsedTicks =
            static_cast<double>(now.QuadPart - start.QuadPart);
        if (useDuration && elapsedTicks >= durationTicks) break;

        IMFSample* sample = nullptr;
        DWORD streamIndex = 0, flags = 0;
        LONGLONG timestamp = 0;

        HRESULT hr = m_reader->ReadSample(
            static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM),
            0, &streamIndex, &flags, &timestamp, &sample);

        if (FAILED(hr)) {
            ++errorCount;
            lastError = hr;
            Logger::Global().Warn("VideoCapturer",
                std::format("ReadSample failed: {}", HResultToString(hr)));
            if (sample) { sample->Release(); sample = nullptr; }
            if (errorCount >= 10) {
                Logger::Global().Error("VideoCapturer",
                    "Too many consecutive ReadSample errors; aborting");
                abortedByError = true;
                break;
            }
            continue;
        }

        // A successful read breaks the consecutive-error streak.
        errorCount = 0;

        if (sample) {
            // Record arrival time via QPC
            LARGE_INTEGER arrival{};
            QueryPerformanceCounter(&arrival);
            metrics.RecordFrame(arrival.QuadPart);
            ++sampleCount;

            if (onFrame) {
                Frame frame;
                frame.format     = m_outputFormat;
                frame.width      = m_outputWidth;
                frame.height     = m_outputHeight;
                frame.stride     = m_outputStride;
                frame.timestamp  = timestamp;
                frame.arrivalQpc = arrival.QuadPart;

                if (CopySampleToFrame(sample, frame)) {
                    onFrame(std::move(frame));
                    ++delivered;
                } else {
                    Logger::Global().Warn("VideoCapturer",
                        "Failed to copy sample payload; frame skipped");
                }
            }

            sample->Release();
            sample = nullptr;
        }

        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) {
            Logger::Global().Info("VideoCapturer",
                "End of stream reached (device may have disconnected)");
            lastError = MF_E_END_OF_STREAM;
            abortedByError = true;
            break;
        }
        if (flags & MF_SOURCE_READERF_ERROR) {
            ++errorCount;
            lastError = E_FAIL;
            Logger::Global().Warn("VideoCapturer",
                "Source reader reported stream error");
            if (errorCount >= 10) {
                Logger::Global().Error("VideoCapturer",
                    "Too many stream errors; aborting");
                abortedByError = true;
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
        std::format("Capture ended: {} samples, {} delivered, {} errors, {:.2f}s{}",
            sampleCount, delivered, errorCount, actualDurationSec,
            abortedByError
                ? std::format(" (aborted: {})", HResultToString(lastError))
                : std::string()));

    return abortedByError ? lastError : S_OK;
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

    m_outputSubtype = {};
    m_outputWidth   = 0;
    m_outputHeight  = 0;
    m_outputStride  = 0;
    m_outputFPS     = 0.0;
    m_outputFormat  = FrameFormat::NV12;
}

} // namespace frameforge
