extends Node2D

@onready var synth: Node = $NkidoPlayer

func _ready():
	synth.compilation_finished.connect(_on_compiled)

func _on_compiled(success: bool, errors: Array):
	if success:
		print("Nkido: Compiled successfully")
	else:
		for e in errors:
			print("Nkido error: Line %d: %s" % [e.get("line", 0), e.get("message", "")])
