@tool
extends EditorInspectorPlugin

var current_player: Node = null
var status_label: Label = null
var params_container: VBoxContainer = null

func _can_handle(object: Object) -> bool:
  return object.has_method(&"compile") and object.has_method(&"get_param_decls")

func _parse_begin(object: Object) -> void:
  current_player = object as Node

  var container := VBoxContainer.new()
  container.add_theme_constant_override("separation", 8)

  # --- Transport buttons ---
  var transport := HBoxContainer.new()
  transport.add_theme_constant_override("separation", 4)

  var play_btn := Button.new()
  play_btn.text = "Play"
  play_btn.pressed.connect(func():
    if current_player.call("compile"):
      current_player.call("play")
  )
  transport.add_child(play_btn)

  var stop_btn := Button.new()
  stop_btn.text = "Stop"
  stop_btn.pressed.connect(func():
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

  # Connect signals
  if current_player.has_signal("compilation_finished"):
    if not current_player.is_connected("compilation_finished", _on_compilation_finished):
      current_player.connect("compilation_finished", _on_compilation_finished)
  if current_player.has_signal("params_changed"):
    if not current_player.is_connected("params_changed", _on_params_changed):
      current_player.connect("params_changed", _on_params_changed)

  # Build initial params if already compiled
  var decls: Array = current_player.call("get_param_decls")
  if decls.size() > 0:
    _build_param_controls(decls)

  add_custom_control(container)

func _parse_property(object: Object, type: Variant.Type, name: String, hint_type: PropertyHint, hint_string: String, usage_flags: int, wide: bool) -> bool:
  if name == "source":
    var edit := CodeEdit.new()
    edit.custom_minimum_size = Vector2(0, 180)
    edit.text = object.get("source")
    edit.placeholder_text = 'osc("sin", 440) |> out(%, %)'
    edit.gutters_draw_line_numbers = true
    edit.scroll_fit_content_height = false

    # Monospaced font with ligatures
    var base_font := SystemFont.new()
    base_font.font_names = PackedStringArray([
      "Fira Code", "JetBrains Mono", "Cascadia Code",
      "Source Code Pro", "Consolas", "monospace"
    ])
    var font := FontVariation.new()
    font.base_font = base_font
    # Enable contextual alternates (calt) for ligatures like |>
    # OpenType tag "calt" = 0x63616C74
    font.set_opentype_features({0x63616C74: 1, 0x636C6967: 1})
    edit.add_theme_font_override("font", font)
    edit.add_theme_font_size_override("font_size", 13)

    # Syntax highlighting
    var hl := CodeHighlighter.new()
    hl.number_color = Color(0.72, 0.52, 0.9)       # purple - numbers
    hl.symbol_color = Color(0.67, 0.76, 0.82)      # light blue-grey - operators
    hl.function_color = Color(0.4, 0.75, 0.95)     # blue - function calls
    hl.member_variable_color = Color(0.9, 0.55, 0.4) # orange - member access

    # Comments
    hl.add_color_region("//", "", Color(0.45, 0.5, 0.45), true)

    # Strings
    hl.add_color_region("\"", "\"", Color(0.6, 0.82, 0.5))

    # DSP builtins - oscillators
    for kw in ["osc", "sin", "tri", "saw", "sqr", "sine", "ramp", "phasor",
        "sqr_minblep", "sqr_pwm", "saw_pwm", "sqr_pwm_minblep"]:
      hl.add_keyword_color(kw, Color(0.4, 0.85, 0.75))

    # Filters
    for kw in ["lp", "hp", "bp", "moog", "diode", "formant", "sallenkey",
        "lpf", "hpf", "bpf"]:
      hl.add_keyword_color(kw, Color(0.4, 0.85, 0.75))

    # Envelopes & dynamics
    for kw in ["adsr", "ar", "env_follower", "comp", "limiter", "gate"]:
      hl.add_keyword_color(kw, Color(0.4, 0.85, 0.75))

    # Effects
    for kw in ["delay", "delay_ms", "delay_smp", "tap_delay",
        "freeverb", "dattorro", "fdn", "chorus", "flanger", "phaser", "comb",
        "saturate", "softclip", "bitcrush", "fold", "tube", "tape", "xfmr",
        "excite", "smooth", "pingpong"]:
      hl.add_keyword_color(kw, Color(0.4, 0.85, 0.75))

    # I/O & utility
    for kw in ["out", "noise", "mtof", "dc", "slew", "sah", "clock", "lfo",
        "trigger", "stereo", "left", "right", "pan", "width",
        "ms_encode", "ms_decode", "sample", "sample_loop", "soundfont"]:
      hl.add_keyword_color(kw, Color(0.4, 0.85, 0.75))

    # Parameters
    for kw in ["param", "button", "toggle", "dropdown"]:
      hl.add_keyword_color(kw, Color(0.95, 0.75, 0.3))

    # Patterns & sequencing
    for kw in ["pat", "seq", "note", "timeline", "beat", "co", "euclid"]:
      hl.add_keyword_color(kw, Color(0.95, 0.6, 0.65))

    # Math
    for kw in ["abs", "sqrt", "log", "exp", "floor", "ceil",
        "cos", "tan", "asin", "acos", "atan", "sinh", "cosh", "tanh",
        "min", "max", "clamp", "wrap", "select", "neg", "pow"]:
      hl.add_keyword_color(kw, Color(0.4, 0.85, 0.75))

    # Language keywords
    for kw in ["as", "true", "false"]:
      hl.add_keyword_color(kw, Color(0.85, 0.5, 0.65))

    edit.syntax_highlighter = hl

    edit.text_changed.connect(func():
      object.set("source", edit.text)
    )
    add_custom_control(edit)
    return true
  return false

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

func _on_params_changed(params: Array) -> void:
  _build_param_controls(params)

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
    if is_instance_valid(current_player):
      current_player.call("set_param", captured_name, val)
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
    if is_instance_valid(current_player):
      current_player.call("trigger_button", captured_name)
  )
  params_container.add_child(btn)

func _add_toggle_param(param_name: String, p: Dictionary) -> void:
  var toggle := CheckButton.new()
  toggle.text = param_name
  toggle.button_pressed = p.get("default", 0.0) > 0.5
  var captured_name := param_name
  toggle.toggled.connect(func(pressed: bool):
    if is_instance_valid(current_player):
      current_player.call("set_param", captured_name, 1.0 if pressed else 0.0)
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
    if is_instance_valid(current_player):
      current_player.call("set_param", captured_name, float(idx))
  )
  row.add_child(option)

  params_container.add_child(row)
