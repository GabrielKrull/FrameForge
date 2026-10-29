#pragma once

#include "core/BoundedQueue.h"
#include "core/Frame.h"

namespace frameforge {

// ============================================================
// Bounded frame hand-off between the capture thread and the
// preview/rendering thread.
//
// Capacity is deliberately small (a handful of frames): the
// display path should always show the newest frame instead of
// accumulating latency.
// ============================================================
using FrameQueue = BoundedQueue<Frame>;

} // namespace frameforge
