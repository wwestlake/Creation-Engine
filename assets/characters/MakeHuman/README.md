# MakeHuman Character Asset

Place the MakeHuman character source files here, including the skeletal FBX,
textures, bind-pose data, and animation clips.

This folder is the source asset location for the reusable Creation Engine
Mannequin system. Imported/native runtime assets should be generated into the
Engine asset pipeline rather than mixed with the source files.

## Source units

Djehuti Engine's model contract is one numeric geometry unit equals one meter.
The supplied MakeHuman conversion chain can carry a `0.1` root/node scale as
FBX unit-conversion baggage even when its raw character mesh is already about
two units tall. The glTF importer ignores node-scale factors and measures the
raw mesh/skeleton instead; it must not apply an additional `0.1` multiplier.

Use **meter** and **Feet on ground** when exporting from MakeHuman. Meter is
the intended authoring unit, while Feet on ground establishes a useful origin;
the Engine still validates the imported geometry rather than trusting source
transform metadata.
