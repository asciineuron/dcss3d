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

// --- Death detection (hp <= 0) ---

TEST_CASE("Player triggers game_over on death (hp drops to zero or below)", "[Player][AudioManager]")
{
    AudioManager audio;
    Player player;
    player.setAudioManager(&audio);

    json msg1 = { {"msg", "player"}, {"name", "DoomedHero"}, {"hp", 10}, {"hp_max", 25}, {"time", 100} };
    player.handleMessage(msg1);

    // Lethal blow: hp goes to -7
    json msg2 = { {"msg", "player"}, {"hp", -7}, {"time", 200} };
    player.handleMessage(msg2);

    auto& sounds = audio.triggeredSounds();
    REQUIRE(sounds.size() == 1);
    REQUIRE(sounds[0] == "game_over");
}

TEST_CASE("Player triggers game_over not damage when one-shot killed", "[Player][AudioManager]")
{
    AudioManager audio;
    Player player;
    player.setAudioManager(&audio);

    json msg1 = { {"msg", "player"}, {"name", "FragileHero"}, {"hp", 25}, {"hp_max", 25}, {"time", 100} };
    player.handleMessage(msg1);

    // Killed in one hit: hp goes to 0
    json msg2 = { {"msg", "player"}, {"hp", 0}, {"time", 200} };
    player.handleMessage(msg2);

    auto& sounds = audio.triggeredSounds();
    REQUIRE(sounds.size() == 1);
    REQUIRE(sounds[0] == "game_over");
}

TEST_CASE("Player does NOT trigger game_over twice for consecutive death messages", "[Player][AudioManager]")
{
    AudioManager audio;
    Player player;
    player.setAudioManager(&audio);

    json msg1 = { {"msg", "player"}, {"name", "DeadHero"}, {"hp", 5}, {"hp_max", 25}, {"time", 100} };
    player.handleMessage(msg1);

    // Death
    json msg2 = { {"msg", "player"}, {"hp", -2}, {"time", 200} };
    player.handleMessage(msg2);

    // Another death message (e.g., poison tick on corpse)
    json msg3 = { {"msg", "player"}, {"hp", -3}, {"time", 300} };
    player.handleMessage(msg3);

    auto& sounds = audio.triggeredSounds();
    // Only one game_over, not a second one plus damage
    REQUIRE(sounds.size() == 1);
    REQUIRE(sounds[0] == "game_over");
}

// --- Level up detection ---

TEST_CASE("Player detects XL increase and triggers level_up sound", "[Player][AudioManager]")
{
    AudioManager audio;
    Player player;
    player.setAudioManager(&audio);

    json msg1 = { {"msg", "player"}, {"name", "Hero"}, {"xl", 3}, {"time", 100} };
    player.handleMessage(msg1);
    REQUIRE(audio.triggeredSounds().empty());

    json msg2 = { {"msg", "player"}, {"xl", 4}, {"time", 200} };
    player.handleMessage(msg2);

    auto& sounds = audio.triggeredSounds();
    REQUIRE(sounds.size() == 1);
    REQUIRE(sounds[0] == "level_up");
}

TEST_CASE("Player does NOT trigger level_up on first player message", "[Player][AudioManager]")
{
    AudioManager audio;
    Player player;
    player.setAudioManager(&audio);

    json msg = { {"msg", "player"}, {"name", "NewHero"}, {"xl", 1}, {"time", 100} };
    player.handleMessage(msg);

    REQUIRE(audio.triggeredSounds().empty());
}

TEST_CASE("Player does NOT trigger level_up on same XL", "[Player][AudioManager]")
{
    AudioManager audio;
    Player player;
    player.setAudioManager(&audio);

    json msg1 = { {"msg", "player"}, {"name", "Hero"}, {"xl", 5}, {"time", 100} };
    player.handleMessage(msg1);

    json msg2 = { {"msg", "player"}, {"xl", 5}, {"time", 200} };
    player.handleMessage(msg2);

    REQUIRE(audio.triggeredSounds().empty());
}

TEST_CASE("Player triggers both damage and level_up in same message", "[Player][AudioManager]")
{
    AudioManager audio;
    Player player;
    player.setAudioManager(&audio);

    json msg1 = { {"msg", "player"}, {"name", "Hero"}, {"hp", 50}, {"xl", 3}, {"time", 100} };
    player.handleMessage(msg1);

    // Both damage and level up in one message (unlikely in practice, but valid)
    json msg2 = { {"msg", "player"}, {"hp", 40}, {"xl", 4}, {"time", 200} };
    player.handleMessage(msg2);

    auto& sounds = audio.triggeredSounds();
    REQUIRE(sounds.size() == 2);
    // damage is checked first, level_up second (order matters for consistency)
    REQUIRE(sounds[0] == "damage");
    REQUIRE(sounds[1] == "level_up");
}

// --- game_ended detection ---

TEST_CASE("AudioManager::handleMessage triggers game_over on game_ended", "[AudioManager]")
{
    AudioManager audio;

    json msg = { {"msg", "game_ended"}, {"reason", "quit"} };
    audio.handleMessage(msg);

    auto& sounds = audio.triggeredSounds();
    REQUIRE(sounds.size() == 1);
    REQUIRE(sounds[0] == "game_over");
}

TEST_CASE("AudioManager::handleMessage triggers game_over regardless of reason", "[AudioManager]")
{
    AudioManager audio;

    audio.handleMessage({ {"msg", "game_ended"}, {"reason", "quit"} });
    audio.handleMessage({ {"msg", "game_ended"}, {"reason", "crash"} });

    auto& sounds = audio.triggeredSounds();
    REQUIRE(sounds.size() == 2);
    REQUIRE(sounds[0] == "game_over");
    REQUIRE(sounds[1] == "game_over");
}

TEST_CASE("AudioManager::handleMessage does NOT trigger game_over for non-game_ended", "[AudioManager]")
{
    AudioManager audio;

    json msg = { {"msg", "player"}, {"hp", 10} };
    audio.handleMessage(msg);

    REQUIRE(audio.triggeredSounds().empty());
}
