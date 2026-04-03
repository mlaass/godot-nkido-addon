#include "nkido_akkado_source.h"

#include <godot_cpp/core/class_db.hpp>

using namespace godot;

void NkidoAkkadoSource::_bind_methods() {
    ClassDB::bind_method(D_METHOD("set_source_code", "code"),
        &NkidoAkkadoSource::set_source_code);
    ClassDB::bind_method(D_METHOD("get_source_code"),
        &NkidoAkkadoSource::get_source_code);
    ADD_PROPERTY(
        PropertyInfo(Variant::STRING, "source_code", PROPERTY_HINT_MULTILINE_TEXT),
        "set_source_code", "get_source_code");
}

void NkidoAkkadoSource::set_source_code(const String &p_code) {
    if (source_code_ == p_code) {
        return;
    }
    source_code_ = p_code;
    emit_changed();
}

String NkidoAkkadoSource::get_source_code() const {
    return source_code_;
}
