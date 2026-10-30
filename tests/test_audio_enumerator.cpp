#include "audio/AudioEnumerator.h"
#include "audio/AudioFormatUtils.h"

#include <cstdio>
#include <vector>

using namespace frameforge;

static int g_total = 0;
static int g_passed = 0;

#define TEST_ASSERT(expr) do { \
    ++g_total; \
    if (expr) { ++g_passed; } \
    else { std::printf("  FAIL: %s (line %d)\n", #expr, __LINE__); } \
} while(0)

int main() {
    std::printf("Running test_audio_enumerator...\n");

    // 1. Empty list
    {
        std::vector<AudioDeviceInfo> empty;
        TEST_ASSERT(AudioEnumerator::FindDevice(empty, 0) == -1);
        TEST_ASSERT(AudioEnumerator::FindDevice(empty, -1, "test") == -1);
    }

    // 2. Exact index selection
    {
        std::vector<AudioDeviceInfo> list = {
            { 0, "id0", "Microphone (Realtek Audio)", false, {} },
            { 1, "id1", "USB Audio Capture", false, {} },
            { 2, "id2", "Virtual Cable", true, {} }
        };

        TEST_ASSERT(AudioEnumerator::FindDevice(list, 1) == 1);
        TEST_ASSERT(AudioEnumerator::FindDevice(list, 2) == 2);
    }

    // 3. Name matching (case-insensitive substring)
    {
        std::vector<AudioDeviceInfo> list = {
            { 0, "id0", "Microphone (Realtek Audio)", false, {} },
            { 1, "id1", "USB Audio Capture Device", false, {} },
            { 2, "id2", "Virtual Cable", true, {} }
        };

        TEST_ASSERT(AudioEnumerator::FindDevice(list, -1, "usb") == 1);
        TEST_ASSERT(AudioEnumerator::FindDevice(list, -1, "CAPTURE") == 1);
        TEST_ASSERT(AudioEnumerator::FindDevice(list, -1, "realtek") == 0);
    }

    // 4. Default device fallback
    {
        std::vector<AudioDeviceInfo> list = {
            { 0, "id0", "Microphone (Realtek Audio)", false, {} },
            { 1, "id1", "USB Audio Capture Device", false, {} },
            { 2, "id2", "Virtual Cable", true, {} }
        };

        // Out of range index and unknown name -> falls back to default (index 2)
        TEST_ASSERT(AudioEnumerator::FindDevice(list, 99, "nonexistent") == 2);
    }

    // 5. Format conversion logic
    {
        WAVEFORMATEX wfx{};
        wfx.wFormatTag = WAVE_FORMAT_PCM;
        wfx.nChannels = 2;
        wfx.nSamplesPerSec = 48000;
        wfx.wBitsPerSample = 16;
        wfx.nBlockAlign = 4;
        wfx.nAvgBytesPerSec = 48000 * 4;

        AudioFormat fmt = WaveFormatToAudioFormat(&wfx);
        TEST_ASSERT(fmt.sampleRate == 48000);
        TEST_ASSERT(fmt.channels == 2);
        TEST_ASSERT(fmt.bitsPerSample == 16);
        TEST_ASSERT(fmt.sampleFormat == AudioSampleFormat::Pcm16);
        TEST_ASSERT(fmt.BlockAlign() == 4);
        TEST_ASSERT(fmt.BytesPerSecond() == 192000);
    }

    // 6. IEEE Float format conversion
    {
        WAVEFORMATEX wfx{};
        wfx.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
        wfx.nChannels = 2;
        wfx.nSamplesPerSec = 44100;
        wfx.wBitsPerSample = 32;

        AudioFormat fmt = WaveFormatToAudioFormat(&wfx);
        TEST_ASSERT(fmt.sampleRate == 44100);
        TEST_ASSERT(fmt.sampleFormat == AudioSampleFormat::Float32);
    }

    std::printf("test_audio_enumerator: %d/%d passed\n", g_passed, g_total);
    return (g_passed == g_total) ? 0 : 1;
}

