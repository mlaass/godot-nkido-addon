extends Control

@onready var player: AudioStreamPlayer = $Player
@onready var play_button: Button = %PlayButton
@onready var stop_button: Button = %StopButton
@onready var status_label: Label = %StatusLabel
@onready var wave_dropdown: OptionButton = %WaveDropdown
@onready var cutoff_slider: HSlider = %CutoffSlider
@onready var resonance_slider: HSlider = %ResonanceSlider
@onready var reverb_slider: HSlider = %ReverbSlider
@onready var volume_slider: HSlider = %VolumeSlider
@onready var hit_button: Button = %HitButton
@onready var chorus_toggle: CheckButton = %ChorusToggle
@onready var waveform_container: Control = %WaveformContainer

var stream: NkidoAudioStream
var waveform_view: WaveformView


func _ready():
  stream = player.stream
  stream.compilation_finished.connect(_on_compiled)

  # Transport
  play_button.pressed.connect(_on_play)
  stop_button.pressed.connect(_on_stop)

  # Param controls
  wave_dropdown.item_selected.connect(func(idx): stream.set_param("wave", float(idx)))
  cutoff_slider.value_changed.connect(func(val): stream.set_param("cutoff", val))
  resonance_slider.value_changed.connect(func(val): stream.set_param("resonance", val))
  reverb_slider.value_changed.connect(func(val): stream.set_param("reverb", val))
  volume_slider.value_changed.connect(func(val): stream.set_param("volume", val))
  hit_button.pressed.connect(func(): stream.trigger_button("hit"))
  chorus_toggle.toggled.connect(func(on): stream.set_param("chorus_on", 1.0 if on else 0.0))

  # Waveform display
  waveform_view = WaveformView.new()
  waveform_view.set_anchors_and_offsets_preset(PRESET_FULL_RECT)
  waveform_container.add_child(waveform_view)


func _process(_delta: float) -> void:
  if player.playing and stream.is_compiled():
    waveform_view.set_data(stream.get_waveform_data())


func _on_play():
  if not stream.is_compiled():
    stream.compile()
  player.play()


func _on_stop():
  player.stop()
  waveform_view.set_data(PackedFloat32Array())


func _on_compiled(success: bool, errors: Array):
  if success:
    status_label.text = "Compiled"
    status_label.add_theme_color_override("font_color", Color.GREEN_YELLOW)
  else:
    var msg := "Error"
    if errors.size() > 0:
      msg += ": Line %d - %s" % [errors[0].get("line", 0), errors[0].get("message", "")]
    status_label.text = msg
    status_label.add_theme_color_override("font_color", Color.TOMATO)


class WaveformView extends Control:
  var waveform_data: PackedFloat32Array = PackedFloat32Array()

  func set_data(data: PackedFloat32Array) -> void:
    waveform_data = data
    queue_redraw()

  func _draw() -> void:
    var rect := get_rect()
    var w := rect.size.x
    var h := rect.size.y
    draw_rect(Rect2(Vector2.ZERO, rect.size), Color(0.1, 0.1, 0.12), true)
    draw_line(Vector2(0, h / 2.0), Vector2(w, h / 2.0), Color(0.25, 0.25, 0.3), 1.0)
    if waveform_data.size() < 4:
      return
    var num_frames: int = waveform_data.size() / 2
    var points := PackedVector2Array()
    points.resize(int(w))
    for px in range(int(w)):
      var idx: int = int(float(px) / w * num_frames)
      idx = clampi(idx, 0, num_frames - 1)
      var sample: float = waveform_data[idx * 2]
      var y: float = h / 2.0 - sample * (h / 2.0) * 0.9
      points[px] = Vector2(px, clampf(y, 0, h))
    draw_polyline(points, Color(0.3, 0.85, 0.7, 0.9), 1.5, true)
