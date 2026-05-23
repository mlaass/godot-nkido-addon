#include "nkido_audio_stream.h"
#include "nkido_audio_stream_playback.h"

#include <godot_cpp/classes/audio_server.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <cedar/io/audio_decoder.hpp>
#include <cedar/io/buffer.hpp>
#include <cedar/opcodes/dsp_state.hpp>
#include <cedar/opcodes/midi.hpp>
#include <cedar/vm/instruction.hpp>
#include <cedar/vm/sample_bank.hpp>
#include <cedar/audio/soundfont.hpp>
#include <cedar/wavetable/registry.hpp>
#include <akkado/sample_registry.hpp>

#include <algorithm>

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
    ClassDB::bind_method(D_METHOD("set_akkado_source", "source"),
        &NkidoAudioStream::set_akkado_source);
    ClassDB::bind_method(D_METHOD("get_akkado_source"),
        &NkidoAudioStream::get_akkado_source);
    ADD_PROPERTY(
        PropertyInfo(Variant::OBJECT, "akkado_source",
            PROPERTY_HINT_RESOURCE_TYPE, "NkidoAkkadoSource"),
        "set_akkado_source", "get_akkado_source");

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

    ClassDB::bind_method(D_METHOD("set_sample_pack", "pack"), &NkidoAudioStream::set_sample_pack);
    ClassDB::bind_method(D_METHOD("get_sample_pack"), &NkidoAudioStream::get_sample_pack);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "sample_pack", PROPERTY_HINT_RESOURCE_TYPE, "Resource"),
        "set_sample_pack", "get_sample_pack");

    // Compilation
    ClassDB::bind_method(D_METHOD("compile"), &NkidoAudioStream::compile);
    ClassDB::bind_method(D_METHOD("get_diagnostics"), &NkidoAudioStream::get_diagnostics);
    ClassDB::bind_method(D_METHOD("is_compiled"), &NkidoAudioStream::is_compiled);

    // Sample / asset loading
    ClassDB::bind_method(D_METHOD("load_sample", "name", "path"), &NkidoAudioStream::load_sample);
    ClassDB::bind_method(D_METHOD("load_soundfont", "name", "path"), &NkidoAudioStream::load_soundfont);
    ClassDB::bind_method(D_METHOD("load_wavetable", "name", "path"), &NkidoAudioStream::load_wavetable);
    ClassDB::bind_method(D_METHOD("clear_samples"), &NkidoAudioStream::clear_samples);
    ClassDB::bind_method(D_METHOD("clear_soundfonts"), &NkidoAudioStream::clear_soundfonts);
    ClassDB::bind_method(D_METHOD("get_loaded_samples"), &NkidoAudioStream::get_loaded_samples);
    ClassDB::bind_method(D_METHOD("get_loaded_soundfonts"), &NkidoAudioStream::get_loaded_soundfonts);
    ClassDB::bind_method(D_METHOD("get_required_samples"), &NkidoAudioStream::get_required_samples);
    ClassDB::bind_method(D_METHOD("get_required_soundfonts"), &NkidoAudioStream::get_required_soundfonts);
    ClassDB::bind_method(D_METHOD("get_required_wavetables"), &NkidoAudioStream::get_required_wavetables);
    ClassDB::bind_method(D_METHOD("get_required_uris"), &NkidoAudioStream::get_required_uris);
    ClassDB::bind_method(D_METHOD("get_required_midi_sources"), &NkidoAudioStream::get_required_midi_sources);
    ClassDB::bind_method(D_METHOD("get_midi_cc_routes"), &NkidoAudioStream::get_midi_cc_routes);
    ClassDB::bind_method(D_METHOD("get_viz_decls"), &NkidoAudioStream::get_viz_decls);

    // Parameters
    ClassDB::bind_method(D_METHOD("set_param", "name", "value", "slew_ms"),
        &NkidoAudioStream::set_param, DEFVAL(20.0f));
    ClassDB::bind_method(D_METHOD("get_param", "name"), &NkidoAudioStream::get_param);
    ClassDB::bind_method(D_METHOD("trigger_button", "name"), &NkidoAudioStream::trigger_button);
    ClassDB::bind_method(D_METHOD("get_param_decls"), &NkidoAudioStream::get_param_decls);

    // Waveform
    ClassDB::bind_method(D_METHOD("get_waveform_data"), &NkidoAudioStream::get_waveform_data);

    // Audio input
    ClassDB::bind_method(D_METHOD("set_input_buffers", "left", "right"),
        &NkidoAudioStream::set_input_buffers);

    // Signals
    ADD_SIGNAL(MethodInfo("compilation_finished",
        PropertyInfo(Variant::BOOL, "success"),
        PropertyInfo(Variant::ARRAY, "errors")));
    ADD_SIGNAL(MethodInfo("params_changed",
        PropertyInfo(Variant::ARRAY, "params")));
}

