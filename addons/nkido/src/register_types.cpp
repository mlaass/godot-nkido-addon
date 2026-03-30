#include "register_types.h"

#include "nkido_audio_stream.h"
#include "nkido_audio_stream_playback.h"

#include <gdextension_interface.h>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/godot.hpp>

#include <cedar/cedar.hpp>

using namespace godot;

void initialize_nkido_module(ModuleInitializationLevel p_level) {
    if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
        return;
    }

    cedar::init();

    GDREGISTER_CLASS(NkidoAudioStream);
    GDREGISTER_CLASS(NkidoAudioStreamPlayback);
}

void uninitialize_nkido_module(ModuleInitializationLevel p_level) {
    if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
        return;
    }

    cedar::shutdown();
}

extern "C" {
GDExtensionBool GDE_EXPORT nkido_library_init(
    GDExtensionInterfaceGetProcAddress p_get_proc_address,
    GDExtensionClassLibraryPtr p_library,
    GDExtensionInitialization *r_initialization) {
    godot::GDExtensionBinding::InitObject init_obj(
        p_get_proc_address, p_library, r_initialization);

    init_obj.register_initializer(initialize_nkido_module);
    init_obj.register_terminator(uninitialize_nkido_module);
    init_obj.set_minimum_library_initialization_level(
        MODULE_INITIALIZATION_LEVEL_SCENE);

    return init_obj.init();
}
}
