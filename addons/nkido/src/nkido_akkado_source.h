#pragma once

#include <godot_cpp/classes/resource.hpp>

namespace godot {

class NkidoAkkadoSource : public Resource {
    GDCLASS(NkidoAkkadoSource, Resource)

public:
    void set_source_code(const String &p_code);
    String get_source_code() const;

protected:
    static void _bind_methods();

private:
    String source_code_;
};

} // namespace godot
