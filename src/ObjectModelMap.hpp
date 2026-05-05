#pragma once
#include <string>

// Returns the OBJ model filename for an inanimate object at a map cell,
// based on its mf (map_feature) value.  Unknown types fall back to
// "icosphere.obj".
const std::string& getObjectModelFile(int mf);
