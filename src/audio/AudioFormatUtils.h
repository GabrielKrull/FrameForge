#pragma once

#include "audio/AudioTypes.h"

#include <mmreg.h>

namespace frameforge {

inline AudioFormat WaveFormatToAudioFormat(const WAVEFORMATEX* wfx) {
    AudioFormat fmt;
    if (!wfx) return fmt;

    fmt.sampleRate = wfx->nSamplesPerSec;
    fmt.channels = wfx->nChannels;
    fmt.bitsPerSample = wfx->wBitsPerSample;

    if (wfx->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) {
        fmt.sampleFormat = AudioSampleFormat::Float32;
    } else if (wfx->wFormatTag == WAVE_FORMAT_PCM) {
        if (fmt.bitsPerSample == 16) {
            fmt.sampleFormat = AudioSampleFormat::Pcm16;
        } else if (fmt.bitsPerSample == 24) {
            fmt.sampleFormat = AudioSampleFormat::Pcm24;
        } else if (fmt.bitsPerSample == 32) {
            fmt.sampleFormat = AudioSampleFormat::Pcm32;
        } else {
            fmt.sampleFormat = AudioSampleFormat::Unknown;
        }
    } else if (wfx->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
        const auto* ext = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(wfx);
        if (ext->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT) {
            fmt.sampleFormat = AudioSampleFormat::Float32;
        } else if (ext->SubFormat == KSDATAFORMAT_SUBTYPE_PCM) {
            if (fmt.bitsPerSample == 16) {
                fmt.sampleFormat = AudioSampleFormat::Pcm16;
            } else if (fmt.bitsPerSample == 24) {
                fmt.sampleFormat = AudioSampleFormat::Pcm24;
            } else if (fmt.bitsPerSample == 32) {
                fmt.sampleFormat = AudioSampleFormat::Pcm32;
            } else {
                fmt.sampleFormat = AudioSampleFormat::Unknown;
            }
        }
    }

    return fmt;
}

} // namespace frameforge

