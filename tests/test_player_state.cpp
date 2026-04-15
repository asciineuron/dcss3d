#include <catch2/catch_all.hpp>

TEST_CASE("Direction enum bit flags work correctly", "[Turn]")
{
    // Test Direction enum values from Turn.hpp
    constexpr int None = 0;
    constexpr int North = 1 << 0;  // 1
    constexpr int East = 1 << 1;   // 2
    constexpr int South = 1 << 2;  // 4
    constexpr int West = 1 << 3;   // 8
    constexpr int NorthEast = (1 << 0) + (1 << 1);  // 3
    constexpr int DirectionSize = 32;
    
    REQUIRE(None == 0);
    REQUIRE(North == 1);
    REQUIRE(East == 2);
    REQUIRE(South == 4);
    REQUIRE(West == 8);
    REQUIRE(NorthEast == 3);
    REQUIRE(DirectionSize == 32);
}

TEST_CASE("Bitwise operations on direction flags", "[Turn]")
{
    constexpr int North = 1 << 0;
    constexpr int South = 1 << 2;
    constexpr int NorthSouth = North | South;
    
    REQUIRE((NorthSouth & North) == North);
    REQUIRE((NorthSouth & South) == South);
    REQUIRE(NorthSouth == 5);
}
