@tool
extends VBoxContainer

var current_player: AudioStreamPlayer = null
var current_stream: Object = null

var toolbar: HBoxContainer
var code_edit: CodeEdit
var status_label: Label
var params_panel: VBoxContainer
var waveform_view: Control
var waveform_timer: Timer

func _ready() -> void:
  name = "NkidoPanel"

  # --- Toolbar ---
  toolbar = HBoxContainer.new()
  toolbar.add_theme_constant_override("separation", 6)

  var compile_btn := Button.new()
  compile_btn.text = "Compile"
  compile_btn.pressed.connect(_on_compile)
  toolbar.add_child(compile_btn)

  var play_btn := Button.new()
  play_btn.text = "Play"
  play_btn.pressed.connect(_on_play)
  toolbar.add_child(play_btn)

  var stop_btn := Button.new()
  stop_btn.text = "Stop"
  stop_btn.pressed.connect(_on_stop)
  toolbar.add_child(stop_btn)

  var sep := VSeparator.new()
  toolbar.add_child(sep)

  var bpm_label := Label.new()
  bpm_label.text = "BPM:"
  toolbar.add_child(bpm_label)

  var bpm_spin := SpinBox.new()
  bpm_spin.min_value = 1
  bpm_spin.max_value = 300
  bpm_spin.step = 0.1
  bpm_spin.value = 120
  bpm_spin.value_changed.connect(func(val: float):
    if is_instance_valid(current_stream):
      current_stream.set("bpm", val)
  )
  toolbar.add_child(bpm_spin)

  status_label = Label.new()
  status_label.text = ""
  status_label.add_theme_font_size_override("font_size", 12)
  status_label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
  status_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
  toolbar.add_child(status_label)

  add_child(toolbar)

  # --- Main split: code + params/waveform ---
  var hsplit := HSplitContainer.new()
  hsplit.size_flags_vertical = Control.SIZE_EXPAND_FILL

  # Code editor
  code_edit = CodeEdit.new()
  code_edit.size_flags_horizontal = Control.SIZE_EXPAND_FILL
  code_edit.size_flags_vertical = Control.SIZE_EXPAND_FILL
  code_edit.placeholder_text = 'osc("sin", 440) |> out(%, %)'
  code_edit.gutters_draw_line_numbers = true

  # Font
  var editor_theme := EditorInterface.get_editor_theme()
  var base_font := editor_theme.get_font("source", "EditorFonts")
  var font := FontVariation.new()
  font.base_font = base_font
  font.set_opentype_features({0x63616C74: 1, 0x636C6967: 1, 0x6C696761: 1})
  code_edit.add_theme_font_override("font", font)
  code_edit.add_theme_font_size_override("font_size", 13)

  # Syntax highlighting (reuse from inspector)
  var InspectorScript = preload("nkido_inspector.gd")
  code_edit.syntax_highlighter = InspectorScript._create_highlighter()

  # Error gutter
  code_edit.add_gutter(0)
  code_edit.set_gutter_type(0, TextEdit.GUTTER_TYPE_ICON)
  code_edit.set_gutter_width(0, 20)
  code_edit.set_gutter_name(0, "errors")

  code_edit.text_changed.connect(_on_text_changed)
  hsplit.add_child(code_edit)

  # Right panel: params + waveform
  var right_panel := VBoxContainer.new()
  right_panel.custom_minimum_size.x = 250
  right_panel.add_theme_constant_override("separation", 8)

  params_panel = VBoxContainer.new()
  params_panel.add_theme_constant_override("separation", 4)
  right_panel.add_child(params_panel)

  # Waveform
  waveform_view = NkidoWaveformView.new()
  waveform_view.custom_minimum_size = Vector2(0, 80)
  right_panel.add_child(waveform_view)

  hsplit.add_child(right_panel)
  add_child(hsplit)

  # Waveform update timer
  waveform_timer = Timer.new()
  waveform_timer.wait_time = 1.0 / 30.0  # ~30 FPS
  waveform_timer.timeout.connect(_on_waveform_tick)
  add_child(waveform_timer)

