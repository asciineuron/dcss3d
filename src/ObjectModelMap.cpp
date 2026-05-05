#include "ObjectModelMap.hpp"
#include <unordered_map>

const std::string& getObjectModelFile(int mf)
{
    static const std::string s_defaultModel = "icosphere.obj";

    // map_feature enum → OBJ model file mapping.
    // Add per-feature models here as they become available.
    static const std::unordered_map<int, std::string> s_objectModelMap = {
        {  6, "icosphere.obj" },  // MF_ITEM — corpses, dropped items
        { 15, "icosphere.obj" },  // MF_FEATURE — plants, statues, portals, traps
    };

    auto it = s_objectModelMap.find(mf);
    if (it != s_objectModelMap.end())
        return it->second;
    return s_defaultModel;
}
