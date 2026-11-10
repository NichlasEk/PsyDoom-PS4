#pragma once

// PS4 stub for SoundTouch (audio resampling library not available on PS4)

#include <cstdint>

namespace soundtouch {

// Setting IDs — also in global namespace for compatibility
enum {
    SETTING_USE_AA_FILTER = 0,
    SETTING_AA_FILTER_LENGTH = 1,
    SETTING_USE_QUICKSEEK = 2,
    SETTING_SEQUENCE_MS = 3,
    SETTING_SEEKWINDOW_MS = 4,
    SETTING_OVERLAP_MS = 5,
    SETTING_NOMINAL_INPUT_SEQUENCE = 6,
    SETTING_NOMINAL_OUTPUT_SEQUENCE = 7,
};

} // namespace soundtouch

// Global namespace aliases for SoundTouch setting IDs
using soundtouch::SETTING_USE_AA_FILTER;
using soundtouch::SETTING_AA_FILTER_LENGTH;
using soundtouch::SETTING_USE_QUICKSEEK;
using soundtouch::SETTING_SEQUENCE_MS;
using soundtouch::SETTING_SEEKWINDOW_MS;
using soundtouch::SETTING_OVERLAP_MS;
using soundtouch::SETTING_NOMINAL_INPUT_SEQUENCE;
using soundtouch::SETTING_NOMINAL_OUTPUT_SEQUENCE;

namespace soundtouch {

class SoundTouch {
public:
    SoundTouch() = default;
    ~SoundTouch() = default;

    void setSampleRate(uint32_t srate) {}
    void setChannels(uint32_t channels) {}
    void setTempoChange(double tempo) {}
    void setPitch(double pitch) {}
    void setRate(double rate) {}
    void setTempo(double tempo) {}
    void setSetting(int settingId, int value) {}
    void clear() {}
    void flush() {}

    uint32_t putSamples(const float* samples, uint32_t count) { return count; }
    uint32_t receiveSamples(float* out, uint32_t max) { return 0; }
    uint32_t receiveSamples(uint32_t max) { return 0; }
    uint32_t numSamples() const { return 0; }
    uint32_t numUnprocessedSamples() const { return 0; }
    bool isEmpty() const { return true; }

    float* bufBegin() { return m_buf; }
    double getInputOutputSampleRatio() const { return 1.0; }

private:
    float m_buf[1] = {0};
};

} // namespace soundtouch
