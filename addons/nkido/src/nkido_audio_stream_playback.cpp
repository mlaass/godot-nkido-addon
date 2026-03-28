#include "nkido_audio_stream_playback.h"
#include "nkido_player.h"

#include <godot_cpp/core/class_db.hpp>

#include <cedar/vm/vm.hpp>

using namespace godot;

NkidoAudioStreamPlayback::NkidoAudioStreamPlayback() = default;
NkidoAudioStreamPlayback::~NkidoAudioStreamPlayback() = default;

void NkidoAudioStreamPlayback::_bind_methods() {
}

void NkidoAudioStreamPlayback::set_player(NkidoPlayer *p_player) {
    player_ = p_player;
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
    if (!player_ || !active_) {
        for (int32_t i = 0; i < p_frames; i++) {
            p_buffer[i] = {0.0f, 0.0f};
        }
        return p_frames;
    }

    auto *vm = player_->get_vm();
    if (!vm || !player_->is_compiled()) {
        for (int32_t i = 0; i < p_frames; i++) {
            p_buffer[i] = {0.0f, 0.0f};
        }
        return p_frames;
    }

    // Update BPM on audio thread
    vm->set_bpm(player_->get_effective_bpm());

    // Fill ring buffer as needed
    while (ring_available() < static_cast<std::size_t>(p_frames)) {
        vm->process_block(temp_left_, temp_right_);
        for (std::size_t i = 0; i < BLOCK_SIZE; i++) {
            ring_buffer_[ring_write_ & (RING_SIZE - 1)] = {
                temp_left_[i], temp_right_[i]};
            ring_write_++;
        }
    }

    // Copy from ring buffer to output
    for (int32_t i = 0; i < p_frames; i++) {
        p_buffer[i] = ring_buffer_[ring_read_ & (RING_SIZE - 1)];
        ring_read_++;
    }

    return p_frames;
}
