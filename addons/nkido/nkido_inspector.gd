@tool
extends EditorInspectorPlugin

var current_player: Node = null  # AudioStreamPlayer
var current_stream: Object = null  # NkidoAudioStream
var status_label: Label = null
var params_container: VBoxContainer = null
var code_edit: CodeEdit = null

func _can_handle(object: Object) -> bool:
  if object is AudioStreamPlayer:
    var stream = object.get("stream")
    if stream and stream.has_method(&"compile") and stream.has_method(&"get_param_decls"):
      return true
  return false

func _parse_begin(object: Object) -> void:
  current_player = object as Node
  current_stream = object.get("stream")
  if not current_stream:
    return

  var container := VBoxContainer.new()
  container.add_theme_constant_override("separation", 8)

  # --- Transport buttons ---
  var transport := HBoxContainer.new()
  transport.add_theme_constant_override("separation", 4)

  var play_btn := Button.new()
  play_btn.text = "Play"
  play_btn.pressed.connect(func():
    if is_instance_valid(current_stream) and current_stream.call("compile"):
      if is_instance_valid(current_player):
        current_player.call("play")
  )
  transport.add_child(play_btn)

  var stop_btn := Button.new()
  stop_btn.text = "Stop"
  stop_btn.pressed.connect(func():
    if is_instance_valid(current_player):
      current_player.call("stop")
  )
  transport.add_child(stop_btn)

  container.add_child(transport)

  # --- Status label ---
  status_label = Label.new()
  status_label.text = ""
  status_label.add_theme_font_size_override("font_size", 12)
  container.add_child(status_label)

  # --- Parameters container ---
  params_container = VBoxContainer.new()
  params_container.add_theme_constant_override("separation", 4)
  container.add_child(params_container)

  # Connect signals on the stream
  if current_stream.has_signal("compilation_finished"):
    if not current_stream.is_connected("compilation_finished", _on_compilation_finished):
      current_stream.connect("compilation_finished", _on_compilation_finished)
  if current_stream.has_signal("params_changed"):
    if not current_stream.is_connected("params_changed", _on_params_changed):
      current_stream.connect("params_changed", _on_params_changed)

  # Build initial params if already compiled
  var decls: Array = current_stream.call("get_param_decls")
  if decls.size() > 0:
    _build_param_controls(decls)

  add_custom_control(container)

func _parse_property(object: Object, type: Variant.Type, name: String, hint_type: PropertyHint, hint_string: String, usage_flags: int, wide: bool) -> bool:
  # Intercept the stream property to add source editor
  if name == "stream" and current_stream:
    # --- Source file row ---
    var source_file: String = current_stream.get("source_file") if current_stream.get("source_file") else ""
    if not source_file.is_empty():
      var file_row := HBoxContainer.new()
      file_row.add_theme_constant_override("separation", 4)
      var file_label := Label.new()
      file_label.text = source_file.get_file()
      file_label.tooltip_text = source_file
      file_label.add_theme_font_size_override("font_size", 12)
      file_label.add_theme_color_override("font_color", Color(0.6, 0.8, 1.0))
      file_label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
      file_row.add_child(file_label)

      var reload_btn := Button.new()
      reload_btn.text = "Reload"
      reload_btn.pressed.connect(func():
        if is_instance_valid(current_stream):
          _reload_source_file()
      )
      file_row.add_child(reload_btn)
      add_custom_control(file_row)

    # --- Code editor ---
    code_edit = CodeEdit.new()
    code_edit.custom_minimum_size = Vector2(0, 180)
    code_edit.gutters_draw_line_numbers = true
    code_edit.scroll_fit_content_height = false

    # Load initial content
    if not source_file.is_empty():
      var f := FileAccess.open(source_file, FileAccess.READ)
      if f:
        code_edit.text = f.get_as_text()
      else:
        code_edit.text = current_stream.get("source") if current_stream.get("source") else ""
    else:
      code_edit.text = current_stream.get("source") if current_stream.get("source") else ""

    code_edit.placeholder_text = 'osc("sin", 440) |> out(%, %)'

    # Monospaced font with ligatures
    var editor_theme := EditorInterface.get_editor_theme()
    var base_font := editor_theme.get_font("source", "EditorFonts")
    var font := FontVariation.new()
    font.base_font = base_font
    font.set_opentype_features({0x63616C74: 1, 0x636C6967: 1, 0x6C696761: 1})
    code_edit.add_theme_font_override("font", font)
    code_edit.add_theme_font_size_override("font_size", 13)

    # Syntax highlighting
    code_edit.syntax_highlighter = _create_highlighter()

    # Error gutter
    code_edit.add_gutter(0)
    code_edit.set_gutter_type(0, TextEdit.GUTTER_TYPE_ICON)
    code_edit.set_gutter_width(0, 20)
    code_edit.set_gutter_name(0, "errors")

    var stream_ref = current_stream
    var is_file = not source_file.is_empty()
    code_edit.text_changed.connect(func():
      if is_instance_valid(stream_ref):
        if is_file:
          var sf: String = stream_ref.get("source_file")
          if not sf.is_empty():
            var f := FileAccess.open(sf, FileAccess.WRITE)
            if f:
              f.store_string(code_edit.text)
        else:
          stream_ref.set("source", code_edit.text)
    )
    add_custom_control(code_edit)
  return false

