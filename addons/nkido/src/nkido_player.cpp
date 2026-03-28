#include "nkido_player.h"
#include "nkido_engine.h"
#include "nkido_audio_stream.h"

#include <godot_cpp/classes/audio_server.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <cedar/opcodes/dsp_state.hpp>

using namespace godot;

NkidoPlayer::NkidoPlayer() {
    vm_ = std::make_unique<cedar::VM>();
}

NkidoPlayer::~NkidoPlayer() = default;

void NkidoPlayer::_bind_methods() {
    // Properties
    ClassDB::bind_method(D_METHOD("set_source", "source"), &NkidoPlayer::set_source);
    ClassDB::bind_method(D_METHOD("get_source"), &NkidoPlayer::get_source);
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "source", PROPERTY_HINT_MULTILINE_TEXT),
        "set_source", "get_source");

    ClassDB::bind_method(D_METHOD("set_bpm", "bpm"), &NkidoPlayer::set_bpm);
    ClassDB::bind_method(D_METHOD("get_bpm"), &NkidoPlayer::get_bpm);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "bpm", PROPERTY_HINT_RANGE, "0,300,0.1"),
        "set_bpm", "get_bpm");

    ClassDB::bind_method(D_METHOD("set_crossfade_blocks", "blocks"),
        &NkidoPlayer::set_crossfade_blocks);
    ClassDB::bind_method(D_METHOD("get_crossfade_blocks"),
        &NkidoPlayer::get_crossfade_blocks);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "crossfade_blocks", PROPERTY_HINT_RANGE, "1,10"),
        "set_crossfade_blocks", "get_crossfade_blocks");

    ClassDB::bind_method(D_METHOD("set_autoplay", "autoplay"), &NkidoPlayer::set_autoplay);
    ClassDB::bind_method(D_METHOD("get_autoplay"), &NkidoPlayer::get_autoplay);
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "autoplay"), "set_autoplay", "get_autoplay");

    ClassDB::bind_method(D_METHOD("set_bus", "bus"), &NkidoPlayer::set_bus);
    ClassDB::bind_method(D_METHOD("get_bus"), &NkidoPlayer::get_bus);
    ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "bus", PROPERTY_HINT_ENUM, ""),
        "set_bus", "get_bus");

    // Methods
    ClassDB::bind_method(D_METHOD("compile"), &NkidoPlayer::compile);
    ClassDB::bind_method(D_METHOD("get_diagnostics"), &NkidoPlayer::get_diagnostics);

    ClassDB::bind_method(D_METHOD("play"), &NkidoPlayer::play);
    ClassDB::bind_method(D_METHOD("stop"), &NkidoPlayer::stop);
    ClassDB::bind_method(D_METHOD("pause"), &NkidoPlayer::pause);
    ClassDB::bind_method(D_METHOD("is_playing"), &NkidoPlayer::is_playing);

    ClassDB::bind_method(D_METHOD("set_param", "name", "value", "slew_ms"),
        &NkidoPlayer::set_param, DEFVAL(20.0f));
    ClassDB::bind_method(D_METHOD("get_param", "name"), &NkidoPlayer::get_param);
    ClassDB::bind_method(D_METHOD("trigger_button", "name"), &NkidoPlayer::trigger_button);
    ClassDB::bind_method(D_METHOD("get_param_decls"), &NkidoPlayer::get_param_decls);

    // Signals
    ADD_SIGNAL(MethodInfo("compilation_finished",
        PropertyInfo(Variant::BOOL, "success"),
        PropertyInfo(Variant::ARRAY, "errors")));
    ADD_SIGNAL(MethodInfo("params_changed",
        PropertyInfo(Variant::ARRAY, "params")));
    ADD_SIGNAL(MethodInfo("playback_started"));
    ADD_SIGNAL(MethodInfo("playback_stopped"));
}

// --- Properties ---

void NkidoPlayer::set_source(const String &p_source) {
    source_ = p_source;
}

String NkidoPlayer::get_source() const {
    return source_;
}

void NkidoPlayer::set_bpm(float p_bpm) {
    bpm_ = p_bpm;
}

float NkidoPlayer::get_bpm() const {
    return bpm_;
}

void NkidoPlayer::set_crossfade_blocks(int p_blocks) {
    crossfade_blocks_ = CLAMP(p_blocks, 1, 10);
    if (vm_) {
        vm_->set_crossfade_blocks(static_cast<std::uint32_t>(crossfade_blocks_));
    }
}

int NkidoPlayer::get_crossfade_blocks() const {
    return crossfade_blocks_;
}

void NkidoPlayer::set_autoplay(bool p_autoplay) {
    autoplay_ = p_autoplay;
}

bool NkidoPlayer::get_autoplay() const {
    return autoplay_;
}

void NkidoPlayer::set_bus(const StringName &p_bus) {
    bus_ = p_bus;
    if (stream_player_) {
        stream_player_->set_bus(bus_);
    }
}

StringName NkidoPlayer::get_bus() const {
    return bus_;
}

