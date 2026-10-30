#pragma once

#include "audio/AudioTypes.h"
#include "core/common.h"

#include <audioclient.h>
#include <mmdeviceapi.h>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>

namespace frameforge {

class AudioRenderer {
public:
    AudioRenderer();
    ~AudioRenderer();

    AudioRenderer(const AudioRenderer&) = delete;
    AudioRenderer& operator=(const AudioRenderer&) = delete;

    // Initialize renderer on default or specific render endpoint
    HRESULT Initialize(const AudioFormat& inputFormat,
                       const std::string& endpointId = "");

    HRESULT Start();
    void Stop();
    void Close();

    // Submit an audio packet for playback. Non-blocking; drops on congestion.
    void RenderPacket(const AudioPacket& packet);

    bool IsActive() const { return m_active.load(std::memory_order_relaxed); }
    const std::string& DeviceName() const { return m_deviceName; }

private:
    ComPtr<IMMDevice>          m_device;
    ComPtr<IAudioClient>       m_audioClient;
    ComPtr<IAudioRenderClient> m_renderClient;

    WAVEFORMATEX*              m_pMixFormat = nullptr;
    AudioFormat                m_format;
    AudioFormat                m_inputFormat;
    std::string                m_deviceName;

    UINT32                     m_bufferFrameCount = 0;
    std::atomic<bool>          m_active = false;
    std::mutex                 m_mutex;
};

} // namespace frameforge

