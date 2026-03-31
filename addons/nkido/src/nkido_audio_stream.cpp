#include "nkido_audio_stream.h"
#include "nkido_audio_stream_playback.h"

#include <godot_cpp/classes/audio_server.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <cedar/io/audio_decoder.hpp>
#include <cedar/io/buffer.hpp>
#include <cedar/opcodes/dsp_state.hpp>
#include <cedar/vm/sample_bank.hpp>
#include <cedar/audio/soundfont.hpp>
#include <akkado/sample_registry.hpp>

using namespace godot;

NkidoAudioStream::NkidoAudioStream() {
    vm_ = std::make_unique<cedar::VM>();

    auto *audio_server = AudioServer::get_singleton();
    if (audio_server) {
        vm_->set_sample_rate(audio_server->get_mix_rate());
    }

    vm_->set_crossfade_blocks(static_cast<std::uint32_t>(crossfade_blocks_));
}

NkidoAudioStream::~NkidoAudioStream() = default;

void NkidoAudioStream::_bind_methods() {
    // Properties
    ClassDB::bind_method(D_METHOD("set_source", "source"), &NkidoAudioStream::set_source);
    ClassDB::bind_method(D_METHOD("get_source"), &NkidoAudioStream::get_source);
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "source", PROPERTY_HINT_MULTILINE_TEXT),
        "set_source", "get_source");

    ClassDB::bind_method(D_METHOD("set_bpm", "bpm"), &NkidoAudioStream::set_bpm);
    ClassDB::bind_method(D_METHOD("get_bpm"), &NkidoAudioStream::get_bpm);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "bpm", PROPERTY_HINT_RANGE, "0,300,0.1"),
        "set_bpm", "get_bpm");

    ClassDB::bind_method(D_METHOD("set_crossfade_blocks", "blocks"),
        &NkidoAudioStream::set_crossfade_blocks);
    ClassDB::bind_method(D_METHOD("get_crossfade_blocks"),
        &NkidoAudioStream::get_crossfade_blocks);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "crossfade_blocks", PROPERTY_HINT_RANGE, "1,10"),
        "set_crossfade_blocks", "get_crossfade_blocks");

    ClassDB::bind_method(D_METHOD("set_source_file", "path"), &NkidoAudioStream::set_source_file);
    ClassDB::bind_method(D_METHOD("get_source_file"), &NkidoAudioStream::get_source_file);
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "source_file", PROPERTY_HINT_FILE, "*.akk"),
        "set_source_file", "get_source_file");

    ClassDB::bind_method(D_METHOD("set_sample_pack", "pack"), &NkidoAudioStream::set_sample_pack);
    ClassDB::bind_method(D_METHOD("get_sample_pack"), &NkidoAudioStream::get_sample_pack);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "sample_pack", PROPERTY_HINT_RESOURCE_TYPE, "Resource"),
        "set_sample_pack", "get_sample_pack");

    // Compilation
    ClassDB::bind_method(D_METHOD("compile"), &NkidoAudioStream::compile);
    ClassDB::bind_method(D_METHOD("get_diagnostics"), &NkidoAudioStream::get_diagnostics);
    ClassDB::bind_method(D_METHOD("is_compiled"), &NkidoAudioStream::is_compiled);

    // Sample loading
    ClassDB::bind_method(D_METHOD("load_sample", "name", "path"), &NkidoAudioStream::load_sample);
    ClassDB::bind_method(D_METHOD("load_soundfont", "name", "path"), &NkidoAudioStream::load_soundfont);
    ClassDB::bind_method(D_METHOD("clear_samples"), &NkidoAudioStream::clear_samples);
    ClassDB::bind_method(D_METHOD("clear_soundfonts"), &NkidoAudioStream::clear_soundfonts);
    ClassDB::bind_method(D_METHOD("get_loaded_samples"), &NkidoAudioStream::get_loaded_samples);
    ClassDB::bind_method(D_METHOD("get_loaded_soundfonts"), &NkidoAudioStream::get_loaded_soundfonts);
    ClassDB::bind_method(D_METHOD("get_required_samples"), &NkidoAudioStream::get_required_samples);

    // Parameters
    ClassDB::bind_method(D_METHOD("set_param", "name", "value", "slew_ms"),
        &NkidoAudioStream::set_param, DEFVAL(20.0f));
    ClassDB::bind_method(D_METHOD("get_param", "name"), &NkidoAudioStream::get_param);
    ClassDB::bind_method(D_METHOD("trigger_button", "name"), &NkidoAudioStream::trigger_button);
    ClassDB::bind_method(D_METHOD("get_param_decls"), &NkidoAudioStream::get_param_decls);

    // Waveform
    ClassDB::bind_method(D_METHOD("get_waveform_data"), &NkidoAudioStream::get_waveform_data);

    // Signals
    ADD_SIGNAL(MethodInfo("compilation_finished",
        PropertyInfo(Variant::BOOL, "success"),
        PropertyInfo(Variant::ARRAY, "errors")));
    ADD_SIGNAL(MethodInfo("params_changed",
        PropertyInfo(Variant::ARRAY, "params")));
}

