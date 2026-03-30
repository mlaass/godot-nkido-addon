@tool
extends EditorPlugin

var inspector_plugin: EditorInspectorPlugin
var bottom_panel: Control = null

func _enter_tree() -> void:
  inspector_plugin = preload("nkido_inspector.gd").new()
  add_inspector_plugin(inspector_plugin)

  bottom_panel = preload("nkido_bottom_panel.gd").new()
  add_control_to_bottom_panel(bottom_panel, "Nkido")

func _exit_tree() -> void:
  remove_inspector_plugin(inspector_plugin)
  if bottom_panel:
    remove_control_from_bottom_panel(bottom_panel)
    bottom_panel.queue_free()
    bottom_panel = null

func _handles(object: Object) -> bool:
  if object is AudioStreamPlayer:
    var stream = object.get("stream")
    if stream and stream.has_method(&"compile"):
      return true
  return false

func _edit(object: Object) -> void:
  if object is AudioStreamPlayer and bottom_panel:
    bottom_panel.set_player(object as AudioStreamPlayer)
    make_bottom_panel_item_visible(bottom_panel)
