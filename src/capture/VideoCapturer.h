#pragma once

#include "core/Frame.h"
#include "core/common.h"
#include "diagnostics/Metrics.h"

#include <atomic>
#include <functional>

namespace frameforge {

// ============================================================
// Describes one native media type exposed by the device
// ============================================================
struct MediaTypeInfo {
    int     index      = 0;
    GUID    subtype    = {};
    UINT32  width      = 0;
    UINT32  height     = 0;
    UINT32  fpsNum     = 0;
    UINT32  fpsDen     = 0;

    double  Fps() const {
        if (fpsDen == 0) return 0.0;
        return static_cast<double>(fpsNum) / static_cast<double>(fpsDen);
    }

    std::string SubtypeName() const { return VideoSubtypeToString(subtype); }
};

// ============================================================
// Video capture session backed by IMFSourceReader.
// Owns a single source reader per instance.
// ============================================================
class VideoCapturer {
public:
    // Called on the capture thread for every received frame.
    // The callback must not block for long; preview hands frames
    // to a bounded queue and returns immediately.
    using FrameCallback = std::function<void(Frame&&)>;

    VideoCapturer();
    ~VideoCapturer();

    VideoCapturer(const VideoCapturer&) = delete;
    VideoCapturer& operator=(const VideoCapturer&) = delete;

    // Create the source reader from an activated device.
    // Must be called before QueryMediaTypes / SelectMediaType / RunCapture.
    HRESULT Initialize(IMFActivate* activate);

    // Query the native media types exposed by the device.
    HRESULT QueryMediaTypes(std::vector<MediaTypeInfo>& types);

    // Select a specific media type by index (from QueryMediaTypes results)
    HRESULT SelectMediaType(int index);

    // Auto-select the best native type (prefers NV12, highest resolution+FPS)
    HRESULT SelectMediaTypeAuto();

    // Request a preview-friendly output type from the source reader:
    // NV12 when available, BGRA otherwise. Media Foundation inserts
    // decoders/converters as needed, so the capture thread receives
    // frames in a single known layout (no CPU color conversion here).
    HRESULT ConfigurePreviewOutput();

    // Open the capture session (source reader + selected type are ready)
    // This is a no-op if already open; the selected type is applied on demand.
    HRESULT OpenCapture();

    // Run a synchronous ReadSample loop.
    //
    // durationSec > 0 : stop after that many seconds (wall clock).
    // durationSec <= 0: run until 'stopFlag' is set (required in that case).
    //
    // QPC arrival timestamps are recorded into 'metrics'; onFrame (when
    // provided) receives a copy of each sample's payload. When
    // resetMetrics is false the existing samples are kept, which lets
    // callers aggregate statistics across reconnections.
    //
    // Returns S_OK when the loop ended because of the duration or the
    // stop flag. If the reader reported an unrecoverable error the last
    // failing HRESULT is returned instead.
    HRESULT RunCapture(double durationSec, Metrics& metrics, double expectedFPS,
                       const FrameCallback& onFrame = {},
                       const std::atomic_bool* stopFlag = nullptr,
                       bool resetMetrics = true);

    // Tear down the source reader and reset state
    void Close();

    // QPC frequency in ticks-per-second (cached at construction)
    int64_t QpcFrequency() const { return m_qpcFrequency; }

    // True after OpenCapture / media type negotiation succeeded.
    bool IsOpen() const { return m_open; }

    // Index selected by SelectMediaType / SelectMediaTypeAuto (-1 = none)
    int SelectedMediaTypeIndex() const { return m_selectedIndex; }

    // Negotiated output properties (valid after ConfigurePreviewOutput,
    // otherwise mirrors the selected native media type).
    UINT32      OutputWidth() const { return m_outputWidth; }
    UINT32      OutputHeight() const { return m_outputHeight; }
    uint32_t    OutputStride() const { return m_outputStride; }
    FrameFormat OutputFormat() const { return m_outputFormat; }
    double      OutputFPS() const { return m_outputFPS; }
    GUID        OutputSubtype() const { return m_outputSubtype; }

private:
    bool m_open = false;

    int64_t m_qpcFrequency = 0;

    ComPtr<IMFMediaSource> m_mediaSource;
    ComPtr<IMFSourceReader> m_reader;
    std::vector<MediaTypeInfo> m_types;
    int m_selectedIndex = -1;

    // Negotiated output description
    GUID        m_outputSubtype = {};
    UINT32      m_outputWidth  = 0;
    UINT32      m_outputHeight = 0;
    uint32_t    m_outputStride = 0;
    double      m_outputFPS    = 0.0;
    FrameFormat m_outputFormat = FrameFormat::NV12;
};

} // namespace frameforge
