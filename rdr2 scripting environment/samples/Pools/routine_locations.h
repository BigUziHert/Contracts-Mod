#pragma once

// Source-grounded candidate areas, not pre-approved spawn coordinates. The runtime must
// validate ground, navigation, water, interiors, clearance and collision before use.
// Vector3 is supplied by global.h/contract_data.h before including this header.
// See docs/routine-location-sources.md for the exact source context and limitations.
namespace RoutineData
{
	enum class TownId { Rhodes, Blackwater, Valentine, Strawberry, SaintDenis, VanHorn, Annesburg };
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

		{ "rhd_north_homes", "Northern homes", TownId::Rhodes, PlaceKind::Rest,
			{ 1392.933f, -1138.08f, 74.72879f }, 5.0f, 2.5f, 12.0f, 0, 0, AllOccupations, true, "beat_domestic_dispute.c:10576" },
		{ "rhd_station", "Station approach", TownId::Rhodes, PlaceKind::Rest,
			{ 1234.343f, -1297.911f, 75.9041f }, 5.0f, 2.5f, 14.0f, 0, 0, AllOccupations, true, "rcm_beau_and_penelope21.c:11178" },
		{ "blw_waterfront_street", "Waterfront shops", TownId::Blackwater, PlaceKind::Shops,
			{ -755.42f, -1269.1f, 43.02f }, 5.0f, 2.5f, 16.0f, 480, 1200, AllOccupations, true, "beat_town_burial.c:1592" },
		{ "blw_north_civic", "North civic approach", TownId::Blackwater, PlaceKind::Rest,
			{ -816.0f, -1221.0f, 43.5f }, 4.0f, 2.5f, 12.0f, 0, 0, AllOccupations, true, "owner outdoor survey 2026-09-06; public approach only" },
		{ "val_keanes", "Keane's approach", TownId::Valentine, PlaceKind::Leisure,
			{ -249.7122f, 765.8113f, 116.4685f }, 5.0f, 2.5f, 12.0f, 1020, 120, AllOccupations, true, "beat_rowdy_drunks.c:3789" },
		{ "val_theatre", "Theatre tent approach", TownId::Valentine, PlaceKind::Leisure,
			{ -352.6599f, 708.3228f, 115.8174f }, 5.0f, 2.5f, 12.0f, 1020, 120, AllOccupations, true, "theatre_ticket_taker.c:7216" },
		{ "val_station", "Station approach", TownId::Valentine, PlaceKind::Rest,
			{ -168.7946f, 646.7418f, 112.5389f }, 5.0f, 2.5f, 16.0f, 0, 0, AllOccupations, true, "train_fast_travel_core.c:2046" },
		{ "str_south_loop", "South-loop street", TownId::Strawberry, PlaceKind::Rest,
			{ -1778.01f, -434.3291f, 154.1013f }, 5.0f, 2.5f, 14.0f, 0, 0, AllOccupations, true, "beat_lost_dog.c:11249" },
		{ "str_stable_door", "Stable entrance", TownId::Strawberry, PlaceKind::Work,
			{ -1813.551f, -563.673f, 157.3218f }, 5.0f, 2.5f, 14.0f, 360, 1080, Local | Laborer | LivestockHand, true, "strawberry.c:689" },
		{ "sd_gunsmith", "Gunsmith porch", TownId::SaintDenis, PlaceKind::Shops,
			{ 2721.924f, -1281.781f, 49.68018f }, 5.0f, 2.5f, 14.0f, 480, 1200, AllOccupations, true, "saintdenis.c:789" },
		{ "sd_stables", "Stable entrance", TownId::SaintDenis, PlaceKind::Work,
			{ 2502.26f, -1435.017f, 45.37257f }, 5.0f, 2.5f, 16.0f, 360, 1080, Local | Laborer, true, "saintdenis.c:837" },
		{ "sd_vaudeville", "Vaudeville entrance", TownId::SaintDenis, PlaceKind::Leisure,
			{ 2537.884f, -1278.321f, 48.21795f }, 5.0f, 2.5f, 12.0f, 1020, 120, AllOccupations, true, "theatre_ticket_taker.c:8091" },
		{ "sd_lantern", "Lantern entrance", TownId::SaintDenis, PlaceKind::Leisure,
			{ 2682.637f, -1365.043f, 46.54007f }, 5.0f, 2.5f, 12.0f, 1020, 120, AllOccupations, true, "theatre_ticket_taker.c:7212" },
		{ "sd_doctor", "Doctor approach", TownId::SaintDenis, PlaceKind::Shops,
			{ 2724.748f, -1237.901f, 48.9465f }, 5.0f, 2.5f, 12.0f, 480, 1200, AllOccupations, true, "rcm_doctors_opinion1.c:10443" },
		{ "sd_tailor", "Tailor approach", TownId::SaintDenis, PlaceKind::Shops,
			{ 2552.833f, -1178.821f, 52.3113f }, 5.0f, 2.5f, 12.0f, 480, 1200, AllOccupations, true, "industry3.c:37045" },
		{ "sd_photographer", "Photo studio approach", TownId::SaintDenis, PlaceKind::Shops,
			{ 2724.84f, -1116.28f, 48.6f }, 5.0f, 2.5f, 12.0f, 480, 1200, AllOccupations, true, "beat_brontes_town_encounter.c:3949" },
		{ "sd_fancy_saloon", "Fancy saloon approach", TownId::SaintDenis, PlaceKind::Leisure,
			{ 2624.247f, -1213.454f, 52.2357f }, 5.0f, 2.5f, 12.0f, 1020, 120, AllOccupations, true, "finale3.c:52284" },
		{ "sd_vegetable_market", "Vegetable market", TownId::SaintDenis, PlaceKind::Shops,
			{ 2842.929f, -1230.073f, 46.6737f }, 5.0f, 2.5f, 14.0f, 480, 1200, AllOccupations, true, "finale3.c:61527" },
		{ "sd_trapper", "Trapper-side market", TownId::SaintDenis, PlaceKind::Shops,
			{ 2827.043f, -1225.969f, 46.5896f }, 5.0f, 2.5f, 12.0f, 480, 1200, AllOccupations, true, "finale3.c:61523" },
		{ "sd_fence", "Fence approach", TownId::SaintDenis, PlaceKind::Shops,
			{ 2849.79f, -1202.872f, 46.54987f }, 5.0f, 2.5f, 12.0f, 480, 1200, AllOccupations, true, "marston6.c:72104" },
		{ "rhd_fence", "Fence-side lane", TownId::Rhodes, PlaceKind::Shops,
			{ 1305.627f, -1144.795f, 80.3402f }, 5.0f, 2.5f, 12.0f, 480, 1200, AllOccupations, true, "beat_public_hanging.c:922" },
		{ "rhd_north_camp", "Northern camp", TownId::Rhodes, PlaceKind::Rest,
			{ 1353.0f, -1160.0f, 82.0f }, 5.0f, 2.5f, 12.0f, 0, 0, AllOccupations, true, "owner outdoor survey 2026-09-06" },

