#include "register_types.h"

#include "nkido_akkado_source.h"
#include "nkido_akkado_source_format_loader.h"
#include "nkido_akkado_source_format_saver.h"
#include "nkido_audio_stream.h"
#include "nkido_audio_stream_playback.h"

#include <gdextension_interface.h>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/resource_saver.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/godot.hpp>

#include <cedar/cedar.hpp>

using namespace godot;

static Ref<NkidoAkkadoSourceFormatLoader> akkado_loader;
static Ref<NkidoAkkadoSourceFormatSaver> akkado_saver;

void initialize_nkido_module(ModuleInitializationLevel p_level) {
    if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
        return;
    }

    cedar::init();

    GDREGISTER_CLASS(NkidoAkkadoSource);
    GDREGISTER_CLASS(NkidoAkkadoSourceFormatLoader);
    GDREGISTER_CLASS(NkidoAkkadoSourceFormatSaver);
    GDREGISTER_CLASS(NkidoAudioStream);
    GDREGISTER_CLASS(NkidoAudioStreamPlayback);

    akkado_loader.instantiate();
    ResourceLoader::get_singleton()->add_resource_format_loader(akkado_loader);

    akkado_saver.instantiate();
    ResourceSaver::get_singleton()->add_resource_format_saver(akkado_saver);
}

void uninitialize_nkido_module(ModuleInitializationLevel p_level) {
    if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
        return;
    }

    ResourceLoader::get_singleton()->remove_resource_format_loader(akkado_loader);
    akkado_loader.unref();

    ResourceSaver::get_singleton()->remove_resource_format_saver(akkado_saver);
    akkado_saver.unref();

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
