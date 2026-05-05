#include <catch2/catch_all.hpp>
#include "InputModeTracker.hpp"

TEST_CASE("InputModeTracker default mode is NORMAL", "[InputModeTracker]")
{
    InputModeTracker tracker;
    REQUIRE(tracker.currentMode() == InputModeTracker::NORMAL);
    REQUIRE(tracker.isGameplayMode());
}

TEST_CASE("InputModeTracker parses input_mode message", "[InputModeTracker]")
{
    InputModeTracker tracker;

    json msg = {{"msg", "input_mode"}, {"mode", 7}}; // PROMPT
    tracker.handleMessage(msg);
    REQUIRE(tracker.currentMode() == 7);
    REQUIRE_FALSE(tracker.isGameplayMode());
}

TEST_CASE("InputModeTracker transitions back to NORMAL", "[InputModeTracker]")
{
    InputModeTracker tracker;

    // Enter PROMPT
    tracker.handleMessage({{"msg", "input_mode"}, {"mode", 7}});
    REQUIRE_FALSE(tracker.isGameplayMode());

    // Return to NORMAL
    tracker.handleMessage({{"msg", "input_mode"}, {"mode", 0}});
    REQUIRE(tracker.currentMode() == 0);
    REQUIRE(tracker.isGameplayMode());
}

TEST_CASE("InputModeTracker isGameplayMode for all modes", "[InputModeTracker]")
{
    InputModeTracker tracker;

    // NORMAL = gameplay
    tracker.handleMessage({{"msg", "input_mode"}, {"mode", 0}});
    REQUIRE(tracker.isGameplayMode());

    // COMMAND = gameplay (targeting)
    tracker.handleMessage({{"msg", "input_mode"}, {"mode", 1}});
    REQUIRE(tracker.isGameplayMode());

    // TARGET = gameplay
    tracker.handleMessage({{"msg", "input_mode"}, {"mode", 2}});
    REQUIRE(tracker.isGameplayMode());

    // MORE = NOT gameplay
    tracker.handleMessage({{"msg", "input_mode"}, {"mode", 5}});
    REQUIRE_FALSE(tracker.isGameplayMode());

    // PROMPT = NOT gameplay
    tracker.handleMessage({{"msg", "input_mode"}, {"mode", 7}});
    REQUIRE_FALSE(tracker.isGameplayMode());

    // YESNO = NOT gameplay
    tracker.handleMessage({{"msg", "input_mode"}, {"mode", 8}});
    REQUIRE_FALSE(tracker.isGameplayMode());
}

TEST_CASE("InputModeTracker ignores non-input_mode messages", "[InputModeTracker]")
{
    InputModeTracker tracker;

    tracker.handleMessage({{"msg", "player"}, {"hp", 50}});
    REQUIRE(tracker.currentMode() == InputModeTracker::NORMAL);
}
