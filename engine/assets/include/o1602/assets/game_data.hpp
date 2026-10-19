// SPDX-License-Identifier: GPL-3.0-or-later
//
// Numbers of the original's data that the engine relies on: ware numbers, HAUS Kind and
// HAUS_PRODTYP Kind values, figure numbers, the base prices and the buildings the computer players
// name by Id. They are defined here once instead of in every file that needs them, so that a later
// data loader for mods (ADR 0007) has one place to replace and a renumbered ware cannot be missed
// in one file and kept in another. The simulation re-exports them (sim/game_data.hpp); assets,
// render and the app use them from here.
//
// The names follow the symbols of haeuser.cod and figuren.cod (assets/cod_predefined.cpp).
#pragma once

#include <array>
#include <cstdint>

namespace o1602::assets {

/// Ware numbers (HAUS Ware, Kontor stock, ship holds).
namespace ware {
inline constexpr std::uint8_t kNone = 0;  ///< NOWARE
inline constexpr std::uint8_t kAll = 1;   ///< ALLWARE: markets and Kontors hold every good
inline constexpr std::uint8_t kOre = 2;   ///< EISENERZ
inline constexpr std::uint8_t kGold = 3;
inline constexpr std::uint8_t kWool = 4;
inline constexpr std::uint8_t kSugar = 5;
inline constexpr std::uint8_t kTobacco = 6;
inline constexpr std::uint8_t kMeat = 7;
inline constexpr std::uint8_t kGrain = 8;
inline constexpr std::uint8_t kFlour = 9;
inline constexpr std::uint8_t kIron = 10;
inline constexpr std::uint8_t kSwords = 11;
inline constexpr std::uint8_t kMuskets = 12;
inline constexpr std::uint8_t kCannons = 13;
inline constexpr std::uint8_t kFood = 14;
inline constexpr std::uint8_t kTobaccoGoods = 15;
inline constexpr std::uint8_t kSpices = 16;
inline constexpr std::uint8_t kCocoa = 17;
inline constexpr std::uint8_t kAlcohol = 18;
inline constexpr std::uint8_t kCloth = 19;
inline constexpr std::uint8_t kClothes = 20;
inline constexpr std::uint8_t kJewellery = 21;
inline constexpr std::uint8_t kTools = 22;
inline constexpr std::uint8_t kWood = 23;
inline constexpr std::uint8_t kBricks = 24;
inline constexpr std::uint8_t kFirstSoldier = 25;  ///< wSOLDAT1: soldiers in a ship's hold ...
inline constexpr std::uint8_t kLastSoldier = 44;   ///< ... up to wPIONIER4
/// A unit in a hold is the ware of its figure plus this (SOLDAT1 = 1 is wSOLDAT1 = 25).
inline constexpr std::uint8_t kSoldierOffset = kFirstSoldier - 1;
inline constexpr std::uint8_t kCornField = 45;
inline constexpr std::uint8_t kTobaccoField = 46;
inline constexpr std::uint8_t kSpiceField = 47;
inline constexpr std::uint8_t kSugarField = 48;
inline constexpr std::uint8_t kCottonField = 49;
inline constexpr std::uint8_t kWineField = 50;
inline constexpr std::uint8_t kCocoaField = 51;
inline constexpr std::uint8_t kGrass = 52;
inline constexpr std::uint8_t kTree = 53;
inline constexpr std::uint8_t kStone = 54;        ///< STEINE: the quarry's rock
inline constexpr std::uint8_t kOreMountain = 55;  ///< ERZE: the ware of ore mountains
inline constexpr std::uint8_t kGame = 56;
inline constexpr std::uint8_t kFish = 57;
inline constexpr std::uint8_t kTreasure = 58;  ///< SCHATZ

/// Goods a Kontor stores and ships trade: EISENERZ to ZIEGEL.
inline constexpr std::uint8_t kFirstTrade = kOre;
inline constexpr std::uint8_t kLastTrade = kBricks;
}  // namespace ware

/// A good a Kontor stores and ships trade.
[[nodiscard]] constexpr bool is_trade_ware(std::uint32_t w) noexcept {
    return w >= ware::kFirstTrade && w <= ware::kLastTrade;
}

/// Soldiers carried in a ship's hold (one slot per unit type and level).
[[nodiscard]] constexpr bool is_soldier_ware(std::uint32_t w) noexcept {
    return w >= ware::kFirstSoldier && w <= ware::kLastSoldier;
}

/// Base prices of the wares per ton (the original's table at 0x0049AE50): the default prices of a
/// Kontor (sell 50 %, buy 75 %) and what the free trader pays. Indexed by ware number.
inline constexpr std::array<std::uint16_t, ware::kLastTrade + 1> kBasePrices = {
    0, 0, 90, 700, 24, 30, 35, 10, 5, 10, 130, 180, 250, 500, 26, 100, 60, 50, 80, 50, 200, 900, 140, 30, 45};

/// Base price of a ware per ton (0 for wares that are not traded).
[[nodiscard]] constexpr std::uint16_t base_price(std::uint8_t w) noexcept {
    return w < kBasePrices.size() ? kBasePrices[w] : 0;
}

/// HAUS Kind values (record +0x04).
namespace kind {
inline constexpr std::int32_t kRoad = 1;
inline constexpr std::int32_t kGate = 3;
inline constexpr std::int32_t kWall = 4;
inline constexpr std::int32_t kWallBeach = 5;
inline constexpr std::int32_t kTower = 6;
inline constexpr std::int32_t kTowerBeach = 7;
inline constexpr std::int32_t kForest = 10;
inline constexpr std::int32_t kGround = 11;
inline constexpr std::int32_t kRuin = 12;
inline constexpr std::int32_t kSquare = 13;
inline constexpr std::int32_t kBuilding = 14;
inline constexpr std::int32_t kRock = 15;
inline constexpr std::int32_t kRiver = 16;
inline constexpr std::int32_t kRiverCorner = 17;
inline constexpr std::int32_t kBridge = 18;
inline constexpr std::int32_t kSea = 19;
inline constexpr std::int32_t kSurf = 20;
inline constexpr std::int32_t kSurfCorner = 21;
inline constexpr std::int32_t kRiverMouth = 22;  ///< MUENDUNG
inline constexpr std::int32_t kBeach = 23;
inline constexpr std::int32_t kBeachMouth = 24;
inline constexpr std::int32_t kBeachOuter = 25;
inline constexpr std::int32_t kBeachInner = 26;
inline constexpr std::int32_t kBeachVariant = 27;
inline constexpr std::int32_t kBeachHouse = 28;
inline constexpr std::int32_t kBeachRuin = 29;
inline constexpr std::int32_t kPier = 30;
inline constexpr std::int32_t kSlope = 31;
inline constexpr std::int32_t kSlopeCorner = 32;
inline constexpr std::int32_t kSlopeSpring = 33;
inline constexpr std::int32_t kMine = 34;
inline constexpr std::int32_t kHq = 35;
inline constexpr std::int32_t kHarbour = 36;
inline constexpr std::int32_t kMill = 37;
}  // namespace kind

/// HAUS_PRODTYP Kind values (what a building does).
namespace prod {
inline constexpr std::int32_t kWorkshop = 1;      ///< HANDWERK
inline constexpr std::int32_t kFarm = 2;          ///< PLANTAGE
inline constexpr std::int32_t kMine = 3;          ///< BERGWERK
inline constexpr std::int32_t kPasture = 4;       ///< WEIDETIER
inline constexpr std::int32_t kHunter = 5;        ///< JAGDHAUS
inline constexpr std::int32_t kFisher = 6;        ///< FISCHEREI
inline constexpr std::int32_t kMarket = 7;        ///< MARKT
inline constexpr std::int32_t kKontor = 8;        ///< KONTOR
inline constexpr std::int32_t kRipe = 9;          ///< ROHSTOFF: ripe, can be harvested
inline constexpr std::int32_t kGrowing = 10;      ///< ROHSTWACHS: growing (or withered)
inline constexpr std::int32_t kQuarry = 11;       ///< STEINBRUCH
inline constexpr std::int32_t kOreMountain = 12;  ///< ROHSTERZ
inline constexpr std::int32_t kHouse = 13;        ///< WOHNUNG
inline constexpr std::int32_t kShipyard = 14;     ///< WERFT
inline constexpr std::int32_t kMilitary = 15;     ///< MILITAR
inline constexpr std::int32_t kTower = 16;        ///< WACHTURM
inline constexpr std::int32_t kWirt = 17;
inline constexpr std::int32_t kKapelle = 18;
inline constexpr std::int32_t kKirche = 19;
inline constexpr std::int32_t kBadehaus = 20;
inline constexpr std::int32_t kTheater = 21;
inline constexpr std::int32_t kKlinik = 22;  ///< the doctor
inline constexpr std::int32_t kSchule = 23;
inline constexpr std::int32_t kHochschule = 24;
inline constexpr std::int32_t kGalgen = 25;
inline constexpr std::int32_t kBrunnen = 26;  ///< the fire brigade
inline constexpr std::int32_t kSchloss = 27;
inline constexpr std::int32_t kDenkmal = 28;
inline constexpr std::int32_t kTriumph = 29;
inline constexpr std::int32_t kHaupt = 30;
inline constexpr std::int32_t kPirateHouse = 31;  ///< PIRATWOHN
inline constexpr std::int32_t kWall = 32;         ///< pMAUER
/// The public buildings, WIRT to SCHLOSS.
inline constexpr std::int32_t kPublicFirst = kWirt;
inline constexpr std::int32_t kPublicLast = kSchloss;
}  // namespace prod

/// Figure numbers (figuren.cod).
namespace figure {
/// The families of land units, each with four levels: SOLDAT1, KAVALERIE1, MUSKETIER1, KANONIER1.
inline constexpr std::array<std::uint32_t, 4> kUnitFamilies{1, 5, 9, 13};
/// Ships the shipyards build, by type: HANDEL1, HANDEL2, KRIEG1, KRIEG2 (each followed by its
/// wreck).
inline constexpr std::array<std::uint16_t, 4> kShips{21, 23, 25, 27};
inline constexpr std::uint16_t kTrader = 29;    ///< HANDLER: the free trader's ship
inline constexpr std::uint16_t kPirate = 31;    ///< PIRAT
inline constexpr std::uint16_t kDeer = 89;      ///< HIRSCH: the northern islands' game
inline constexpr std::uint16_t kAntelope = 91;  ///< ANTILOPE: the southern islands' game
inline constexpr std::uint16_t kRobber = 92;    ///< RAEUBER
}  // namespace figure

/// FIGURE Kind values of the land units (figuren.cod FIGTYP_*).
namespace figtype {
inline constexpr std::int32_t kSwordsman = 1;  ///< FIGTYP_SCHWERT
inline constexpr std::int32_t kRider = 2;      ///< FIGTYP_KAVALERIE
inline constexpr std::int32_t kMusketeer = 3;  ///< FIGTYP_MUSKETIER
inline constexpr std::int32_t kGunner = 4;     ///< FIGTYP_KANONIER
}  // namespace figtype

/// Buildings of the base game named by Id, mostly by the computer players.
namespace id {
inline constexpr std::uint32_t kRoadStone = 20401;
inline constexpr std::uint32_t kRoadDirt = 20411;
inline constexpr std::uint32_t kSquare = 20421;
inline constexpr std::uint32_t kHouse = 20605;
inline constexpr std::uint32_t kKapelle = 20803;
inline constexpr std::uint32_t kKirche = 20805;
inline constexpr std::uint32_t kWirt = 20807;
inline constexpr std::uint32_t kMarket = 20809;
inline constexpr std::uint32_t kBrunnen = 20811;
inline constexpr std::uint32_t kBadehaus = 20813;
inline constexpr std::uint32_t kTheater = 20815;
inline constexpr std::uint32_t kKlinik = 20817;
inline constexpr std::uint32_t kHochschule = 20819;
inline constexpr std::uint32_t kSchule = 20821;
inline constexpr std::uint32_t kGalgen = 20841;
inline constexpr std::uint32_t kDenkmal = 20851;
inline constexpr std::uint32_t kGate = 20304;
inline constexpr std::uint32_t kStoneTower = 20305;
inline constexpr std::uint32_t kCastle1 = 20311;
inline constexpr std::uint32_t kCastle2 = 20313;
inline constexpr std::uint32_t kCastle3 = 20315;
inline constexpr std::uint32_t kWebstube = 20507;
inline constexpr std::uint32_t kWeberei = 20531;
inline constexpr std::uint32_t kGoldsmith = 20533;  ///< also marks the treasure entries of a world plan
inline constexpr std::uint32_t kWoodTower = 21405;
inline constexpr std::uint32_t kBridgeWood = 21601;
inline constexpr std::uint32_t kBridgeStone = 21603;
inline constexpr std::uint32_t kPier = 21073;
inline constexpr std::uint32_t kHunter = 22003;
inline constexpr std::uint32_t kKontor1 = 22101;
inline constexpr std::uint32_t kShipyardBig = 22111;
inline constexpr std::uint32_t kShipyardSmall = 22115;
inline constexpr std::uint32_t kPirateKontor = 22221;
inline constexpr std::uint32_t kPirateHouse1 = 22222;
inline constexpr std::uint32_t kPirateHouse2 = 22223;
inline constexpr std::uint32_t kPirateHouse3 = 22224;
inline constexpr std::uint32_t kPirateTower = 22241;
inline constexpr std::uint32_t kOreMine = 22401;
inline constexpr std::uint32_t kOreMineDeep = 22403;
inline constexpr std::uint32_t kGoldMine = 22405;
inline constexpr std::uint32_t kQuarry = 22407;
inline constexpr std::uint32_t kWall = 22501;
}  // namespace id

}  // namespace o1602::assets
