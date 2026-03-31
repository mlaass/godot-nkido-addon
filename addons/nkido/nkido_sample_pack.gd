@tool
class_name NkidoSamplePack
extends Resource

## Map sample names to audio file paths.
## Keys are lookup names matching Akkado patterns: "bd", "sd", "hh".
## For variants use "name:N" (e.g., "sd:1"). For banks use "bank_name_N" (e.g., "TR808_bd_0").
@export var samples: Dictionary = {}

## Map soundfont names to SF2 file paths.
## Keys must match the filename used in Akkado soundfont() calls.
@export var soundfonts: Dictionary = {}