func set_player(player: AudioStreamPlayer) -> void:
  # Disconnect old signals
  if is_instance_valid(current_stream):
    if current_stream.has_signal("compilation_finished"):
      if current_stream.is_connected("compilation_finished", _on_compilation_finished):
        current_stream.disconnect("compilation_finished", _on_compilation_finished)
    if current_stream.has_signal("params_changed"):
      if current_stream.is_connected("params_changed", _on_params_changed):
        current_stream.disconnect("params_changed", _on_params_changed)

  current_player = player
  current_stream = player.stream if player else null

  if not current_stream:
    code_edit.text = ""
    waveform_timer.stop()
    return

  # Connect signals
  if current_stream.has_signal("compilation_finished"):
    current_stream.connect("compilation_finished", _on_compilation_finished)
  if current_stream.has_signal("params_changed"):
    current_stream.connect("params_changed", _on_params_changed)

  # Load source
  var source_file: String = current_stream.get("source_file") if current_stream.get("source_file") else ""
  if not source_file.is_empty():
    var f := FileAccess.open(source_file, FileAccess.READ)
    if f:
      code_edit.text = f.get_as_text()
    else:
      code_edit.text = ""
  else:
    code_edit.text = current_stream.get("source") if current_stream.get("source") else ""

  # Load params
  var decls: Array = current_stream.call("get_param_decls")
  if decls.size() > 0:
    _build_param_controls(decls)

  waveform_timer.start()

func _on_compile() -> void:
  if not is_instance_valid(current_stream):
    return
  current_stream.call("compile")

func _on_play() -> void:
  if not is_instance_valid(current_stream) or not is_instance_valid(current_player):
    return
  if current_stream.call("compile"):
    current_player.play()

func _on_stop() -> void:
  if is_instance_valid(current_player):
    current_player.stop()

func _on_text_changed() -> void:
  if not is_instance_valid(current_stream):
    return
  var source_file: String = current_stream.get("source_file") if current_stream.get("source_file") else ""
  if not source_file.is_empty():
    var f := FileAccess.open(source_file, FileAccess.WRITE)
    if f:
      f.store_string(code_edit.text)
  else:
    current_stream.set("source", code_edit.text)

func _on_compilation_finished(success: bool, errors: Array) -> void:
  if not is_instance_valid(status_label):
    return
  if success:
    status_label.text = "Compiled"
    status_label.add_theme_color_override("font_color", Color(0.5, 1.0, 0.5))
  else:
    var msg := "Error"
    if errors.size() > 0:
      var e: Dictionary = errors[0]
      msg += " - Line %d: %s" % [e.get("line", 0), e.get("message", "")]
    status_label.text = msg
    status_label.add_theme_color_override("font_color", Color(1.0, 0.4, 0.4))

  # Update error markers
  if is_instance_valid(code_edit):
    for i in range(code_edit.get_line_count()):
      code_edit.set_line_gutter_icon(i, 0, null)
    if not success:
      var error_icon := EditorInterface.get_editor_theme().get_icon("StatusError", "EditorIcons")
      for e: Dictionary in errors:
        var line: int = e.get("line", 0) - 1
        if line >= 0 and line < code_edit.get_line_count():
          code_edit.set_line_gutter_icon(line, 0, error_icon)

func _on_params_changed(params: Array) -> void:
  _build_param_controls(params)

func _on_waveform_tick() -> void:
  if not is_instance_valid(waveform_view):
    return
  if is_instance_valid(current_player) and current_player.playing and is_instance_valid(current_stream):
    var data: PackedFloat32Array = current_stream.call("get_waveform_data")
    waveform_view.set_data(data)
  else:
    waveform_view.set_data(PackedFloat32Array())

