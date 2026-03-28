#include "nkido_audio_stream.h"
#include "nkido_audio_stream_playback.h"
#include "nkido_player.h"

#include <godot_cpp/core/class_db.hpp>

using namespace godot;

NkidoAudioStream::NkidoAudioStream() = default;
NkidoAudioStream::~NkidoAudioStream() = default;

void NkidoAudioStream::_bind_methods() {
}

void NkidoAudioStream::set_player(NkidoPlayer *p_player) {
    player_ = p_player;
}

NkidoPlayer *NkidoAudioStream::get_player() const {
    return player_;
}

Ref<AudioStreamPlayback> NkidoAudioStream::_instantiate_playback() const {
    Ref<NkidoAudioStreamPlayback> playback;
    playback.instantiate();
    playback->set_player(player_);
    return playback;
}

double NkidoAudioStream::_get_length() const {
    return 0.0; // infinite
}

bool NkidoAudioStream::_is_monophonic() const {
    return true;
}