void NkidoPlayer::_validate_property(PropertyInfo &p_property) const {
    if (p_property.name == StringName("bus")) {
        String buses;
        auto *audio_server = AudioServer::get_singleton();
        if (audio_server) {
            for (int i = 0; i < audio_server->get_bus_count(); i++) {
                if (!buses.is_empty()) {
                    buses += ",";
                }
                buses += audio_server->get_bus_name(i);
            }
        }
        p_property.hint = PROPERTY_HINT_ENUM;
        p_property.hint_string = buses;
    }
}

// --- Lifecycle ---

void NkidoPlayer::ensure_initialized() {
    if (initialized_) {
        return;
    }
    if (!is_inside_tree()) {
        return;
    }
    initialized_ = true;

    stream_player_ = memnew(AudioStreamPlayer);
    add_child(stream_player_);
    stream_player_->set_owner(nullptr); // don't serialize

    audio_stream_.instantiate();
    audio_stream_->set_player(this);
    stream_player_->set_stream(audio_stream_);
    stream_player_->set_bus(bus_);

    // Set sample rate from NkidoEngine singleton
    auto *engine_obj = Engine::get_singleton()->get_singleton("NkidoEngine");
    if (engine_obj) {
        auto *nkido_engine = Object::cast_to<NkidoEngine>(engine_obj);
        if (nkido_engine) {
            vm_->set_sample_rate(nkido_engine->get_sample_rate());
        }
    }

    vm_->set_crossfade_blocks(static_cast<std::uint32_t>(crossfade_blocks_));
}

void NkidoPlayer::_ready() {
    ensure_initialized();

    if (autoplay_ && !source_.is_empty()) {
        if (compile()) {
            play();
        }
    }
}

void NkidoPlayer::_process(double p_delta) {
    // Handle button trigger releases
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

// --- Compilation ---

bool NkidoPlayer::compile() {
    ensure_initialized();

    if (source_.is_empty()) {
        Array errors;
        Dictionary err;
        err["line"] = 0;
        err["column"] = 0;
        err["message"] = "No source code provided";
        errors.push_back(err);
        emit_signal("compilation_finished", false, errors);
        return false;
    }

    std::string code_str = source_.utf8().get_data();
    last_compile_result_ = akkado::compile(code_str);

    Array diagnostics_array;
    for (const auto &diag : last_compile_result_.diagnostics) {
        Dictionary d;
        d["line"] = static_cast<int>(diag.location.line);
        d["column"] = static_cast<int>(diag.location.column);
        d["message"] = String(diag.message.c_str());
        diagnostics_array.push_back(d);
    }

    if (last_compile_result_.success) {
        // Apply state initializations before loading program
        apply_state_inits(last_compile_result_.state_inits);

        // Load bytecode (queues hot-swap for next block boundary)
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

        // Store param declarations
        param_decls_ = last_compile_result_.param_decls;

        // Initialize params with defaults
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

void NkidoPlayer::apply_state_inits(
    const std::vector<akkado::StateInitData> &p_inits) {
    for (const auto &init : p_inits) {
        if (init.type == akkado::StateInitData::Type::SequenceProgram) {
            // Set up sequence event pointers before passing to VM
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

Array NkidoPlayer::get_diagnostics() const {
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

// --- Playback ---

void NkidoPlayer::play() {
    ensure_initialized();
    if (!compiled_ || !stream_player_) {
        return;
    }
    stream_player_->play();
    emit_signal("playback_started");
}

void NkidoPlayer::stop() {
    if (!stream_player_) {
        return;
    }
    stream_player_->stop();
    emit_signal("playback_stopped");
}

void NkidoPlayer::pause() {
    if (!stream_player_) {
        return;
    }
    stream_player_->set_stream_paused(!stream_player_->get_stream_paused());
}

bool NkidoPlayer::is_playing() const {
    if (!stream_player_) {
        return false;
    }
    return stream_player_->is_playing();
}

// --- Parameters ---

void NkidoPlayer::set_param(const String &p_name, float p_value, float p_slew_ms) {
    if (!vm_) {
        return;
    }
    std::string name = p_name.utf8().get_data();
    vm_->set_param(name.c_str(), p_value, p_slew_ms);
}

float NkidoPlayer::get_param(const String &p_name) const {
    // EnvMap doesn't have a get_value; return the declared default
    std::string name = p_name.utf8().get_data();
    for (const auto &p : param_decls_) {
        if (p.name == name) {
            return p.default_value;
        }
    }
    return 0.0f;
}

void NkidoPlayer::trigger_button(const String &p_name) {
    if (!vm_) {
        return;
    }
    std::string name = p_name.utf8().get_data();
    vm_->set_param(name.c_str(), 1.0f, 0.0f);
    pending_button_releases_[name] = 2;
}

Array NkidoPlayer::get_param_decls() const {
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

// --- Internal ---

cedar::VM *NkidoPlayer::get_vm() const {
    return vm_.get();
}

bool NkidoPlayer::is_compiled() const {
    return compiled_;
}

float NkidoPlayer::get_effective_bpm() const {
    if (bpm_ > 0.0f) {
        return bpm_;
    }
    auto *engine_obj = Engine::get_singleton()->get_singleton("NkidoEngine");
    if (engine_obj) {
        auto *nkido_engine = Object::cast_to<NkidoEngine>(engine_obj);
        if (nkido_engine) {
            return nkido_engine->get_bpm();
        }
    }
    return 120.0f;
}
