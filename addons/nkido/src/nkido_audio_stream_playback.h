#pragma once

#include <godot_cpp/classes/audio_frame.hpp>
#include <godot_cpp/classes/audio_stream_playback.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>

#include <atomic>
#include <cstddef>

namespace godot {

class NkidoAudioStream;

class NkidoAudioStreamPlayback : public AudioStreamPlayback {
    GDCLASS(NkidoAudioStreamPlayback, AudioStreamPlayback)

public:
    NkidoAudioStreamPlayback();
    ~NkidoAudioStreamPlayback();

    void set_stream(const Ref<NkidoAudioStream> &p_stream);

    void _start(double p_from_pos) override;
    void _stop() override;
    bool _is_playing() const override;
    int32_t _get_loop_count() const override;
    double _get_playback_position() const override;
    void _seek(double p_position) override;
    int32_t _mix(AudioFrame *p_buffer, float p_rate_scale, int32_t p_frames) override;

    // Waveform visualization (read from main thread, written from audio thread)
    PackedFloat32Array get_waveform_data() const;

protected:
    static void _bind_methods();

private:
    static constexpr std::size_t RING_SIZE = 4096;
    static constexpr std::size_t BLOCK_SIZE = 128;
    static constexpr std::size_t WAVEFORM_SIZE = 1024;

    std::size_t ring_available() const;

    Ref<NkidoAudioStream> stream_;
    bool active_ = false;

    AudioFrame ring_buffer_[RING_SIZE] = {};
    std::size_t ring_read_ = 0;
    std::size_t ring_write_ = 0;

    float temp_left_[BLOCK_SIZE] = {};
    float temp_right_[BLOCK_SIZE] = {};

    // Lock-free waveform buffer for visualization
    float waveform_buffer_[WAVEFORM_SIZE * 2] = {}; // L/R interleaved
    std::atomic<std::size_t> waveform_write_{0};
};

} // namespace godot
