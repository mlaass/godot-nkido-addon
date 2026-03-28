#pragma once

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/audio_stream_player.hpp>

#include <akkado/akkado.hpp>
#include <cedar/vm/vm.hpp>

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace godot {

class NkidoAudioStream;

class NkidoPlayer : public Node {
    GDCLASS(NkidoPlayer, Node)

public:
    NkidoPlayer();
    ~NkidoPlayer();

    // Properties
    void set_source(const String &p_source);
    String get_source() const;

    void set_bpm(float p_bpm);
    float get_bpm() const;

    void set_crossfade_blocks(int p_blocks);
    int get_crossfade_blocks() const;

    void set_autoplay(bool p_autoplay);
    bool get_autoplay() const;

    void set_bus(const StringName &p_bus);
    StringName get_bus() const;

    // Compilation
    bool compile();
    Array get_diagnostics() const;

    // Playback
    void play();
    void stop();
    void pause();
    bool is_playing() const;

    // Parameters
    void set_param(const String &p_name, float p_value, float p_slew_ms = 20.0f);
    float get_param(const String &p_name) const;
    void trigger_button(const String &p_name);
    Array get_param_decls() const;

    // Internal accessors (for audio stream playback)
    cedar::VM *get_vm() const;
    bool is_compiled() const;
    float get_effective_bpm() const;

    void _ready() override;
    void _process(double p_delta) override;
    void _validate_property(PropertyInfo &p_property) const;

protected:
    static void _bind_methods();

private:
    void apply_state_inits(const std::vector<akkado::StateInitData> &p_inits);
    void ensure_initialized();

    bool initialized_ = false;
    std::unique_ptr<cedar::VM> vm_;
    String source_;
    bool compiled_ = false;
    float bpm_ = 120.0f; // 0 = use global
    int crossfade_blocks_ = 3;
    bool autoplay_ = false;
    StringName bus_ = "Master";

    std::vector<akkado::ParamDecl> param_decls_;
    akkado::CompileResult last_compile_result_;

    AudioStreamPlayer *stream_player_ = nullptr;
    Ref<NkidoAudioStream> audio_stream_;

    // Button trigger tracking: name -> frames remaining
    std::unordered_map<std::string, int> pending_button_releases_;
};

} // namespace godot
