#include "nkido_akkado_source_format_loader.h"
#include "nkido_akkado_source.h"

#include <godot_cpp/classes/file_access.hpp>

using namespace godot;

PackedStringArray NkidoAkkadoSourceFormatLoader::_get_recognized_extensions() const {
    PackedStringArray exts;
    exts.push_back("akk");
    return exts;
}

bool NkidoAkkadoSourceFormatLoader::_handles_type(const StringName &p_type) const {
    return p_type == StringName("NkidoAkkadoSource");
}

String NkidoAkkadoSourceFormatLoader::_get_resource_type(const String &p_path) const {
    if (p_path.get_extension().to_lower() == "akk") {
        return "NkidoAkkadoSource";
    }
    return "";
}

Variant NkidoAkkadoSourceFormatLoader::_load(const String &p_path,
    const String &p_original_path, bool p_use_sub_threads,
    int32_t p_cache_mode) const {
    auto file = FileAccess::open(p_path, FileAccess::READ);
    if (file.is_null()) {
        return Variant();
    }

    String source = file->get_as_text();

    Ref<NkidoAkkadoSource> res;
    res.instantiate();
    res->set_source_code(source);
    res->set_path(p_path);

    return res;
}