// --- Properties ---

void NkidoAudioStream::set_source(const String &p_source) {
    source_ = p_source;
}

String NkidoAudioStream::get_source() const {
    return source_;
}

void NkidoAudioStream::set_bpm(float p_bpm) {
    bpm_ = p_bpm;
}

float NkidoAudioStream::get_bpm() const {
    return bpm_;
}

void NkidoAudioStream::set_crossfade_blocks(int p_blocks) {
    crossfade_blocks_ = CLAMP(p_blocks, 1, 10);
    if (vm_) {
        vm_->set_crossfade_blocks(static_cast<std::uint32_t>(crossfade_blocks_));
    }
}

int NkidoAudioStream::get_crossfade_blocks() const {
    return crossfade_blocks_;
}

void NkidoAudioStream::set_source_file(const String &p_path) {
    source_file_ = p_path;
}

String NkidoAudioStream::get_source_file() const {
    return source_file_;
}

void NkidoAudioStream::set_sample_pack(const Ref<Resource> &p_pack) {
    sample_pack_ = p_pack;
}

Ref<Resource> NkidoAudioStream::get_sample_pack() const {
    return sample_pack_;
}

// --- Sample Loading ---

bool NkidoAudioStream::load_sample(const String &p_name, const String &p_path) {
    PackedByteArray bytes = FileAccess::get_file_as_bytes(p_path);
    if (bytes.is_empty()) {
        UtilityFunctions::printerr("NkidoAudioStream: Failed to read file: ", p_path);
        return false;
    }

    std::string name = p_name.utf8().get_data();
    cedar::MemoryView mem{bytes.ptr(), static_cast<std::size_t>(bytes.size())};
    auto id = vm_->sample_bank().load_audio_data(name, mem);
    if (id == 0) {
        UtilityFunctions::printerr("NkidoAudioStream: Failed to decode audio: ", p_path);
        return false;
    }

    return true;
}

bool NkidoAudioStream::load_soundfont(const String &p_name, const String &p_path) {
    PackedByteArray bytes = FileAccess::get_file_as_bytes(p_path);
    if (bytes.is_empty()) {
        UtilityFunctions::printerr("NkidoAudioStream: Failed to read file: ", p_path);
        return false;
    }

    std::string name = p_name.utf8().get_data();
    int result = vm_->soundfont_registry().load_from_memory(
        bytes.ptr(), static_cast<int>(bytes.size()), name, vm_->sample_bank());
    if (result < 0) {
        UtilityFunctions::printerr("NkidoAudioStream: Failed to load SoundFont: ", p_path);
        return false;
    }

    return true;
}

void NkidoAudioStream::clear_samples() {
    vm_->sample_bank().clear();
}

