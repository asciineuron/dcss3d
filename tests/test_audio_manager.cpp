#include <catch2/catch_all.hpp>
#include "AudioManager.hpp"
#include "PlayerState.hpp"
#include "Turn.hpp"
#include <nlohmann/json.hpp>

using json = nlohmann::json;

TEST_CASE("MoveTurn::playSound triggers footfall", "[AudioManager]")
{
    AudioManager audio;
    MoveTurn move(North);
    move.playSound(audio);

    auto& sounds = audio.triggeredSounds();
    REQUIRE(sounds.size() == 1);
    REQUIRE(sounds[0] == "footfall");
}

TEST_CASE("MoveTurn::playSound triggers footfall for all directions", "[AudioManager]")
{
    AudioManager audio;
    MoveTurn(North).playSound(audio);
    MoveTurn(South).playSound(audio);
    MoveTurn(East).playSound(audio);
    MoveTurn(West).playSound(audio);
    MoveTurn(NorthEast).playSound(audio);
    MoveTurn(NorthWest).playSound(audio);
    MoveTurn(SouthEast).playSound(audio);
    MoveTurn(SouthWest).playSound(audio);
    MoveTurn(Here).playSound(audio);

    auto& sounds = audio.triggeredSounds();
    REQUIRE(sounds.size() == 9);
    for (const auto& s : sounds) {
        REQUIRE(s == "footfall");
    }
}

TEST_CASE("InputTurn::playSound does NOT trigger any sound", "[AudioManager]")
{
    AudioManager audio;
    InputTurn inputTurn(SDL_SCANCODE_RETURN);
    inputTurn.playSound(audio);

    REQUIRE(audio.triggeredSounds().empty());
}

TEST_CASE("AudioManager::handleMessage triggers map_update for map messages", "[AudioManager]")
{
    AudioManager audio;
    json msg = { {"msg", "map"}, {"cells", json::array()} };
    audio.handleMessage(msg);

    auto& sounds = audio.triggeredSounds();
    REQUIRE(sounds.size() == 1);
    REQUIRE(sounds[0] == "map_update");
}

TEST_CASE("AudioManager::handleMessage does NOT trigger map_update for non-map messages", "[AudioManager]")
{
    AudioManager audio;
    json msg = { {"msg", "login"} };
    audio.handleMessage(msg);

    REQUIRE(audio.triggeredSounds().empty());
}

TEST_CASE("AudioManager::handleMessage does nothing for unrelated messages", "[AudioManager]")
{
    AudioManager audio;
    json msg = { {"msg", "player"}, {"hp", 42} };
    audio.handleMessage(msg);

    REQUIRE(audio.triggeredSounds().empty());
}

TEST_CASE("AudioManager::clearTriggeredSounds empties the sound list", "[AudioManager]")
{
    AudioManager audio;
    MoveTurn(North).playSound(audio);
    REQUIRE(audio.triggeredSounds().size() == 1);

    audio.clearTriggeredSounds();
    REQUIRE(audio.triggeredSounds().empty());
}

TEST_CASE("triggerSound works for arbitrary sound names", "[AudioManager]")
{
    AudioManager audio;
    audio.triggerSound("explosion");

    auto& sounds = audio.triggeredSounds();
    REQUIRE(sounds.size() == 1);
    REQUIRE(sounds[0] == "explosion");
}

// --- Player damage detection ---

TEST_CASE("Player detects HP decrease and triggers damage sound", "[Player][AudioManager]")
{
    AudioManager audio;
    Player player;
    player.setAudioManager(&audio);

    // First message: establish HP at 50
    json msg1 = { {"msg", "player"}, {"name", "TestHero"}, {"hp", 50}, {"hp_max", 60}, {"time", 100} };
    player.handleMessage(msg1);
    REQUIRE(audio.triggeredSounds().empty());

    // Second message: HP dropped to 40 → damage sound
    json msg2 = { {"msg", "player"}, {"hp", 40}, {"time", 200} };
    player.handleMessage(msg2);

    auto& sounds = audio.triggeredSounds();
    REQUIRE(sounds.size() == 1);
    REQUIRE(sounds[0] == "damage");
}

TEST_CASE("Player does NOT trigger damage on first player message", "[Player][AudioManager]")
{
    AudioManager audio;
    Player player;
    player.setAudioManager(&audio);

    // First message has hp=50 but no previous state, so no damage sound
    json msg = { {"msg", "player"}, {"name", "NewHero"}, {"hp", 50}, {"hp_max", 60}, {"time", 100} };
    player.handleMessage(msg);

    REQUIRE(audio.triggeredSounds().empty());
}

TEST_CASE("Player does NOT trigger damage on same HP", "[Player][AudioManager]")
{
    AudioManager audio;
    Player player;
    player.setAudioManager(&audio);

    json msg1 = { {"msg", "player"}, {"name", "SameHero"}, {"hp", 50}, {"hp_max", 60}, {"time", 100} };
    player.handleMessage(msg1);

    json msg2 = { {"msg", "player"}, {"hp", 50}, {"time", 200} };
    player.handleMessage(msg2);

    REQUIRE(audio.triggeredSounds().empty());
}

TEST_CASE("Player does NOT trigger damage on HP increase (healing)", "[Player][AudioManager]")
{
    AudioManager audio;
    Player player;
    player.setAudioManager(&audio);

    json msg1 = { {"msg", "player"}, {"name", "HealHero"}, {"hp", 30}, {"hp_max", 60}, {"time", 100} };
    player.handleMessage(msg1);

    json msg2 = { {"msg", "player"}, {"hp", 50}, {"time", 200} };
    player.handleMessage(msg2);

    REQUIRE(audio.triggeredSounds().empty());
}

TEST_CASE("Player damage detection works across multiple decreases", "[Player][AudioManager]")
{
    AudioManager audio;
    Player player;
    player.setAudioManager(&audio);

    json init = { {"msg", "player"}, {"name", "ToughHero"}, {"hp", 100}, {"hp_max", 100}, {"time", 100} };
    player.handleMessage(init);

    json hit1 = { {"msg", "player"}, {"hp", 80}, {"time", 200} };
    player.handleMessage(hit1);

    json hit2 = { {"msg", "player"}, {"hp", 55}, {"time", 300} };
    player.handleMessage(hit2);

    auto& sounds = audio.triggeredSounds();
    REQUIRE(sounds.size() == 2);
    REQUIRE(sounds[0] == "damage");
    REQUIRE(sounds[1] == "damage");
}
