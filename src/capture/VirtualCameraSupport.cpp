#include "capture/VirtualCameraSupport.h"

#include <mfvirtualcamera.h>

namespace frameforge {

VirtualCameraSupport QueryVirtualCameraSupport() {
    BOOL supported = FALSE;
    const HRESULT hr = MFIsVirtualCameraTypeSupported(
        MFVirtualCameraType_SoftwareCameraSource, &supported);

    return VirtualCameraSupport{ hr, SUCCEEDED(hr) && supported != FALSE };
}

} // namespace frameforge
