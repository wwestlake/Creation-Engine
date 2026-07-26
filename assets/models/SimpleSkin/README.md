# SimpleSkin

Test/bring-up asset, not original content. Fetched from the Khronos Group's
[glTF-Sample-Assets](https://github.com/KhronosGroup/glTF-Sample-Assets)
repository (`Models/SimpleSkin/glTF/`) — the official minimal conformance
test asset for glTF skinning: a single quad-strip mesh with a 2-joint skin
(inverse bind matrices) and a simple bend animation.

Used here to verify `Source/Render/Import/GltfLoader`'s skin parsing (AI4)
against a real file instead of a hand-authored one. The animation data
(`SimpleSkin_animation.bin`) isn't consumed yet — AI4 is bind-pose only —
but is vendored alongside the rest since it's part of the same official
fixture and will be useful once animation playback (AI5) exists.