// --- Properties ---

void NkidoAudioStream::set_akkado_source(const Ref<NkidoAkkadoSource> &p_source) {
    if (akkado_source_.is_valid()) {
        akkado_source_->disconnect("changed", callable_mp(this, &NkidoAudioStream::_on_source_changed));
    }
    akkado_source_ = p_source;
    if (akkado_source_.is_valid()) {
        akkado_source_->connect("changed", callable_mp(this, &NkidoAudioStream::_on_source_changed));
    }
}

Ref<NkidoAkkadoSource> NkidoAudioStream::get_akkado_source() const {
    return akkado_source_;
}

void NkidoAudioStream::_on_source_changed() {
    emit_changed();
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
    cedar::MemoryView mem{bytes.ptr(), static_cast<std::size_t>(bytes.size())};
    int result = vm_->soundfont_registry().load_from_memory(
        mem, name, vm_->sample_bank());
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

Array NkidoAudioStream::get_required_soundfonts() const {
    Array result;
    for (const auto &sf : last_compile_result_.required_soundfonts) {
        Dictionary d;
        d["filename"] = String(sf.filename.c_str());
        d["preset_index"] = sf.preset_index;
        result.push_back(d);
    }
    return result;
}

Array NkidoAudioStream::get_required_wavetables() const {
    Array result;
    for (const auto &wt : last_compile_result_.required_wavetables) {
        Dictionary d;
        d["name"] = String(wt.name.c_str());
        d["path"] = String(wt.path.c_str());
        d["id"] = wt.id;
        result.push_back(d);
    }
    return result;
}

Array NkidoAudioStream::get_required_uris() const {
    Array result;
    for (const auto &u : last_compile_result_.required_uris) {
        Dictionary d;
        d["uri"] = String(u.uri.c_str());
        switch (u.kind) {
            case akkado::UriKind::SampleBank: d["kind"] = "sample_bank"; break;
            case akkado::UriKind::SoundFont:  d["kind"] = "soundfont";   break;
            case akkado::UriKind::Wavetable:  d["kind"] = "wavetable";   break;
            case akkado::UriKind::Sample:     d["kind"] = "sample";      break;
        }
        result.push_back(d);
    }
    return result;
}

Array NkidoAudioStream::get_required_midi_sources() const {
    Array result;
    for (const auto &m : last_compile_result_.required_midi_sources) {
        Dictionary d;
        d["state_id"] = static_cast<int64_t>(m.state_id);
        d["kind"] = static_cast<int>(m.kind);
        d["name_or_path"] = String(m.name_or_path.c_str());
        d["channel_filter"] = m.channel_filter;
        d["loop"] = m.loop;
        d["tempo_mode"] = static_cast<int>(m.tempo_mode);
        result.push_back(d);
    }
    return result;
}

Array NkidoAudioStream::get_midi_cc_routes() const {
    Array result;
    for (const auto &r : last_compile_result_.required_midi_cc_routes) {
        Dictionary d;
        d["param_name"] = String(r.param_name.c_str());
        d["cc_num"] = r.cc_num;
        d["channel_filter"] = r.channel_filter;
        d["scale"] = r.scale;
        d["bias"] = r.bias;
        d["slew_ms"] = r.slew_ms;
        result.push_back(d);
    }
    return result;
}

Array NkidoAudioStream::get_viz_decls() const {
    Array result;
    for (const auto &v : last_compile_result_.viz_decls) {
        Dictionary d;
        d["name"] = String(v.name.c_str());
        switch (v.type) {
            case akkado::VisualizationType::PianoRoll:    d["type"] = "piano_roll";   break;
            case akkado::VisualizationType::Oscilloscope: d["type"] = "oscilloscope"; break;
            case akkado::VisualizationType::Waveform:     d["type"] = "waveform";     break;
            case akkado::VisualizationType::Spectrum:     d["type"] = "spectrum";     break;
            case akkado::VisualizationType::Waterfall:    d["type"] = "waterfall";    break;
        }
        d["state_id"] = static_cast<int64_t>(v.state_id);
        d["options_json"] = String(v.options_json.c_str());
        d["source_offset"] = static_cast<int64_t>(v.source_offset);
        d["source_length"] = static_cast<int64_t>(v.source_length);
        d["pattern_state_init_index"] = v.pattern_state_init_index;
        result.push_back(d);
    }
    return result;
}

// --- Asset auto-loading ---

namespace {

// Convert a URI to a path FileAccess can read. Returns empty String for
// unsupported (network) schemes. file:// is stripped; res:// / user:// /
// bare paths pass through.
String uri_to_godot_path(const std::string &uri) {
    static constexpr const char *kFile = "file://";
    static constexpr const char *kRes = "res://";
    static constexpr const char *kUser = "user://";
    if (uri.rfind(kFile, 0) == 0) {
        return String(uri.substr(7).c_str());
    }
    if (uri.rfind("http://", 0) == 0 || uri.rfind("https://", 0) == 0 ||
        uri.rfind("github:", 0) == 0 || uri.rfind("bundled://", 0) == 0 ||
        uri.rfind("blob:", 0) == 0) {
        return String();
    }
    return String(uri.c_str());
}

// Derive a short asset name from a URI/path (last path component, no ext).
std::string name_from_uri(const std::string &uri) {
    auto slash = uri.find_last_of("/\\");
    std::string base = (slash == std::string::npos) ? uri : uri.substr(slash + 1);
    auto dot = base.find_last_of('.');
    if (dot != std::string::npos) base = base.substr(0, dot);
    return base;
}

} // namespace

void NkidoAudioStream::auto_load_required_assets(const akkado::CompileResult &p_result) {
    // 1. URI-declared assets (samples()/soundfont URIs/etc.)
    for (const auto &req : p_result.required_uris) {
        String path = uri_to_godot_path(req.uri);
        if (path.is_empty()) {
            UtilityFunctions::print_rich(
                String("[color=yellow]NkidoAudioStream: skipping network URI ") +
                String(req.uri.c_str()) +
                String(" (only file://, res://, user:// supported)[/color]"));
            continue;
        }
        PackedByteArray bytes = FileAccess::get_file_as_bytes(path);
        if (bytes.is_empty()) {
            UtilityFunctions::printerr(
                "NkidoAudioStream: failed to read URI ", String(req.uri.c_str()));
            continue;
        }
        std::string nm = name_from_uri(req.uri);
        cedar::MemoryView mem{bytes.ptr(), static_cast<std::size_t>(bytes.size())};
        switch (req.kind) {
            case akkado::UriKind::Sample:
                vm_->sample_bank().load_audio_data(nm, mem);
                break;
            case akkado::UriKind::SoundFont:
                vm_->soundfont_registry().load_from_memory(
                    mem, nm, vm_->sample_bank());
                break;
            case akkado::UriKind::Wavetable:
                vm_->wavetable_registry().load_from_memory(nm, mem, nullptr);
                break;
            case akkado::UriKind::SampleBank:
                UtilityFunctions::print_rich(
                    String("[color=yellow]NkidoAudioStream: sample-bank manifest URIs "
                            "are not auto-resolved yet (") +
                    String(req.uri.c_str()) +
                    String("); use load_sample() per entry[/color]"));
                break;
        }
    }

    // 2. SoundFonts declared by literal filename via soundfont("name.sf2", …).
    for (const auto &sf : p_result.required_soundfonts) {
        load_soundfont(String(sf.filename.c_str()), String(sf.filename.c_str()));
    }

    // 3. Wavetables declared by wt_load("name", "path").
    for (const auto &wt : p_result.required_wavetables) {
        load_wavetable(String(wt.name.c_str()), String(wt.path.c_str()));
    }

    // 4. Legacy fallback: sample_pack manifest. Loads any names the
    // pack defines that aren't already in the SampleBank.
    load_samples_from_pack();
}

void NkidoAudioStream::init_midi_sources(const akkado::CompileResult &p_result) {
    for (const auto &m : p_result.required_midi_sources) {
        vm_->init_midi_queue_state(m.state_id, m.kind,
            m.name_or_path.c_str(), m.channel_filter, m.loop, m.tempo_mode);
    }
}

bool NkidoAudioStream::load_wavetable(const String &p_name, const String &p_path) {
    PackedByteArray bytes = FileAccess::get_file_as_bytes(p_path);
    if (bytes.is_empty()) {
        UtilityFunctions::printerr("NkidoAudioStream: Failed to read file: ", p_path);
        return false;
    }
    std::string name = p_name.utf8().get_data();
    cedar::MemoryView mem{bytes.ptr(), static_cast<std::size_t>(bytes.size())};
    int id = vm_->wavetable_registry().load_from_memory(name, mem, nullptr);
    if (id < 0) {
        UtilityFunctions::printerr("NkidoAudioStream: Failed to load wavetable: ", p_path);
        return false;
    }
    return true;
}

void NkidoAudioStream::set_input_buffers(const PackedFloat32Array &p_left,
                                          const PackedFloat32Array &p_right) {
    input_left_.assign(p_left.ptr(), p_left.ptr() + p_left.size());
    input_right_.assign(p_right.ptr(), p_right.ptr() + p_right.size());
    if (vm_) {
        vm_->set_input_buffers(
            input_left_.empty() ? nullptr : input_left_.data(),
            input_right_.empty() ? nullptr : input_right_.data());
    }
}

// --- Compilation ---

bool NkidoAudioStream::compile() {
    // Determine source code from akkado_source resource
    if (akkado_source_.is_null()) {
        Array errors;
        Dictionary err;
        err["line"] = 0;
        err["column"] = 0;
        err["message"] = "No Akkado source assigned";
        errors.push_back(err);
        emit_signal("compilation_finished", false, errors);
        return false;
    }

    String source_text = akkado_source_->get_source_code();
    if (source_text.is_empty()) {
        Array errors;
        Dictionary err;
        err["line"] = 0;
        err["column"] = 0;
        err["message"] = "Empty source";
        errors.push_back(err);
        emit_signal("compilation_finished", false, errors);
        return false;
    }

    std::string code_str = source_text.utf8().get_data();
    std::string filename = "<input>";
    String res_path = akkado_source_->get_path();
    if (!res_path.is_empty()) {
        filename = res_path.utf8().get_data();
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
        // Fetch any URI/soundfont/wavetable assets the program referenced
        // before resolving sample IDs (so the SampleBank is populated).
        auto_load_required_assets(last_compile_result_);

        // Resolve sample IDs in state_inits + scalar mappings
        resolve_sample_ids();

        apply_state_inits(last_compile_result_.state_inits);

        // Set up MIDI sources declared by midi() calls
        init_midi_sources(last_compile_result_);

        auto *insts = reinterpret_cast<const cedar::Instruction *>(
            last_compile_result_.bytecode.data());
        std::size_t total_instructions =
            last_compile_result_.bytecode.size() / sizeof(cedar::Instruction);
        auto load_result = vm_->load_program_with_blocks(
            std::span{insts, total_instructions},
            std::span{last_compile_result_.block_table.data(),
                      last_compile_result_.block_table.size()},
            last_compile_result_.main_instruction_count);
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

std::string NkidoAudioStream::build_sample_lookup_name(const std::string &bank,
                                                        const std::string &name,
                                                        int variant) {
    if (bank.empty() || bank == "default") {
        return variant > 0 ? name + ":" + std::to_string(variant) : name;
    }
    return bank + "_" + name + "_" + std::to_string(variant);
}

void NkidoAudioStream::resolve_sample_ids() {
    // Pattern-event sample resolutions (sequence_sample_mappings).
    for (auto &init : last_compile_result_.state_inits) {
        for (const auto &mapping : init.sequence_sample_mappings) {
            if (mapping.seq_idx >= init.sequence_events.size()) continue;
            auto &events = init.sequence_events[mapping.seq_idx];
            if (mapping.event_idx >= events.size()) continue;
            std::string lookup = build_sample_lookup_name(
                mapping.bank, mapping.sample_name, mapping.variant);
            auto id = vm_->sample_bank().get_sample_id(lookup);
            std::uint8_t slot = mapping.value_slot;
            if (slot >= sizeof(events[mapping.event_idx].values) /
                            sizeof(events[mapping.event_idx].values[0])) {
                slot = 0;
            }
            events[mapping.event_idx].values[slot] = static_cast<float>(id);
        }
    }

    // Scalar sample("name") refs — patch the PUSH_CONST instruction's
    // state_id immediate (which carries the constant value) with the
    // resolved sample ID. See akkado/required_sample.hpp ScalarSampleMapping.
    auto *insts = reinterpret_cast<cedar::Instruction *>(
        last_compile_result_.bytecode.data());
    std::size_t inst_count =
        last_compile_result_.bytecode.size() / sizeof(cedar::Instruction);
    for (const auto &m : last_compile_result_.scalar_sample_mappings) {
        if (m.instruction_index >= inst_count) continue;
        std::string lookup = build_sample_lookup_name(m.bank, m.name, m.variant);
        auto id = vm_->sample_bank().get_sample_id(lookup);
        insts[m.instruction_index].state_id = id;
    }
}

void NkidoAudioStream::apply_state_inits(
    const std::vector<akkado::StateInitData> &p_inits) {
    for (const auto &init : p_inits) {
        switch (init.type) {
        case akkado::StateInitData::Type::SequenceProgram: {
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
            if (init.iter_n > 0) {
                vm_->init_sequence_iter_state(init.state_id, init.iter_n,
                    init.iter_dir);
            }
            break;
        }
        case akkado::StateInitData::Type::PolyAlloc: {
            vm_->init_poly_state(init.state_id, init.poly_seq_state_id,
                init.poly_max_voices, init.poly_mode, init.poly_steal_strategy,
                init.poly_release_seconds, init.poly_prop_count,
                init.poly_prop_count > 0 ? init.poly_prop_defaults : nullptr);
            break;
        }
        case akkado::StateInitData::Type::Timeline: {
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
            break;
        }
        case akkado::StateInitData::Type::ExtendedParams: {
            vm_->init_extended_params(init.state_id,
                init.ext_constants.data(),
                init.ext_buffer_indices.data(),
                init.ext_count);
            break;
        }
        case akkado::StateInitData::Type::ForeachAlloc: {
            vm_->init_foreach_state(init.state_id,
                init.foreach_allocator_kind,
                init.foreach_block_id,
                init.foreach_event_src_state_id,
                init.foreach_max_iterations,
                init.poly_max_voices,
                init.poly_mode,
                init.poly_steal_strategy,
                init.poly_release_seconds,
                init.poly_prop_count,
                init.poly_prop_count > 0 ? init.poly_prop_defaults : nullptr);
            break;
        }
        case akkado::StateInitData::Type::SoundfontEvents: {
            vm_->init_soundfont_voice_event_state(init.state_id,
                init.sf_seq_state_id, init.sf_preset_idx);
            break;
        }
        case akkado::StateInitData::Type::EventTransform: {
            // Transform-owned SequenceState; no compiled sequences. The
            // VM allocates output buffers sized from total_events. Init is
            // a same-call to init_sequence_program_state with empty sequences.
            vm_->init_sequence_program_state(init.state_id, nullptr, 0,
                init.cycle_length, init.is_sample_pattern, init.total_events);
            break;
        }
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
