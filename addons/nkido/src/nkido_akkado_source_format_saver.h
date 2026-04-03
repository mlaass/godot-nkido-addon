#pragma once

#include <godot_cpp/classes/resource_format_saver.hpp>

namespace godot {

class NkidoAkkadoSourceFormatSaver : public ResourceFormatSaver {
    GDCLASS(NkidoAkkadoSourceFormatSaver, ResourceFormatSaver)

public:
    PackedStringArray _get_recognized_extensions(
        const Ref<Resource> &p_resource) const override;
    bool _recognize(const Ref<Resource> &p_resource) const override;
    Error _save(const Ref<Resource> &p_resource, const String &p_path,
        uint32_t p_flags) override;

protected:
    static void _bind_methods() {}
};

} // namespace godot
