#pragma once

// The camp quest script's object-array properties as the plugin file defines
// them. A script's property values are kept in the save from the first time it
// ran, so an existing save never sees items added to an array (a clutter set's
// new lantern, say) in a later version of the plugin. These are read from the
// plugin itself, fresh on every game.
namespace PluginProperties
{
	// The plugin's value for object-array property a_name of
	// LZC_CampsiteQuestScript, or nullptr if the plugin has none (or couldn't be
	// read). Main thread only.
	const std::vector<RE::TESForm*>* ObjectArray(std::string_view a_name);

	// Reads them. Call once the data has loaded, before anything asks.
	void Preload();
}
