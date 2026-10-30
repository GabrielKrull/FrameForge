#pragma once

#include <cstdint>
#include <format>
#include <functional>
#include <string>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <audioclient.h>

namespace frameforge {

// ============================================================
// Supported sample representations
// ============================================================
enum class AudioSampleFormat {
    Pcm16,
    Pcm24,
    Pcm32,
    Float32,
    Unknown,
};

inline const char* AudioSampleFormatToString(AudioSampleFormat fmt) {
    switch (fmt) {
        case AudioSampleFormat::Pcm16:   return "PCM 16-bit";
        case AudioSampleFormat::Pcm24:   return "PCM 24-bit";
        case AudioSampleFormat::Pcm32:   return "PCM 32-bit";
        case AudioSampleFormat::Float32: return "IEEE Float 32-bit";
        default:                         return "Unknown";
    }
}

// ============================================================
// Audio stream format descriptor
// ============================================================
struct AudioFormat {
    uint32_t          sampleRate    = 48000;
    uint16_t          channels      = 2;
    uint16_t          bitsPerSample = 16;
    AudioSampleFormat sampleFormat  = AudioSampleFormat::Pcm16;

    uint32_t BlockAlign() const {
        return static_cast<uint32_t>(channels) * (bitsPerSample / 8u);
    }

    uint32_t BytesPerSecond() const {
        return sampleRate * BlockAlign();
    }

    std::string ToString() const {
        return std::format("{} ch, {} Hz, {} ({})",
            channels, sampleRate, bitsPerSample,
            AudioSampleFormatToString(sampleFormat));
    }
};

// ============================================================
// One packet of captured audio data.
// ============================================================
struct AudioPacket {
    std::vector<uint8_t> data;
    uint32_t             frames         = 0; // Number of sample frames
    int64_t              qpcTimestamp   = 0; // QPC ticks at capture
    uint64_t             devicePosition = 0; // Hardware stream sample count
    DWORD                flags          = 0; // AUDCLNT_BUFFERFLAGS_*

    bool IsDiscontinuity() const {
        return (flags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY) != 0;
    }

    bool IsSilent() const {
        return (flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0;
    }

    double DurationSeconds(uint32_t sampleRate) const {
        return (sampleRate > 0) ? (static_cast<double>(frames) / sampleRate) : 0.0;
    }
};

using AudioPacketCallback = std::function<void(AudioPacket)>;

} // namespace frameforge