		{ "vht_wharf", "Wharf approach", TownId::VanHorn, PlaceKind::Work,
			{ 2979.109f, 537.0801f, 42.49713f }, 4.0f, 2.5f, 12.0f, 360, 1080, Local | Laborer | DockWorker, true, "beat_lost_drunk.c:3648" },
		{ "vht_stable_front", "Stable frontage", TownId::VanHorn, PlaceKind::Shops,
			{ 2959.127f, 795.8362f, 51.00545f }, 6.0f, 2.5f, 18.0f, 480, 1200, AllOccupations, true, "vanhorntradingpost.c:688" },
		{ "vht_fence_front", "Fence-side wharf", TownId::VanHorn, PlaceKind::Shops,
			{ 3031.736f, 553.738f, 43.6349f }, 4.0f, 2.5f, 12.0f, 480, 1200, AllOccupations, true, "beat_rowdy_drunks.c:3993" },
		{ "vht_saloon_street", "Saloon street", TownId::VanHorn, PlaceKind::Leisure,
			{ 2953.286f, 531.9273f, 43.81627f }, 5.0f, 2.5f, 14.0f, 1020, 120, AllOccupations, true, "beat_rowdy_drunks.c:3992" },
		{ "vht_south_street", "South street corner", TownId::VanHorn, PlaceKind::Leisure,
			{ 2952.115f, 494.7861f, 44.90863f }, 5.0f, 2.5f, 14.0f, 1020, 120, AllOccupations, true, "beat_rowdy_drunks.c:3728" },
		{ "vht_depot_road", "Depot road", TownId::VanHorn, PlaceKind::Rest,
			{ 2962.389f, 574.5291f, 43.38869f }, 6.0f, 2.5f, 16.0f, 0, 0, AllOccupations, true, "beat_lost_drunk.c:4234" },
		{ "vht_north_road", "Northwest road", TownId::VanHorn, PlaceKind::Rest,
			{ 2895.641f, 632.9738f, 56.70817f }, 5.0f, 2.5f, 14.0f, 0, 0, AllOccupations, true, "beat_lost_drunk.c:3666" },

