#include "CharacterSelect.hpp"
#include <spdlog/spdlog.h>

static CharSelectPhase menuIdToPhase(const std::string& menuId)
{
    if (menuId.starts_with("species-"))
        return CharSelectPhase::Species;
    if (menuId.starts_with("background-"))
        return CharSelectPhase::Background;
    if (menuId.starts_with("weapon-"))
        return CharSelectPhase::Weapon;
    return CharSelectPhase::None;
}

static ChoiceButton parseButton(const json& btnJson)
{
    ChoiceButton btn;
    btn.x = btnJson.value("x", 0);
    btn.y = btnJson.value("y", 0);
    btn.hotkey = static_cast<char>(btnJson.value("hotkey", 0));
    btn.description = btnJson.value("description", "");
    btn.highlightColour = btnJson.value("highlight_colour", 0);

    // Label: prefer the first element of "labels" array, fall back to "label" string.
    if (auto labelsIt = btnJson.find("labels");
        labelsIt != btnJson.end() && labelsIt->is_array() && !labelsIt->empty()) {
        btn.label = labelsIt->at(0).get<std::string>();
    } else {
        btn.label = btnJson.value("label", "");
    }

    return btn;
}

static void parseItemGrid(const json& gridJson, std::vector<ChoiceButton>& outButtons,
                          int& outWidth, int& outHeight)
{
    outWidth = gridJson.value("width", 0);
    outHeight = gridJson.value("height", 0);

    if (auto buttonsIt = gridJson.find("buttons");
        buttonsIt != gridJson.end() && buttonsIt->is_array()) {
        for (const auto& btnJson : *buttonsIt) {
            outButtons.push_back(parseButton(btnJson));
        }
    }
}

NewgameChoice parseNewgameChoice(const json& uiPushMessage)
{
    NewgameChoice result;

    // Guard: must be a newgame-choice ui-push
    if (uiPushMessage.value("type", "") != "newgame-choice") {
        spdlog::debug("parseNewgameChoice: wrong type '{}'", uiPushMessage.value("type", ""));
        return result;
    }

    // Both main-items and sub-items are required
    auto mainIt = uiPushMessage.find("main-items");
    auto subIt = uiPushMessage.find("sub-items");
    if (mainIt == uiPushMessage.end() || subIt == uiPushMessage.end()) {
        spdlog::debug("parseNewgameChoice: missing main-items or sub-items");
        return result;
    }

    // Determine phase from main-items menu_id
    result.menuId = mainIt->value("menu_id", "");
    if (result.menuId.empty()) {
        spdlog::debug("parseNewgameChoice: missing menu_id");
        return result;
    }
    result.phase = menuIdToPhase(result.menuId);

    // Title
    result.title = uiPushMessage.value("title", "");

    // Parse main items grid
    parseItemGrid(*mainIt, result.mainButtons, result.width, result.height);

    // Parse sub items grid (width/height ignored — use main's dimensions)
    int subW = 0, subH = 0;
    parseItemGrid(*subIt, result.subButtons, subW, subH);

    result.isValid = true;
    return result;
}
