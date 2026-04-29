#include <catch2/catch_all.hpp>
#include <nlohmann/json.hpp>
#include "CharacterSelect.hpp"

using json = nlohmann::json;

// ============================================================
// Helper: build minimal valid species message JSON
// ============================================================
static json makeSpeciesMsg()
{
    // Simplified version of what the DCSS server sends.
    // Real messages have tiles, more buttons, etc.
    return json::parse(R"({
        "msg": "ui-push",
        "type": "newgame-choice",
        "title": "Please select your species.",
        "main-items": {
            "menu_id": "species-main",
            "width": 3,
            "height": 5,
            "buttons": [
                {"x":0,"y":0,"hotkey":97,"label":"a - Human","description":"Humans are versatile.","highlight_colour":1},
                {"x":1,"y":0,"hotkey":98,"label":"b - Elf","description":"Elves are magical.","highlight_colour":2},
                {"x":2,"y":0,"hotkey":99,"label":"c - Dwarf","description":"Dwarves are tough.","highlight_colour":0}
            ],
            "labels": [
                {"x":0,"y":4,"label":"Species"}
            ]
        },
        "sub-items": {
            "menu_id": "species-sub",
            "width": 2,
            "height": 2,
            "buttons": [
                {"x":0,"y":0,"hotkey":43,"label":"+ - Recommended","description":"Choose a recommended species.","highlight_colour":2},
                {"x":1,"y":0,"hotkey":42,"label":"* - Random","description":"Pick a random species.","highlight_colour":1}
            ],
            "labels": []
        }
    })");
}

// ============================================================
// Phase detection
// ============================================================

TEST_CASE("parseNewgameChoice detects Species phase", "[CharacterSelect]")
{
    auto msg = makeSpeciesMsg();
    auto result = parseNewgameChoice(msg);

    REQUIRE(result.isValid == true);
    REQUIRE(result.phase == CharSelectPhase::Species);
    REQUIRE(result.menuId == "species-main");
    REQUIRE(result.title == "Please select your species.");
}

TEST_CASE("parseNewgameChoice detects Background phase", "[CharacterSelect]")
{
    auto msg = makeSpeciesMsg();
    msg["main-items"]["menu_id"] = "background-main";
    msg["sub-items"]["menu_id"] = "background-sub";
    msg["title"] = "Please select your background.";

    auto result = parseNewgameChoice(msg);

    REQUIRE(result.isValid == true);
    REQUIRE(result.phase == CharSelectPhase::Background);
    REQUIRE(result.menuId == "background-main");
}

TEST_CASE("parseNewgameChoice detects Weapon phase", "[CharacterSelect]")
{
    auto msg = makeSpeciesMsg();
    msg["main-items"]["menu_id"] = "weapon-main";
    msg["sub-items"]["menu_id"] = "weapon-sub";
    msg["title"] = "You have a choice of weapons.";

    auto result = parseNewgameChoice(msg);

    REQUIRE(result.isValid == true);
    REQUIRE(result.phase == CharSelectPhase::Weapon);
    REQUIRE(result.menuId == "weapon-main");
}

TEST_CASE("parseNewgameChoice: unknown menu_id returns None phase", "[CharacterSelect]")
{
    auto msg = makeSpeciesMsg();
    msg["main-items"]["menu_id"] = "garbage-menu";

    auto result = parseNewgameChoice(msg);

    REQUIRE(result.isValid == true);
    REQUIRE(result.phase == CharSelectPhase::None);
}

// ============================================================
// Button parsing
// ============================================================

TEST_CASE("parseNewgameChoice parses main buttons", "[CharacterSelect]")
{
    auto result = parseNewgameChoice(makeSpeciesMsg());

    REQUIRE(result.mainButtons.size() == 3);

    // Button 0: Human (hotkey 'a' = 97)
    REQUIRE(result.mainButtons[0].hotkey == 'a');
    REQUIRE(result.mainButtons[0].x == 0);
    REQUIRE(result.mainButtons[0].y == 0);
    REQUIRE(result.mainButtons[0].label == "a - Human");
    REQUIRE(result.mainButtons[0].description == "Humans are versatile.");
    REQUIRE(result.mainButtons[0].highlightColour == 1);

    // Button 1: Elf (hotkey 'b' = 98, highlight = 2 = good)
    REQUIRE(result.mainButtons[1].hotkey == 'b');
    REQUIRE(result.mainButtons[1].label == "b - Elf");
    REQUIRE(result.mainButtons[1].highlightColour == 2);

    // Button 2: Dwarf (hotkey 'c' = 99, highlight = 0 = bad)
    REQUIRE(result.mainButtons[2].hotkey == 'c');
    REQUIRE(result.mainButtons[2].label == "c - Dwarf");
    REQUIRE(result.mainButtons[2].highlightColour == 0);
}

