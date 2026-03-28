@tool
extends EditorPlugin

var inspector_plugin: EditorInspectorPlugin

func _enter_tree() -> void:
  inspector_plugin = preload("nkido_inspector.gd").new()
  add_inspector_plugin(inspector_plugin)

func _exit_tree() -> void:
  remove_inspector_plugin(inspector_plugin)
