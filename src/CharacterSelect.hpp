#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <vector>

using json = nlohmann::json;

// Phases of character selection, distinguished by menu_id in the ui-push message.
enum class CharSelectPhase {
    None,
    Species,
    Background,
    Weapon
};

// A single selectable button from the newgame-choice grid.
struct ChoiceButton {
    int x = 0;
    int y = 0;
    char hotkey = '\0'; // 'a'-'z' or 'A'-'Z'
    std::string label; // "a - Human"
    std::string description; // "Humans are versatile..."
    int highlightColour = 0; // 0=bad, 1=normal, 2=good
};

// Parsed representation of a newgame-choice ui-push message.
struct NewgameChoice {
    CharSelectPhase phase = CharSelectPhase::None;
    std::string title; // "Please select your species."
    std::string menuId; // "species-main"
    int width = 0;
    int height = 0;
    std::vector<ChoiceButton> mainButtons;
    std::vector<ChoiceButton> subButtons;
    bool isValid = false;
};

// Parse a ui-push message of type "newgame-choice".
// Sets isValid=true on success, false on parse errors or missing data.
NewgameChoice parseNewgameChoice(const json& uiPushMessage);