func _reload_source_file() -> void:
  if not is_instance_valid(current_stream) or not is_instance_valid(code_edit):
    return
  var source_file: String = current_stream.get("source_file")
  if source_file.is_empty():
    return
  var f := FileAccess.open(source_file, FileAccess.READ)
  if f:
    code_edit.text = f.get_as_text()

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

  # Update error gutters
  _update_error_markers(errors if not success else [])

func _update_error_markers(errors: Array) -> void:
  if not is_instance_valid(code_edit):
    return
  # Clear all gutter icons
  for i in range(code_edit.get_line_count()):
    code_edit.set_line_gutter_icon(i, 0, null)
    code_edit.set_line_gutter_clickable(i, 0, false)
  # Set error icons
  var error_icon := EditorInterface.get_editor_theme().get_icon("StatusError", "EditorIcons")
  for e: Dictionary in errors:
    var line: int = e.get("line", 0) - 1  # 1-indexed to 0-indexed
    if line >= 0 and line < code_edit.get_line_count():
      code_edit.set_line_gutter_icon(line, 0, error_icon)
      code_edit.set_line_gutter_clickable(line, 0, true)

func _on_params_changed(params: Array) -> void:
  _build_param_controls(params)

static func _create_highlighter() -> CodeHighlighter:
  var hl := CodeHighlighter.new()
  hl.number_color = Color(0.72, 0.52, 0.9)
  hl.symbol_color = Color(0.67, 0.76, 0.82)
  hl.function_color = Color(0.4, 0.75, 0.95)
  hl.member_variable_color = Color(0.9, 0.55, 0.4)

  hl.add_color_region("//", "", Color(0.45, 0.5, 0.45), true)
  hl.add_color_region("\"", "\"", Color(0.6, 0.82, 0.5))

  # DSP builtins
  for kw in ["osc", "sin", "tri", "saw", "sqr", "sine", "ramp", "phasor",
      "sqr_minblep", "sqr_pwm", "saw_pwm", "sqr_pwm_minblep",
      "lp", "hp", "bp", "moog", "diode", "formant", "sallenkey", "lpf", "hpf", "bpf",
      "adsr", "ar", "env_follower", "comp", "limiter", "gate",
      "delay", "delay_ms", "delay_smp", "tap_delay",
      "freeverb", "dattorro", "fdn", "chorus", "flanger", "phaser", "comb",
      "saturate", "softclip", "bitcrush", "fold", "tube", "tape", "xfmr",
      "excite", "smooth", "pingpong",
      "out", "noise", "mtof", "dc", "slew", "sah", "clock", "lfo",
      "trigger", "stereo", "left", "right", "pan", "width",
      "ms_encode", "ms_decode", "sample", "sample_loop", "soundfont",
      "abs", "sqrt", "log", "exp", "floor", "ceil",
      "cos", "tan", "asin", "acos", "atan", "sinh", "cosh", "tanh",
      "min", "max", "clamp", "wrap", "select", "neg", "pow"]:
    hl.add_keyword_color(kw, Color(0.4, 0.85, 0.75))

  # Parameters
  for kw in ["param", "button", "toggle", "dropdown"]:
    hl.add_keyword_color(kw, Color(0.95, 0.75, 0.3))

  # Patterns & sequencing
  for kw in ["pat", "seq", "note", "timeline", "beat", "co", "euclid", "samp", "sf"]:
    hl.add_keyword_color(kw, Color(0.95, 0.6, 0.65))

  # Language keywords
  for kw in ["as", "true", "false"]:
    hl.add_keyword_color(kw, Color(0.85, 0.5, 0.65))

  return hl

