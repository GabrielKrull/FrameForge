#pragma once

#include "core/common.h"
#include "diagnostics/Metrics.h"

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

    // Open the capture session (source reader + selected type are ready)
    // This is a no-op if already open; the selected type is applied on demand.
    HRESULT OpenCapture();

    // Run a synchronous ReadSample loop for durationSec seconds,
    // recording QPC timestamps of each received frame into 'metrics'.
    // expectedFPS is used for dropped-frame estimation.
    HRESULT RunCapture(double durationSec, Metrics& metrics, double expectedFPS);

    // Tear down the source reader and reset state
    void Close();

    // QPC frequency in ticks-per-second (cached at construction)
    int64_t QpcFrequency() const { return m_qpcFrequency; }

private:
    bool m_open = false;

    int64_t m_qpcFrequency = 0;

    ComPtr<IMFMediaSource> m_mediaSource;
    ComPtr<IMFSourceReader> m_reader;
    std::vector<MediaTypeInfo> m_types;
    int m_selectedIndex = -1;
};

} // namespace frameforge
