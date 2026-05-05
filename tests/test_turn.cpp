#include <catch2/catch_all.hpp>
#include "Turn.hpp"

TEST_CASE("TextTurn produces correct JSON for >", "[Turn][TextTurn]")
{
    TextTurn turn(">");
    json msg = turn.asMessage();
    REQUIRE(msg["msg"] == "input");
    REQUIRE(msg["text"] == ">");
}

TEST_CASE("TextTurn produces correct JSON for <", "[Turn][TextTurn]")
{
    TextTurn turn("<");
    json msg = turn.asMessage();
    REQUIRE(msg["msg"] == "input");
    REQUIRE(msg["text"] == "<");
}

TEST_CASE("TextTurn produces correct JSON for arbitrary text", "[Turn][TextTurn]")
{
    TextTurn turn("hello");
    json msg = turn.asMessage();
    REQUIRE(msg["msg"] == "input");
    REQUIRE(msg["text"] == "hello");
}

TEST_CASE("TextTurn empty string", "[Turn][TextTurn]")
{
    TextTurn turn("");
    json msg = turn.asMessage();
    REQUIRE(msg["msg"] == "input");
    REQUIRE(msg["text"] == "");
}
