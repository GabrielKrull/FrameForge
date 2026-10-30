#pragma once

#include "core/common.h"

namespace frameforge {

struct VirtualCameraSupport {
    HRESULT result = E_FAIL;
    bool supported = false;
};

// Queries the Windows Media Foundation software virtual camera capability.
// This does not register a camera or validate a custom media source.
VirtualCameraSupport QueryVirtualCameraSupport();

} // namespace frameforge
