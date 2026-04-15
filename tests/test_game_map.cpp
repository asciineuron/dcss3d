#include <catch2/catch_all.hpp>
#include "GameMap.hpp"

TEST_CASE("MapType enum has expected values", "[GameMap]")
{
    REQUIRE(static_cast<int>(MapType::Wall) == 0);
    REQUIRE(static_cast<int>(MapType::Floor) == 1);
    REQUIRE(static_cast<int>(MapType::Unexplored) == 2);
    REQUIRE(static_cast<int>(MapType::Other) == 3);
}

TEST_CASE("Tile default constructor sets Other type", "[GameMap]")
{
    Tile tile;
    REQUIRE(tile.type() == MapType::Other);
}

TEST_CASE("Tile constructor sets specified type", "[GameMap]")
{
    Tile tile(MapType::Floor);
    REQUIRE(tile.type() == MapType::Floor);
}

TEST_CASE("Pos2 supports comparison operators", "[GameMap]")
{
    Pos2<int> p1{1, 2};
    Pos2<int> p2{1, 2};
    Pos2<int> p3{2, 3};
    
    REQUIRE(p1 == p2);
    REQUIRE(p1 != p3);
}
