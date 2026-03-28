#include "nkido_engine.h"

#include <godot_cpp/classes/audio_server.hpp>
#include <godot_cpp/core/class_db.hpp>

using namespace godot;

NkidoEngine::NkidoEngine() = default;
NkidoEngine::~NkidoEngine() = default;

void NkidoEngine::_bind_methods() {
    ClassDB::bind_method(D_METHOD("set_bpm", "bpm"), &NkidoEngine::set_bpm);
    ClassDB::bind_method(D_METHOD("get_bpm"), &NkidoEngine::get_bpm);
    ClassDB::bind_method(D_METHOD("get_sample_rate"), &NkidoEngine::get_sample_rate);

    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "bpm"), "set_bpm", "get_bpm");
}

void NkidoEngine::set_bpm(float p_bpm) {
    global_bpm_ = p_bpm;
}

float NkidoEngine::get_bpm() const {
    return global_bpm_;
}

float NkidoEngine::get_sample_rate() const {
    auto *audio_server = AudioServer::get_singleton();
    if (audio_server) {
        return audio_server->get_mix_rate();
    }
    return 48000.0f;
}