func _build_param_controls(params: Array) -> void:
  if not is_instance_valid(params_panel):
    return
  for child in params_panel.get_children():
    child.queue_free()
  if params.size() == 0:
    return

  var header := Label.new()
  header.text = "Parameters"
  header.add_theme_font_size_override("font_size", 13)
  params_panel.add_child(header)

  for p: Dictionary in params:
    var param_type: String = p.get("type", "continuous")
    var param_name: String = p.get("name", "")
    match param_type:
      "continuous":
        var row := HBoxContainer.new()
        row.add_theme_constant_override("separation", 6)
        var label := Label.new()
        label.text = param_name
        label.custom_minimum_size.x = 70
        row.add_child(label)
        var slider := HSlider.new()
        slider.min_value = p.get("min", 0.0)
        slider.max_value = p.get("max", 1.0)
        slider.value = p.get("default", 0.0)
        slider.step = (slider.max_value - slider.min_value) / 1000.0
        slider.size_flags_horizontal = Control.SIZE_EXPAND_FILL
        var cn := param_name
        slider.value_changed.connect(func(val: float):
          if is_instance_valid(current_stream):
            current_stream.call("set_param", cn, val)
        )
        row.add_child(slider)
        var vlbl := Label.new()
        vlbl.text = "%.2f" % slider.value
        vlbl.custom_minimum_size.x = 40
        slider.value_changed.connect(func(val: float): vlbl.text = "%.2f" % val)
        row.add_child(vlbl)
        params_panel.add_child(row)
      "button":
        var btn := Button.new()
        btn.text = param_name
        var cn := param_name
        btn.pressed.connect(func():
          if is_instance_valid(current_stream):
            current_stream.call("trigger_button", cn)
        )
        params_panel.add_child(btn)
      "toggle":
        var tog := CheckButton.new()
        tog.text = param_name
        tog.button_pressed = p.get("default", 0.0) > 0.5
        var cn := param_name
        tog.toggled.connect(func(pressed: bool):
          if is_instance_valid(current_stream):
            current_stream.call("set_param", cn, 1.0 if pressed else 0.0)
        )
        params_panel.add_child(tog)
      "select":
        var row := HBoxContainer.new()
        row.add_theme_constant_override("separation", 6)
        var label := Label.new()
        label.text = param_name
        label.custom_minimum_size.x = 70
        row.add_child(label)
        var option := OptionButton.new()
        var options: Array = p.get("options", [])
        for i in range(options.size()):
          option.add_item(options[i], i)
        option.selected = int(p.get("default", 0.0))
        var cn := param_name
        option.item_selected.connect(func(idx: int):
          if is_instance_valid(current_stream):
            current_stream.call("set_param", cn, float(idx))
        )
        row.add_child(option)
        params_panel.add_child(row)


# --- Waveform visualization ---
class NkidoWaveformView extends Control:
  var waveform_data: PackedFloat32Array = PackedFloat32Array()

  func set_data(data: PackedFloat32Array) -> void:
    waveform_data = data
    queue_redraw()

  func _draw() -> void:
    var rect := get_rect()
    var w := rect.size.x
    var h := rect.size.y

    # Background
    draw_rect(Rect2(Vector2.ZERO, rect.size), Color(0.1, 0.1, 0.12), true)

    # Center line
    draw_line(Vector2(0, h / 2.0), Vector2(w, h / 2.0), Color(0.25, 0.25, 0.3), 1.0)

    if waveform_data.size() < 4:
      return

    var num_frames: int = waveform_data.size() / 2
    var points := PackedVector2Array()
    points.resize(int(w))

    # Downsample: for each pixel, take the sample at corresponding position (left channel)
    for px in range(int(w)):
      var idx: int = int(float(px) / w * num_frames)
      idx = clampi(idx, 0, num_frames - 1)
      var sample: float = waveform_data[idx * 2]  # Left channel
      var y: float = h / 2.0 - sample * (h / 2.0) * 0.9
      points[px] = Vector2(px, clampf(y, 0, h))

    draw_polyline(points, Color(0.3, 0.85, 0.7, 0.9), 1.5, true)
