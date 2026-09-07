#pragma once

// Source-grounded candidate areas, not pre-approved spawn coordinates. The runtime must
// validate ground, navigation, water, interiors, clearance and collision before use.
// Vector3 is supplied by global.h/contract_data.h before including this header.
// See docs/routine-location-sources.md for the exact source context and limitations.
namespace RoutineData
{
	enum class TownId { Rhodes, Blackwater, Valentine, Strawberry, SaintDenis };
	enum class PlaceKind { Work, Shops, Leisure, Rest };
	enum Occupation : unsigned
	{
		Local = 1u, Laborer = 2u, DockWorker = 4u, LivestockHand = 8u,
		AllOccupations = Local | Laborer | DockWorker | LivestockHand
	};

	struct Location
	{
		const char* id;
		const char* name;
		TownId town;
		PlaceKind kind;
		Vector3 anchor;
		float candidateRadius; // maximum horizontal displacement from the source anchor
		float maxHeightDelta;  // reject another vertical level, even when it has navmesh
		float wanderRadius;    // fixed around the validated arrival point, not the moving ped
		int openMinute;        // AUTHOR'S outdoor visiting window, not a verified shop opening time
		int closeMinute;       // equal endpoints mean all day; crossing midnight is supported
		unsigned occupations;
		bool enabled;
		const char* source;
	};

