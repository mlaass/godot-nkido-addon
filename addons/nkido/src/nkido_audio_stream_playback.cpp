#include "nkido_audio_stream_playback.h"
#include "nkido_audio_stream.h"

#include <godot_cpp/core/class_db.hpp>

#include <cedar/vm/vm.hpp>

#include <cstring>

using namespace godot;

NkidoAudioStreamPlayback::NkidoAudioStreamPlayback() = default;
NkidoAudioStreamPlayback::~NkidoAudioStreamPlayback() = default;

void NkidoAudioStreamPlayback::_bind_methods() {
}

void NkidoAudioStreamPlayback::set_stream(const Ref<NkidoAudioStream> &p_stream) {
    stream_ = p_stream;
}

std::size_t NkidoAudioStreamPlayback::ring_available() const {
    return ring_write_ - ring_read_;
}

void NkidoAudioStreamPlayback::_start(double p_from_pos) {
    active_ = true;
    ring_read_ = 0;
    ring_write_ = 0;
}

void NkidoAudioStreamPlayback::_stop() {
    active_ = false;
    std::memset(waveform_buffer_, 0, sizeof(waveform_buffer_));
    waveform_write_.store(0, std::memory_order_release);
}

bool NkidoAudioStreamPlayback::_is_playing() const {
    return active_;
}

int32_t NkidoAudioStreamPlayback::_get_loop_count() const {
    return 0;
}

double NkidoAudioStreamPlayback::_get_playback_position() const {
    return 0.0;
}

void NkidoAudioStreamPlayback::_seek(double p_position) {
}

int32_t NkidoAudioStreamPlayback::_mix(
    AudioFrame *p_buffer, float p_rate_scale, int32_t p_frames) {
    if (stream_.is_null() || !active_) {
        for (int32_t i = 0; i < p_frames; i++) {
            p_buffer[i] = {0.0f, 0.0f};
        }
        return p_frames;
    }

    auto *vm = stream_->get_vm();
    if (!vm || !stream_->is_compiled()) {
        for (int32_t i = 0; i < p_frames; i++) {
            p_buffer[i] = {0.0f, 0.0f};
        }
        return p_frames;
    }

    // Update BPM on audio thread
    vm->set_bpm(stream_->get_bpm());

    // Process button releases
    stream_->process_button_releases();

    // Fill ring buffer as needed
    while (ring_available() < static_cast<std::size_t>(p_frames)) {
        vm->process_block(temp_left_, temp_right_);
        for (std::size_t i = 0; i < BLOCK_SIZE; i++) {
            ring_buffer_[ring_write_ & (RING_SIZE - 1)] = {
                temp_left_[i], temp_right_[i]};
            ring_write_++;
        }
    }

    // Copy from ring buffer to output and write to waveform buffer
    std::size_t wpos = waveform_write_.load(std::memory_order_relaxed);
    for (int32_t i = 0; i < p_frames; i++) {
        p_buffer[i] = ring_buffer_[ring_read_ & (RING_SIZE - 1)];
        ring_read_++;

        // Write interleaved L/R to waveform buffer (circular)
        std::size_t wbuf_idx = (wpos & (WAVEFORM_SIZE - 1)) * 2;
        waveform_buffer_[wbuf_idx] = p_buffer[i].left;
        waveform_buffer_[wbuf_idx + 1] = p_buffer[i].right;
        wpos++;
    }
    waveform_write_.store(wpos, std::memory_order_release);

    return p_frames;
}

PackedFloat32Array NkidoAudioStreamPlayback::get_waveform_data() const {
    PackedFloat32Array result;
    result.resize(WAVEFORM_SIZE * 2);

    std::size_t wpos = waveform_write_.load(std::memory_order_acquire);
    // Read the most recent WAVEFORM_SIZE frames, oldest first
    std::size_t start = (wpos >= WAVEFORM_SIZE) ? wpos - WAVEFORM_SIZE : 0;
    std::size_t count = (wpos >= WAVEFORM_SIZE) ? WAVEFORM_SIZE : wpos;

    float *dst = result.ptrw();
    for (std::size_t i = 0; i < count; i++) {
        std::size_t idx = ((start + i) & (WAVEFORM_SIZE - 1)) * 2;
        dst[i * 2] = waveform_buffer_[idx];
        dst[i * 2 + 1] = waveform_buffer_[idx + 1];
    }
    // Zero-fill remaining if buffer isn't full yet
    for (std::size_t i = count; i < WAVEFORM_SIZE; i++) {
        dst[i * 2] = 0.0f;
        dst[i * 2 + 1] = 0.0f;
    }

    return result;
}
