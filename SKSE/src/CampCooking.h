#pragma once

// Cooking at the camp fire has no cooking pot, so the stews and soups are left
// out of its Cook menu: while that menu is open, the recipes needing two or more
// different ingredients (salt aside) are switched to another workbench
// keyword, and switched back when it closes. Real cooking pots are untouched.
namespace CampCooking
{
	// Call just before opening the Cook menu at a_station (the camp's hidden
	// cooking station). Main thread only.
	void Begin(RE::TESObjectREFR* a_station);

	// Watches for the Crafting menu closing. Call once the UI exists.
	void Install();
}
