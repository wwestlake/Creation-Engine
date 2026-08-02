#pragma once

#include <JuceHeader.h>

#include "Render/Import/GltfLoader.h"

namespace ce {

// Parses a Wavefront .obj (plus its referenced .mtl, if any) via
// tinyobjloader and produces the same LoadedModel shape GltfLoader
// produces, so AssetCatalog::AddFromModel and the import/placement
// pipeline don't need to know which format a model came from.
//
// v1 scope, deliberately: every shape/group in the file is merged into
// ONE LoadedPrimitive (matches AddFromModel's own existing "only the
// first primitive is used" limitation for glTF -- an .obj with genuinely
// separate multi-material sub-meshes needing distinct draw calls is a
// natural follow-up, not built here). No skin/animation -- Wavefront OBJ
// has no such concept, so LoadedModel::skin/animations are always left
// empty. Faces with no authored normals get a computed flat face normal
// rather than a degenerate (0,0,0) that would render unlit. See
// ExtractMaterial in the .cpp for why roughness/metallic are left at
// LoadedMaterial's own neutral defaults rather than trusted from the
// .mtl's PBR extension fields.
bool LoadObj(const juce::File& objFile, LoadedModel& outModel);

} // namespace ce
