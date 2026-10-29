#pragma once

#include <cstdint>
#include <vector>

namespace frameforge {

// ============================================================
// Pixel layouts understood by the preview renderer.
// The capture pipeline requests NV12 with a BGRA fallback so
// that no CPU-side color conversion is required.
// ============================================================
enum class FrameFormat {
    NV12,
    BGRA,
};

// ============================================================
// One captured video frame.
//
// 'data' holds the raw bytes exactly as delivered by Media
// Foundation:
//   NV12 : luma plane (stride * height) followed by chroma
//          plane (stride * height / 2)
//   BGRA : stride * height bytes
//
// 'stride' is the number of bytes per row; it may be larger
// than width (hardware alignment) and is never negative.
// ============================================================
struct Frame {
    FrameFormat          format    = FrameFormat::NV12;
    uint32_t             width     = 0;
    uint32_t             height    = 0;
    uint32_t             stride    = 0;
    int64_t              timestamp = 0; // Media Foundation sample time (100 ns)
    int64_t              arrivalQpc = 0; // QPC ticks at arrival
    std::vector<uint8_t> data;
};

} // namespace frameforge