		{ "asb_mine_approach", "Mine approach", TownId::Annesburg, PlaceKind::Work,
			{ 2857.732f, 1364.61f, 64.6895f }, 5.0f, 2.5f, 12.0f, 360, 1080, Local | Laborer, true, "rcm_dutch31.c:33404" },
		{ "asb_factory_street", "Factory-side street", TownId::Annesburg, PlaceKind::Work,
			{ 2967.282f, 1383.099f, 43.7717f }, 4.0f, 2.5f, 12.0f, 360, 1080, Local | Laborer, true, "finale3.c:61604" },
		{ "asb_north_yard", "North waterfront yard", TownId::Annesburg, PlaceKind::Work,
			{ 2948.074f, 1401.257f, 43.2553f }, 4.0f, 2.5f, 12.0f, 360, 1080, Local | Laborer, true, "finale3.c:61613" },
		{ "asb_gunsmith_front", "Gunsmith frontage", TownId::Annesburg, PlaceKind::Shops,
			{ 2940.486f, 1321.557f, 44.432f }, 5.0f, 2.5f, 14.0f, 480, 1200, AllOccupations, true, "annesburg.c:1252" },
		{ "asb_station_road", "Station approach", TownId::Annesburg, PlaceKind::Shops,
			{ 2920.64f, 1273.347f, 43.50124f }, 5.0f, 2.5f, 16.0f, 480, 1200, AllOccupations, true, "train_fast_travel_core.c:2057" },
		{ "asb_waterfront", "North waterfront", TownId::Annesburg, PlaceKind::Leisure,
			{ 2980.358f, 1411.604f, 43.3966f }, 4.0f, 2.5f, 12.0f, 1020, 120, AllOccupations, true, "finale3.c:61601" },
		{ "asb_public_street", "Main street", TownId::Annesburg, PlaceKind::Rest,
			{ 2941.66f, 1340.566f, 43.0553f }, 6.0f, 2.5f, 18.0f, 0, 0, AllOccupations, true, "rcm_edith_down21.c:3038" },
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
		{ TownId::Rhodes, "Rhodes", { 1332.786f, -1299.38f, 77.354f }, 210.0f },
		{ TownId::Blackwater, "Blackwater", { -806.585f, -1330.295f, 43.60916f }, 190.0f },
		{ TownId::Valentine, "Valentine", { -269.754f, 785.441f, 118.489f }, 205.0f },
		{ TownId::Strawberry, "Strawberry", { -1773.417f, -394.25f, 157.091f }, 220.0f },
		{ TownId::SaintDenis, "Saint Denis", { 2721.924f, -1281.781f, 49.68018f }, 310.0f },
		{ TownId::VanHorn, "Van Horn", { 2962.389f, 574.5291f, 43.38869f }, 270.0f },
		{ TownId::Annesburg, "Annesburg", { 2941.66f, 1340.566f, 43.0553f }, 180.0f },
	};
	inline constexpr int kTownCount = static_cast<int>(sizeof(kTowns) / sizeof(kTowns[0]));
}