	// All enabled locations are public exterior candidate areas. Shops means time near a
	// frontage/market, Leisure means ambient loitering; neither promises a shop transaction,
	// alcohol service, a working minigame, a theatre performance, or access to an interior.
	inline const Location kLocations[] = {
		{ "rhd_stables", "Stable yard", TownId::Rhodes, PlaceKind::Work,
			{ 1416.932f, -1304.703f, 78.61515f }, 12.0f, 2.5f, 26.0f, 360, 1080, Local | Laborer | LivestockHand, true, "rhodes.c:874" },
		{ "rhd_shop_street", "Main shop street", TownId::Rhodes, PlaceKind::Shops,
			{ 1327.387f, -1303.313f, 75.4175f }, 8.0f, 2.5f, 22.0f, 480, 1200, AllOccupations, true, "feud1.c:65363" },
		{ "rhd_market", "Butcher frontage", TownId::Rhodes, PlaceKind::Shops,
			{ 1296.911f, -1279.125f, 76.30206f }, 8.0f, 2.5f, 18.0f, 480, 1200, AllOccupations, true, "rhodes.c:890" },
		{ "rhd_saloon", "Saloon approach", TownId::Rhodes, PlaceKind::Leisure,
			{ 1360.207f, -1378.318f, 78.3629f }, 7.0f, 2.5f, 20.0f, 1020, 120, AllOccupations, true, "rcm_slave_catcher1.c:7779" },
		{ "rhd_public", "Newspaper corner", TownId::Rhodes, PlaceKind::Rest,
			{ 1332.786f, -1299.38f, 77.354f }, 9.0f, 2.5f, 20.0f, 0, 0, AllOccupations, true, "rhodes.c:851" },

		{ "blw_stables", "Stable frontage", TownId::Blackwater, PlaceKind::Work,
			{ -856.5364f, -1366.288f, 43.215f }, 8.0f, 2.5f, 24.0f, 360, 1080, Local | Laborer | LivestockHand, true, "blackwater.c:774" },
		{ "blw_barber", "Barber frontage", TownId::Blackwater, PlaceKind::Shops,
			{ -807.6548f, -1366.734f, 43.68237f }, 7.0f, 2.5f, 20.0f, 480, 1200, AllOccupations, true, "blackwater.c:814" },
		{ "blw_general", "General store frontage", TownId::Blackwater, PlaceKind::Shops,
			{ -792.534f, -1324.511f, 44.38398f }, 7.0f, 2.5f, 20.0f, 480, 1200, AllOccupations, true, "blackwater.c:760,1084" },
		{ "blw_saloon", "Saloon street", TownId::Blackwater, PlaceKind::Leisure,
			{ -806.585f, -1330.295f, 43.60916f }, 8.0f, 2.5f, 20.0f, 1020, 120, AllOccupations, true, "blackwater.c:778" },
		{ "blw_public", "Western campfire area", TownId::Blackwater, PlaceKind::Rest,
			{ -934.7934f, -1374.338f, 48.44641f }, 9.0f, 2.5f, 20.0f, 0, 0, AllOccupations, true, "blackwater.c:830" },

		{ "val_auction_south", "South auction yard", TownId::Valentine, PlaceKind::Work,
			{ -257.2065f, 634.5588f, 114.0751f }, 10.0f, 2.5f, 26.0f, 360, 1080, Laborer | LivestockHand, true, "valentine.c:822" },
		{ "val_auction_north", "North auction yard", TownId::Valentine, PlaceKind::Work,
			{ -221.2732f, 673.6355f, 114.0751f }, 10.0f, 2.5f, 26.0f, 360, 1080, Laborer | LivestockHand, true, "valentine.c:823" },
		{ "val_stables", "Stable frontage", TownId::Valentine, PlaceKind::Work,
			{ -361.3393f, 787.48f, 116.4301f }, 8.0f, 2.5f, 24.0f, 360, 1080, Local | Laborer | LivestockHand, true, "valentine.c:827" },
		{ "val_market", "Butcher frontage", TownId::Valentine, PlaceKind::Shops,
			{ -339.5029f, 767.1139f, 116.61f }, 8.0f, 2.5f, 22.0f, 480, 1200, AllOccupations, true, "valentine.c:849" },
		{ "val_saloon", "Smithfield's frontage", TownId::Valentine, PlaceKind::Leisure,
			{ -305.1229f, 797.785f, 117.9535f }, 7.0f, 2.5f, 20.0f, 1020, 120, AllOccupations, true, "saloon1.c:52097-52100" },
		{ "val_public", "Newspaper corner", TownId::Valentine, PlaceKind::Rest,
			{ -269.754f, 785.441f, 118.489f }, 9.0f, 2.5f, 22.0f, 0, 0, AllOccupations, true, "valentine.c:832" },

		{ "str_horse_pen", "Horse pens", TownId::Strawberry, PlaceKind::Work,
			{ -1792.105f, -567.4036f, 156.8778f }, 10.0f, 2.5f, 26.0f, 360, 1080, Local | Laborer | LivestockHand, true, "strawberry.c:685" },
		{ "str_market", "Butcher frontage", TownId::Strawberry, PlaceKind::Shops,
			{ -1753.4f, -392.8f, 156.3f }, 8.0f, 2.5f, 18.0f, 480, 1200, AllOccupations, true, "strawberry.c:682" },
		{ "str_general", "General store frontage", TownId::Strawberry, PlaceKind::Shops,
			{ -1796.973f, -382.4901f, 160.8563f }, 7.0f, 2.5f, 18.0f, 480, 1200, AllOccupations, true, "strawberry.c:1321" },
		{ "str_hotel", "Hotel frontage", TownId::Strawberry, PlaceKind::Leisure,
			{ -1807.855f, -373.9421f, 161.8663f }, 7.0f, 2.5f, 18.0f, 1020, 120, AllOccupations, true, "spd_mayorofstrawberry1.c:3321" },
		{ "str_public", "Newspaper corner", TownId::Strawberry, PlaceKind::Rest,
			{ -1773.417f, -394.25f, 157.091f }, 9.0f, 2.5f, 20.0f, 0, 0, AllOccupations, true, "strawberry.c:691" },

		{ "sd_docks", "Dock approach", TownId::SaintDenis, PlaceKind::Work,
			{ 2748.86f, -1445.835f, 44.9741f }, 8.0f, 2.5f, 22.0f, 360, 1080, Laborer | DockWorker, true, "rcm_for_my_art4.c:10602" },
		{ "sd_market", "French market", TownId::SaintDenis, PlaceKind::Work,
			{ 2836.302f, -1305.196f, 46.90116f }, 9.0f, 2.5f, 24.0f, 360, 1080, Local | Laborer, true, "saintdenis.c:820" },
		{ "sd_barber", "Barber frontage", TownId::SaintDenis, PlaceKind::Shops,
			{ 2661.405f, -1180.077f, 53.38316f }, 7.0f, 2.5f, 20.0f, 480, 1200, AllOccupations, true, "saintdenis.c:812" },
		{ "sd_general", "General store frontage", TownId::SaintDenis, PlaceKind::Shops,
			{ 2831.25f, -1320.236f, 46.7499f }, 7.0f, 2.5f, 20.0f, 480, 1200, AllOccupations, true, "saintdenis.c:807" },
		{ "sd_saloon", "Slum saloon street", TownId::SaintDenis, PlaceKind::Leisure,
			{ 2813.741f, -1182.042f, 46.2764f }, 8.0f, 2.5f, 22.0f, 1020, 120, AllOccupations, true, "rcm_bh_sd_saloon.c:18929" },
		{ "sd_public", "Newspaper corner", TownId::SaintDenis, PlaceKind::Rest,
			{ 2683.454f, -1400.018f, 46.693f }, 9.0f, 2.5f, 22.0f, 0, 0, AllOccupations, true, "saintdenis.c:848" },
	};
	inline constexpr int kLocationCount = static_cast<int>(sizeof(kLocations) / sizeof(kLocations[0]));

	struct Town
	{
		TownId id;
		const char* name;
		Vector3 searchCenter;
		float searchRadius;
	};

	// Deliberately broad, fixed investigation areas. Centres reuse source anchors; these
	// radii are mod policy covering every enabled arrival + candidate/wander radius.
	// They are never used as unrestricted random spawning or wandering circles.
	inline const Town kTowns[] = {
		{ TownId::Rhodes, "Rhodes", { 1332.786f, -1299.38f, 77.354f }, 180.0f },
		{ TownId::Blackwater, "Blackwater", { -806.585f, -1330.295f, 43.60916f }, 190.0f },
		{ TownId::Valentine, "Valentine", { -269.754f, 785.441f, 118.489f }, 205.0f },
		{ TownId::Strawberry, "Strawberry", { -1773.417f, -394.25f, 157.091f }, 220.0f },
		{ TownId::SaintDenis, "Saint Denis", { 2721.924f, -1281.781f, 49.68018f }, 260.0f },
	};
	inline constexpr int kTownCount = static_cast<int>(sizeof(kTowns) / sizeof(kTowns[0]));
}
