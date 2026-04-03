#include "nkido_akkado_source_format_saver.h"
#include "nkido_akkado_source.h"

#include <godot_cpp/classes/file_access.hpp>

using namespace godot;

PackedStringArray NkidoAkkadoSourceFormatSaver::_get_recognized_extensions(
    const Ref<Resource> &p_resource) const {
    PackedStringArray exts;
    if (Object::cast_to<NkidoAkkadoSource>(p_resource.ptr())) {
        exts.push_back("akk");
    }
    return exts;
}

bool NkidoAkkadoSourceFormatSaver::_recognize(
    const Ref<Resource> &p_resource) const {
    return Object::cast_to<NkidoAkkadoSource>(p_resource.ptr()) != nullptr;
}

Error NkidoAkkadoSourceFormatSaver::_save(const Ref<Resource> &p_resource,
    const String &p_path, uint32_t p_flags) {
    auto *source = Object::cast_to<NkidoAkkadoSource>(p_resource.ptr());
    if (!source) {
        return ERR_INVALID_PARAMETER;
    }

    auto file = FileAccess::open(p_path, FileAccess::WRITE);
    if (file.is_null()) {
        return ERR_FILE_CANT_OPEN;
    }

    file->store_string(source->get_source_code());
    return OK;
}
