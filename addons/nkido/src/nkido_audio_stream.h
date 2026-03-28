#pragma once

#include <godot_cpp/classes/audio_stream.hpp>
#include <godot_cpp/classes/audio_stream_playback.hpp>

namespace godot {

class NkidoPlayer;

class NkidoAudioStream : public AudioStream {
    GDCLASS(NkidoAudioStream, AudioStream)

public:
    NkidoAudioStream();
    ~NkidoAudioStream();

    void set_player(NkidoPlayer *p_player);
    NkidoPlayer *get_player() const;

    Ref<AudioStreamPlayback> _instantiate_playback() const override;
    double _get_length() const override;
    bool _is_monophonic() const override;

protected:
    static void _bind_methods();

private:
    NkidoPlayer *player_ = nullptr;
};

} // namespace godot