void NkidoAudioStream::clear_soundfonts() {
    // SoundFontRegistry doesn't have a clear method — recreate via new VM isn't practical.
    // For v2, clearing soundfonts is a no-op with a warning.
    UtilityFunctions::print_rich("[color=yellow]NkidoAudioStream: clear_soundfonts() not yet supported[/color]");
}

void NkidoAudioStream::load_samples_from_pack() {
    if (sample_pack_.is_null()) {
        return;
    }

    Dictionary samples = sample_pack_->get("samples");
    Array keys = samples.keys();
    for (int i = 0; i < keys.size(); i++) {
        String name = keys[i];
        String file = samples[name];
        if (name.is_empty() || file.is_empty()) continue;
        load_sample(name, file);
    }

    Dictionary soundfonts = sample_pack_->get("soundfonts");
    Array sf_keys = soundfonts.keys();
    for (int i = 0; i < sf_keys.size(); i++) {
        String sf_name = sf_keys[i];
        String sf_file = soundfonts[sf_name];
        if (sf_name.is_empty() || sf_file.is_empty()) continue;
        load_soundfont(sf_name, sf_file);
    }
}

Array NkidoAudioStream::get_loaded_samples() const {
    Array result;
    const auto &name_to_id = vm_->sample_bank().get_name_to_id();
    for (const auto &[name, id] : name_to_id) {
        Dictionary d;
        d["name"] = String(name.c_str());
        d["id"] = static_cast<int>(id);
        const auto *sample = vm_->sample_bank().get_sample(id);
        if (sample) {
            d["frames"] = static_cast<int>(sample->frames);
            d["channels"] = static_cast<int>(sample->channels);
            d["sample_rate"] = sample->sample_rate;
        }
        result.push_back(d);
    }
    return result;
}

Array NkidoAudioStream::get_loaded_soundfonts() const {
    Array result;
    auto &registry = vm_->soundfont_registry();
    for (std::size_t i = 0; i < registry.size(); ++i) {
        Dictionary d;
        d["id"] = static_cast<int>(i);
        const auto *bank = registry.get(static_cast<int>(i));
        if (bank) {
            d["preset_count"] = static_cast<int>(bank->presets.size());
        }
        result.push_back(d);
    }
    return result;
}

Array NkidoAudioStream::get_required_samples() const {
    Array result;
    for (const auto &rs : last_compile_result_.required_samples_extended) {
        Dictionary d;
        d["name"] = String(rs.name.c_str());
        d["bank"] = String(rs.bank.c_str());
        d["variant"] = rs.variant;
        result.push_back(d);
    }
    return result;
}

// --- Compilation ---

