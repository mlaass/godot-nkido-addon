#pragma once

#include <godot_cpp/classes/object.hpp>

namespace godot {

class NkidoEngine : public Object {
    GDCLASS(NkidoEngine, Object)

public:
    NkidoEngine();
    ~NkidoEngine();

    void set_bpm(float p_bpm);
    float get_bpm() const;
    float get_sample_rate() const;

protected:
    static void _bind_methods();

private:
    float global_bpm_ = 120.0f;
};

} // namespace godot
