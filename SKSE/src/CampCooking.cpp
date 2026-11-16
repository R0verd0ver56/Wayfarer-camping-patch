#include "CampCooking.h"

namespace CampCooking
{
	namespace
	{
		constexpr RE::FormID kSaltPile = 0x034CDF;  // Skyrim.esm
		constexpr RE::FormID kSpareKeyword = 0xDA1;  // LZC_TentAddonSmall: never a workbench's
		constexpr auto       kPlugin = "Wayfarer - A Convenient Camping Framework.esp"sv;

		std::vector<std::pair<RE::BGSConstructibleObject*, RE::BGSKeyword*>> hidden;  // main thread only
		std::atomic<bool>                                                    menuSeen{ false };

		void Restore()
		{
			for (const auto& [recipe, keyword] : hidden) {
				recipe->benchKeyword = keyword;
			}
			if (!hidden.empty()) {
				logger::info("Cooking: {} stew recipe(s) back on the cooking pot", hidden.size());
			}
			hidden.clear();
		}

		// Two or more different ingredients, salt aside.
		bool IsStew(const RE::BGSConstructibleObject* a_recipe, const RE::TESForm* a_salt)
		{
			std::set<const RE::TESBoundObject*> ingredients;
			a_recipe->requiredItems.ForEachContainerObject([&](RE::ContainerObject& a_entry) {
				if (a_entry.obj && a_entry.obj != a_salt) {
					ingredients.insert(a_entry.obj);
				}
				return RE::BSContainer::ForEachResult::kContinue;
			});
			return ingredients.size() >= 2;
		}

		class CraftingMenuClose : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
		{
		public:
			RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
			{
				if (a_event && a_event->menuName == RE::CraftingMenu::MENU_NAME) {
					if (a_event->opening) {
						menuSeen = true;
					} else {
						SKSE::GetTaskInterface()->AddTask(Restore);
					}
				}
				return RE::BSEventNotifyControl::kContinue;
			}
		};
	}

	void Begin(RE::TESObjectREFR* a_station)
	{
		Restore();
		auto* dataHandler = RE::TESDataHandler::GetSingleton();
		auto* base = a_station ? a_station->GetBaseObject() : nullptr;
		auto* keywords = base ? base->As<RE::BGSKeywordForm>() : nullptr;
		auto* spare = dataHandler ? dataHandler->LookupForm<RE::BGSKeyword>(kSpareKeyword, kPlugin) : nullptr;
		if (!keywords || !spare) {
			return;
		}
		auto* salt = RE::TESForm::LookupByID(kSaltPile);
		for (auto* recipe : dataHandler->GetFormArray<RE::BGSConstructibleObject>()) {
			if (recipe && recipe->benchKeyword && keywords->HasKeyword(recipe->benchKeyword) && IsStew(recipe, salt)) {
				hidden.emplace_back(recipe, recipe->benchKeyword);
				recipe->benchKeyword = spare;
			}
		}
		logger::info("Cooking: {} stew recipe(s) left out at the camp fire", hidden.size());

		// If the menu doesn't open after all, put them back anyway.
		menuSeen = false;
		std::thread([] {
			std::this_thread::sleep_for(5s);
			if (!menuSeen) {
				SKSE::GetTaskInterface()->AddTask([] {
					if (!menuSeen && !RE::UI::GetSingleton()->IsMenuOpen(RE::CraftingMenu::MENU_NAME)) {
						Restore();
					}
				});
			}
		}).detach();
	}

	void Install()
	{
		static CraftingMenuClose sink;
		if (auto* ui = RE::UI::GetSingleton()) {
			ui->AddEventSink<RE::MenuOpenCloseEvent>(&sink);
		}
	}
}