bool NkidoAudioStream::compile() {
    // Determine source code
    std::string code_str;
    std::string filename = "<input>";

    if (!source_file_.is_empty()) {
        // source_file takes priority
        auto file = FileAccess::open(source_file_, FileAccess::READ);
        if (file.is_null()) {
            Array errors;
            Dictionary err;
            err["line"] = 0;
            err["column"] = 0;
            err["message"] = String("File not found: ") + source_file_;
            errors.push_back(err);
            emit_signal("compilation_finished", false, errors);
            return false;
        }
        String content = file->get_as_text();
        code_str = content.utf8().get_data();
        filename = source_file_.utf8().get_data();
    } else if (!source_.is_empty()) {
        code_str = source_.utf8().get_data();
    } else {
        Array errors;
        Dictionary err;
        err["line"] = 0;
        err["column"] = 0;
        err["message"] = "No source code provided";
        errors.push_back(err);
        emit_signal("compilation_finished", false, errors);
        return false;
    }

    // Load samples from pack (before building registry)
    load_samples_from_pack();

    // Build sample registry from loaded samples
    akkado::SampleRegistry sample_registry;
    const auto &name_to_id = vm_->sample_bank().get_name_to_id();
    for (const auto &[name, id] : name_to_id) {
        sample_registry.register_sample(name, id);
    }

    last_compile_result_ = akkado::compile(code_str, filename, &sample_registry);

    Array diagnostics_array;
    for (const auto &diag : last_compile_result_.diagnostics) {
        Dictionary d;
        d["line"] = static_cast<int>(diag.location.line);
        d["column"] = static_cast<int>(diag.location.column);
        d["message"] = String(diag.message.c_str());
        diagnostics_array.push_back(d);
    }

    if (last_compile_result_.success) {
        // Resolve sample IDs in state_inits
        resolve_sample_ids();

        apply_state_inits(last_compile_result_.state_inits);

        auto *insts = reinterpret_cast<const cedar::Instruction *>(
            last_compile_result_.bytecode.data());
        std::size_t count =
            last_compile_result_.bytecode.size() / sizeof(cedar::Instruction);
        auto load_result = vm_->load_program(std::span{insts, count});
        if (load_result != cedar::VM::LoadResult::Success) {
            compiled_ = false;
            Array err_array;
            Dictionary err;
            err["line"] = 0;
            err["column"] = 0;
            err["message"] = "Failed to load program into VM";
            err_array.push_back(err);
            emit_signal("compilation_finished", false, err_array);
            return false;
        }

        param_decls_ = last_compile_result_.param_decls;

        for (const auto &p : param_decls_) {
            vm_->set_param(p.name.c_str(), p.default_value);
        }

        compiled_ = true;
    }

    emit_signal("compilation_finished", last_compile_result_.success, diagnostics_array);
    if (last_compile_result_.success) {
        emit_signal("params_changed", get_param_decls());
    }

    return last_compile_result_.success;
}

void NkidoAudioStream::resolve_sample_ids() {
    for (auto &init : last_compile_result_.state_inits) {
        for (const auto &mapping : init.sequence_sample_mappings) {
            if (mapping.seq_idx < init.sequence_events.size()) {
                auto &events = init.sequence_events[mapping.seq_idx];
                if (mapping.event_idx < events.size()) {
                    std::string lookup_name;
                    if (mapping.bank.empty() || mapping.bank == "default") {
                        lookup_name = mapping.variant > 0
                            ? mapping.sample_name + ":" + std::to_string(mapping.variant)
                            : mapping.sample_name;
                    } else {
                        lookup_name = mapping.bank + "_" + mapping.sample_name +
                            "_" + std::to_string(mapping.variant);
                    }
                    auto id = vm_->sample_bank().get_sample_id(lookup_name);
                    events[mapping.event_idx].values[0] = static_cast<float>(id);
                }
            }
        }
    }
}

void NkidoAudioStream::apply_state_inits(
    const std::vector<akkado::StateInitData> &p_inits) {
    for (const auto &init : p_inits) {
        if (init.type == akkado::StateInitData::Type::SequenceProgram) {
            std::vector<cedar::Sequence> seq_copy = init.sequences;
            for (std::size_t i = 0;
                 i < seq_copy.size() && i < init.sequence_events.size(); ++i) {
                if (!init.sequence_events[i].empty()) {
                    seq_copy[i].events =
                        const_cast<cedar::Event *>(init.sequence_events[i].data());
                    seq_copy[i].num_events =
                        static_cast<std::uint32_t>(init.sequence_events[i].size());
                    seq_copy[i].capacity =
                        static_cast<std::uint32_t>(init.sequence_events[i].size());
                }
            }
            vm_->init_sequence_program_state(init.state_id, seq_copy.data(),
                seq_copy.size(), init.cycle_length, init.is_sample_pattern,
                init.total_events);
        } else if (init.type == akkado::StateInitData::Type::PolyAlloc) {
            vm_->init_poly_state(init.state_id, init.poly_seq_state_id,
                init.poly_max_voices, init.poly_mode, init.poly_steal_strategy);
        } else if (init.type == akkado::StateInitData::Type::Timeline) {
            auto &state =
                vm_->states().get_or_create<cedar::TimelineState>(init.state_id);
            state.num_points = std::min(
                static_cast<std::uint32_t>(init.timeline_breakpoints.size()),
                static_cast<std::uint32_t>(cedar::TimelineState::MAX_BREAKPOINTS));
            for (std::uint32_t i = 0; i < state.num_points; ++i) {
                state.points[i] = init.timeline_breakpoints[i];
            }
            state.loop = init.timeline_loop;
            state.loop_length = init.timeline_loop_length;
        }
    }
}