TEST_CASE("parseNewgameChoice parses sub buttons", "[CharacterSelect]")
{
    auto result = parseNewgameChoice(makeSpeciesMsg());

    REQUIRE(result.subButtons.size() == 2);

    // Recommended (hotkey '+' = 43)
    REQUIRE(result.subButtons[0].hotkey == '+');
    REQUIRE(result.subButtons[0].highlightColour == 2);

    // Random (hotkey '*' = 42)
    REQUIRE(result.subButtons[1].hotkey == '*');
    REQUIRE(result.subButtons[1].highlightColour == 1);
}

TEST_CASE("parseNewgameChoice parses grid dimensions", "[CharacterSelect]")
{
    auto result = parseNewgameChoice(makeSpeciesMsg());

    REQUIRE(result.width == 3);
    REQUIRE(result.height == 5);
}

// ============================================================
// Labels (non-interactive text)
// ============================================================

TEST_CASE("parseNewgameChoice handles labels array", "[CharacterSelect]")
{
    auto result = parseNewgameChoice(makeSpeciesMsg());

    // Labels are stored as buttons with empty hotkey (just for display)
    REQUIRE(result.mainButtons.size() == 3); // actual buttons
    // We don't store labels in mainButtons; they're for display-only.
    // The parser just ignores labels (they're non-interactive text).
}

// ============================================================
// Invalid / missing data
// ============================================================

TEST_CASE("parseNewgameChoice: wrong msg type returns invalid", "[CharacterSelect]")
{
    auto msg = makeSpeciesMsg();
    msg["type"] = "describe-generic";

    auto result = parseNewgameChoice(msg);

    REQUIRE(result.isValid == false);
}

TEST_CASE("parseNewgameChoice: missing main-items returns invalid", "[CharacterSelect]")
{
    auto msg = makeSpeciesMsg();
    msg.erase("main-items");

    auto result = parseNewgameChoice(msg);

    REQUIRE(result.isValid == false);
}

TEST_CASE("parseNewgameChoice: missing sub-items returns invalid", "[CharacterSelect]")
{
    auto msg = makeSpeciesMsg();
    msg.erase("sub-items");

    auto result = parseNewgameChoice(msg);

    REQUIRE(result.isValid == false);
}

TEST_CASE("parseNewgameChoice: missing menu_id returns invalid", "[CharacterSelect]")
{
    auto msg = makeSpeciesMsg();
    msg["main-items"].erase("menu_id");

    auto result = parseNewgameChoice(msg);

    REQUIRE(result.isValid == false);
}

TEST_CASE("parseNewgameChoice: empty buttons still valid", "[CharacterSelect]")
{
    auto msg = makeSpeciesMsg();
    msg["main-items"]["buttons"] = json::array();
    msg["sub-items"]["buttons"] = json::array();

    auto result = parseNewgameChoice(msg);

    REQUIRE(result.isValid == true);
    REQUIRE(result.mainButtons.empty());
    REQUIRE(result.subButtons.empty());
}

// ============================================================
// Edge: multiple labels on a button (labels array)
// ============================================================

TEST_CASE("parseNewgameChoice: button with labels array uses first", "[CharacterSelect]")
{
    auto msg = makeSpeciesMsg();
    msg["main-items"]["buttons"][0].erase("label");
    msg["main-items"]["buttons"][0]["labels"] = json::array({"a - Human", "  (recommended)"});

    auto result = parseNewgameChoice(msg);

    REQUIRE(result.isValid == true);
    REQUIRE(result.mainButtons[0].label == "a - Human");
}

TEST_CASE("parseNewgameChoice: button missing hotkey gets null char", "[CharacterSelect]")
{
    auto msg = makeSpeciesMsg();
    msg["main-items"]["buttons"][0].erase("hotkey");

    auto result = parseNewgameChoice(msg);

    REQUIRE(result.isValid == true);
    REQUIRE(result.mainButtons[0].hotkey == '\0');
}

TEST_CASE("parseNewgameChoice: button missing label gets empty string", "[CharacterSelect]")
{
    auto msg = makeSpeciesMsg();
    msg["main-items"]["buttons"][0].erase("label");

    auto result = parseNewgameChoice(msg);

    REQUIRE(result.isValid == true);
    REQUIRE(result.mainButtons[0].label == "");
}

// ============================================================
// Phase: None -> bool conversion convenience
// ============================================================

TEST_CASE("NewgameChoice: valid implies phase != None", "[CharacterSelect]")
{
    auto result = parseNewgameChoice(makeSpeciesMsg());
    REQUIRE(result.isValid == true);
    REQUIRE(result.phase != CharSelectPhase::None);
}
