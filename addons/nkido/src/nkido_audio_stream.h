#pragma once

#include <godot_cpp/classes/audio_stream.hpp>
#include <godot_cpp/classes/audio_stream_playback.hpp>
#include <godot_cpp/classes/resource.hpp>

#include "nkido_akkado_source.h"

#include <akkado/akkado.hpp>
#include <cedar/vm/vm.hpp>

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace godot {

class NkidoAudioStreamPlayback;

class NkidoAudioStream : public AudioStream {
    GDCLASS(NkidoAudioStream, AudioStream)

public:
    NkidoAudioStream();
    ~NkidoAudioStream();

    // Properties
    void set_akkado_source(const Ref<NkidoAkkadoSource> &p_source);
    Ref<NkidoAkkadoSource> get_akkado_source() const;

    void set_bpm(float p_bpm);
    float get_bpm() const;

    void set_crossfade_blocks(int p_blocks);
    int get_crossfade_blocks() const;

    // Compilation
    bool compile();
    Array get_diagnostics() const;
    bool is_compiled() const;

    // Sample pack
    void set_sample_pack(const Ref<Resource> &p_pack);
    Ref<Resource> get_sample_pack() const;

    // Sample loading
    bool load_sample(const String &p_name, const String &p_path);
    bool load_soundfont(const String &p_name, const String &p_path);
    void clear_samples();
    void clear_soundfonts();
    Array get_loaded_samples() const;
    Array get_loaded_soundfonts() const;
    Array get_required_samples() const;

    // Parameters
    void set_param(const String &p_name, float p_value, float p_slew_ms = 20.0f);
    float get_param(const String &p_name) const;
    void trigger_button(const String &p_name);
    Array get_param_decls() const;

    // Waveform visualization
    PackedFloat32Array get_waveform_data() const;

    // Internal (for playback)
    cedar::VM *get_vm() const;

    // AudioStream overrides
    Ref<AudioStreamPlayback> _instantiate_playback() const override;
    double _get_length() const override;
    bool _is_monophonic() const override;

    // Button release processing (called from audio thread)
    void process_button_releases();

protected:
    static void _bind_methods();

private:
    void apply_state_inits(const std::vector<akkado::StateInitData> &p_inits);

    void resolve_sample_ids();

    void load_samples_from_pack();

    void _on_source_changed();

    std::unique_ptr<cedar::VM> vm_;
    Ref<NkidoAkkadoSource> akkado_source_;
    bool compiled_ = false;
    float bpm_ = 120.0f;
    int crossfade_blocks_ = 3;

    Ref<Resource> sample_pack_;

    mutable NkidoAudioStreamPlayback *active_playback_ = nullptr;

    std::vector<akkado::ParamDecl> param_decls_;
    akkado::CompileResult last_compile_result_;

    // Button trigger tracking: name -> blocks remaining
    std::unordered_map<std::string, int> pending_button_releases_;
};

} // namespace godot