Array NkidoAudioStream::get_diagnostics() const {
    Array result;
    for (const auto &diag : last_compile_result_.diagnostics) {
        Dictionary d;
        d["line"] = static_cast<int>(diag.location.line);
        d["column"] = static_cast<int>(diag.location.column);
        d["message"] = String(diag.message.c_str());
        result.push_back(d);
    }
    return result;
}

bool NkidoAudioStream::is_compiled() const {
    return compiled_;
}

// --- Parameters ---

void NkidoAudioStream::set_param(const String &p_name, float p_value, float p_slew_ms) {
    if (!vm_) {
        return;
    }
    std::string name = p_name.utf8().get_data();
    vm_->set_param(name.c_str(), p_value, p_slew_ms);
}

float NkidoAudioStream::get_param(const String &p_name) const {
    std::string name = p_name.utf8().get_data();
    for (const auto &p : param_decls_) {
        if (p.name == name) {
            return p.default_value;
        }
    }
    return 0.0f;
}

void NkidoAudioStream::trigger_button(const String &p_name) {
    if (!vm_) {
        return;
    }
    std::string name = p_name.utf8().get_data();
    vm_->set_param(name.c_str(), 1.0f, 0.0f);
    pending_button_releases_[name] = 2;
}

Array NkidoAudioStream::get_param_decls() const {
    Array result;
    for (const auto &p : param_decls_) {
        Dictionary d;
        d["name"] = String(p.name.c_str());
        switch (p.type) {
            case akkado::ParamType::Continuous:
                d["type"] = "continuous";
                break;
            case akkado::ParamType::Button:
                d["type"] = "button";
                break;
            case akkado::ParamType::Toggle:
                d["type"] = "toggle";
                break;
            case akkado::ParamType::Select:
                d["type"] = "select";
                break;
        }
        d["default"] = p.default_value;
        d["min"] = p.min_value;
        d["max"] = p.max_value;
        Array options;
        for (const auto &opt : p.options) {
            options.push_back(String(opt.c_str()));
        }
        d["options"] = options;
        result.push_back(d);
    }
    return result;
}

// --- Waveform ---

PackedFloat32Array NkidoAudioStream::get_waveform_data() const {
    if (active_playback_) {
        return active_playback_->get_waveform_data();
    }
    return PackedFloat32Array();
}

// --- Button release processing (called from audio thread) ---

void NkidoAudioStream::process_button_releases() {
    auto it = pending_button_releases_.begin();
    while (it != pending_button_releases_.end()) {
        it->second--;
        if (it->second <= 0) {
            vm_->set_param(it->first.c_str(), 0.0f, 0.0f);
            it = pending_button_releases_.erase(it);
        } else {
            ++it;
        }
    }
}

// --- Internal ---

cedar::VM *NkidoAudioStream::get_vm() const {
    return vm_.get();
}

// --- AudioStream overrides ---

Ref<AudioStreamPlayback> NkidoAudioStream::_instantiate_playback() const {
    Ref<NkidoAudioStreamPlayback> playback;
    playback.instantiate();
    playback->set_stream(Ref<NkidoAudioStream>(const_cast<NkidoAudioStream *>(this)));
    const_cast<NkidoAudioStream *>(this)->active_playback_ = playback.ptr();
    return playback;
}

double NkidoAudioStream::_get_length() const {
    return 0.0; // infinite
}

bool NkidoAudioStream::_is_monophonic() const {
    return true;
}