func _build_param_controls(params: Array) -> void:
  if not is_instance_valid(params_container):
    return

  for child in params_container.get_children():
    child.queue_free()

  if params.size() == 0:
    return

  var header := Label.new()
  header.text = "Parameters"
  header.add_theme_font_size_override("font_size", 13)
  params_container.add_child(header)

  for p: Dictionary in params:
    var param_type: String = p.get("type", "continuous")
    var param_name: String = p.get("name", "")

    match param_type:
      "continuous":
        _add_slider_param(param_name, p)
      "button":
        _add_button_param(param_name)
      "toggle":
        _add_toggle_param(param_name, p)
      "select":
        _add_select_param(param_name, p)

func _add_slider_param(param_name: String, p: Dictionary) -> void:
  var row := HBoxContainer.new()
  row.add_theme_constant_override("separation", 8)

  var label := Label.new()
  label.text = param_name
  label.custom_minimum_size.x = 80
  row.add_child(label)

  var slider := HSlider.new()
  slider.min_value = p.get("min", 0.0)
  slider.max_value = p.get("max", 1.0)
  slider.value = p.get("default", 0.0)
  slider.step = (slider.max_value - slider.min_value) / 1000.0
  slider.size_flags_horizontal = Control.SIZE_EXPAND_FILL
  var captured_name := param_name
  slider.value_changed.connect(func(val: float):
    if is_instance_valid(current_stream):
      current_stream.call("set_param", captured_name, val)
  )
  row.add_child(slider)

  var value_label := Label.new()
  value_label.text = "%.2f" % slider.value
  value_label.custom_minimum_size.x = 50
  slider.value_changed.connect(func(val: float):
    value_label.text = "%.2f" % val
  )
  row.add_child(value_label)

  params_container.add_child(row)

func _add_button_param(param_name: String) -> void:
  var btn := Button.new()
  btn.text = param_name
  var captured_name := param_name
  btn.pressed.connect(func():
    if is_instance_valid(current_stream):
      current_stream.call("trigger_button", captured_name)
  )
  params_container.add_child(btn)

func _add_toggle_param(param_name: String, p: Dictionary) -> void:
  var toggle := CheckButton.new()
  toggle.text = param_name
  toggle.button_pressed = p.get("default", 0.0) > 0.5
  var captured_name := param_name
  toggle.toggled.connect(func(pressed: bool):
    if is_instance_valid(current_stream):
      current_stream.call("set_param", captured_name, 1.0 if pressed else 0.0)
  )
  params_container.add_child(toggle)

func _add_select_param(param_name: String, p: Dictionary) -> void:
  var row := HBoxContainer.new()
  row.add_theme_constant_override("separation", 8)

  var label := Label.new()
  label.text = param_name
  label.custom_minimum_size.x = 80
  row.add_child(label)

  var option := OptionButton.new()
  var options: Array = p.get("options", [])
  for i in range(options.size()):
    option.add_item(options[i], i)
  option.selected = int(p.get("default", 0.0))
  var captured_name := param_name
  option.item_selected.connect(func(idx: int):
    if is_instance_valid(current_stream):
      current_stream.call("set_param", captured_name, float(idx))
  )
  row.add_child(option)

  params_container.add_child(row)
