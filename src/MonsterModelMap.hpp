#pragma once
#include <string>

class Monster;

// Returns the OBJ model filename for a given monster, based on its type enum.
// The mapping covers all monster types from crawl's monster-type.h (TAG_MAJOR_VERSION 34).
// Unknown types fall back to "monkey.obj".
const std::string& getMonsterModelFile(const Monster& mon);
