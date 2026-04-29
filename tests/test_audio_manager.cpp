#include <catch2/catch_all.hpp>
#include "AudioManager.hpp"
#include "Turn.hpp"
#include <nlohmann/json.hpp>

using json = nlohmann::json;

TEST_CASE("AudioManager::onPlayerAction triggers footfall for MoveTurn", "[AudioManager]")
{
    AudioManager audio;
    
    // RED: this should fail since AudioManager doesn't exist yet
    MoveTurn move(North);
    audio.onPlayerAction(move);
    
    auto& sounds = audio.triggeredSounds();
    REQUIRE(sounds.size() == 1);
    REQUIRE(sounds[0] == "footfall");
}

TEST_CASE("AudioManager::onPlayerAction triggers footfall for all move directions", "[AudioManager]")
{
    AudioManager audio;
    
    audio.onPlayerAction(MoveTurn(North));
    audio.onPlayerAction(MoveTurn(South));
    audio.onPlayerAction(MoveTurn(East));
    audio.onPlayerAction(MoveTurn(West));
    audio.onPlayerAction(MoveTurn(NorthEast));
    audio.onPlayerAction(MoveTurn(NorthWest));
    audio.onPlayerAction(MoveTurn(SouthEast));
    audio.onPlayerAction(MoveTurn(SouthWest));
    audio.onPlayerAction(MoveTurn(Here));  // waiting — possibly no footfall, but design allows it
    
    auto& sounds = audio.triggeredSounds();
    REQUIRE(sounds.size() == 9);
    for (const auto& s : sounds) {
        REQUIRE(s == "footfall");
    }
}

TEST_CASE("AudioManager::onPlayerAction does NOT trigger sound for non-MoveTurn", "[AudioManager]")
{
    AudioManager audio;
    
    InputTurn inputTurn(SDL_SCANCODE_RETURN);
    audio.onPlayerAction(inputTurn);
    
    REQUIRE(audio.triggeredSounds().empty());
}

TEST_CASE("AudioManager::handleMessage does nothing for unrelated messages", "[AudioManager]")
{
    AudioManager audio;
    
    json msg = { {"msg", "player"}, {"hp", 42} };
    audio.handleMessage(msg);
    
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

TEST_CASE("AudioManager::clearTriggeredSounds empties the sound list", "[AudioManager]")
{
    AudioManager audio;
    
    audio.onPlayerAction(MoveTurn(North));
    REQUIRE(audio.triggeredSounds().size() == 1);
    
    audio.clearTriggeredSounds();
    REQUIRE(audio.triggeredSounds().empty());
}
