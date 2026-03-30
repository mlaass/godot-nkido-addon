extends Node2D

@onready var player: AudioStreamPlayer = $Player

func _ready():
	var stream: NkidoAudioStream = player.stream
	stream.compilation_finished.connect(_on_compiled)

func _on_compiled(success: bool, errors: Array):
	if success:
		print("Nkido: Compiled successfully")
	else:
		for e in errors:
			print("Nkido error: Line %d: %s" % [e.get("line", 0), e.get("message", "")])
