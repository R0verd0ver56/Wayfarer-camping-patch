#include "PluginProperties.h"

namespace PluginProperties
{
	namespace
	{
		constexpr auto       kPluginPrefix = "Wayfarer - A Convenient Camping Framework."sv;
		constexpr RE::FormID kCampQuest = 0x800;  // LZC_CampsiteQuest
		constexpr auto       kScript = "LZC_CampsiteQuestScript"sv;

		std::map<std::string, std::vector<RE::TESForm*>, std::less<>> arrays;
		bool                                                          loaded = false;

		// Reads little-endian values from a byte buffer, stopping (ok = false) at
		// its end.
		struct Reader
		{
			const std::uint8_t* data;
			std::size_t         size;
			std::size_t         at{ 0 };
			bool                ok{ true };

			template <class T>
			T Get()
			{
				T value{};
				if (at + sizeof(T) > size) {
					ok = false;
					at = size;
					return value;
				}
				std::memcpy(&value, data + at, sizeof(T));
				at += sizeof(T);
				return value;
			}

			std::string WString()
			{
				const auto length = Get<std::uint16_t>();
				if (at + length > size) {
					ok = false;
					at = size;
					return {};
				}
				std::string text(reinterpret_cast<const char*>(data + at), length);
				at += length;
				return text;
			}

			void Skip(std::size_t a_bytes)
			{
				if (at + a_bytes > size) {
					ok = false;
					at = size;
				} else {
					at += a_bytes;
				}
			}
		};

		// The quest's VMAD, from the plugin file (not compressed: the Creation Kit
		// doesn't compress quests). Also the plugin's masters, in order.
		bool ReadQuestVmad(const std::vector<std::uint8_t>& a_file, std::vector<std::string>& a_masters, std::vector<std::uint8_t>& a_vmad)
		{
			if (a_file.size() < 24 || std::memcmp(a_file.data(), "TES4", 4) != 0) {
				return false;
			}
			Reader file{ a_file.data(), a_file.size() };
			// TES4: its MAST subrecords.
			file.Skip(4);
			const auto headerSize = file.Get<std::uint32_t>();
			file.Skip(16);
			const std::size_t headerEnd = file.at + headerSize;
			while (file.ok && file.at + 6 <= headerEnd) {
				char type[4];
				std::memcpy(type, a_file.data() + file.at, 4);
				file.Skip(4);
				const auto length = file.Get<std::uint16_t>();
				if (std::memcmp(type, "MAST", 4) == 0 && length > 0) {
					a_masters.emplace_back(reinterpret_cast<const char*>(a_file.data() + file.at), length - 1);
				}
				file.Skip(length);
			}
			const RE::FormID questID = (static_cast<RE::FormID>(a_masters.size()) << 24) | kCampQuest;

			// Records, through any groups, to the quest.
			std::size_t at = headerEnd;
			while (at + 24 <= a_file.size()) {
				const auto* p = a_file.data() + at;
				std::uint32_t size;
				std::memcpy(&size, p + 4, 4);
				if (std::memcmp(p, "GRUP", 4) == 0) {
					at += 24;  // into the group
					continue;
				}
				std::uint32_t flags, formID;
				std::memcpy(&flags, p + 8, 4);
				std::memcpy(&formID, p + 12, 4);
				if (std::memcmp(p, "QUST", 4) == 0 && formID == questID) {
					if (flags & 0x40000) {
						logger::warn("Plugin properties: the camp quest's record is compressed; can't read it");
						return false;
					}
					Reader record{ p + 24, std::min<std::size_t>(size, a_file.size() - at - 24) };
					std::uint32_t bigSize = 0;
					while (record.ok && record.at + 6 <= record.size) {
						char type[4];
						std::memcpy(type, record.data + record.at, 4);
						record.Skip(4);
						std::uint32_t length = record.Get<std::uint16_t>();
						if (bigSize) {
							length = std::exchange(bigSize, 0);
						}
						if (std::memcmp(type, "XXXX", 4) == 0) {
							bigSize = record.Get<std::uint32_t>();
							continue;
						}
						if (std::memcmp(type, "VMAD", 4) == 0) {
							if (record.at + length > record.size) {
								return false;
							}
							a_vmad.assign(record.data + record.at, record.data + record.at + length);
							return true;
						}
						record.Skip(length);
					}
					return false;
				}
				at += 24 + size;
			}
			return false;
		}

		// A FormID from the plugin file (its top byte an index into its masters,
		// or the plugin itself after them), as loaded.
		RE::TESForm* Resolve(RE::FormID a_fileID, const std::vector<std::string>& a_masters, std::string_view a_plugin)
		{
			if (a_fileID == 0) {
				return nullptr;
			}
			auto*      dataHandler = RE::TESDataHandler::GetSingleton();
			const auto index = a_fileID >> 24;
			const auto local = a_fileID & 0xFFFFFF;
			if (index < a_masters.size()) {
				return dataHandler->LookupForm(local, a_masters[index]);
			}
			return dataHandler->LookupForm(local, a_plugin);
		}

		void Load()
		{
			loaded = true;
			auto*             dataHandler = RE::TESDataHandler::GetSingleton();
			const RE::TESFile* plugin = nullptr;
			for (const auto* extension : { "esp", "esl", "esm" }) {
				const auto name = std::string(kPluginPrefix) + extension;
				if (dataHandler && (plugin = dataHandler->LookupModByName(name)) != nullptr) {
					break;
				}
			}
			if (!plugin) {
				return;
			}
			const std::string pluginName(plugin->GetFilename());
			std::ifstream     stream(std::filesystem::path("Data") / pluginName, std::ios::binary);
			if (!stream) {
				logger::warn("Plugin properties: can't open Data/{}", pluginName);
				return;
			}
			const std::vector<std::uint8_t> file((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
			std::vector<std::string>        masters;
			std::vector<std::uint8_t>       vmad;
			if (!ReadQuestVmad(file, masters, vmad)) {
				logger::warn("Plugin properties: the camp quest's scripts weren't found in {}", pluginName);
				return;
			}

			Reader     r{ vmad.data(), vmad.size() };
			r.Skip(2);  // version
			const auto objectFormat = r.Get<std::int16_t>();
			const auto scriptCount = r.Get<std::uint16_t>();
			const auto readObject = [&] {
				std::uint32_t formID;
				if (objectFormat == 1) {
					formID = r.Get<std::uint32_t>();
					r.Skip(4);
				} else {
					r.Skip(4);
					formID = r.Get<std::uint32_t>();
				}
				return formID;
			};
			for (std::uint16_t s = 0; r.ok && s < scriptCount; ++s) {
				const auto scriptName = r.WString();
				r.Skip(1);  // status
				const auto propertyCount = r.Get<std::uint16_t>();
				for (std::uint16_t p = 0; r.ok && p < propertyCount; ++p) {
					const auto name = r.WString();
					const auto type = r.Get<std::uint8_t>();
					r.Skip(1);  // status
					switch (type) {
					case 1:
						readObject();
						break;
					case 2:
						r.WString();
						break;
					case 3:
					case 4:
						r.Skip(4);
						break;
					case 5:
						r.Skip(1);
						break;
					case 11:
						{
							const auto count = r.Get<std::uint32_t>();
							std::vector<RE::TESForm*> forms;
							for (std::uint32_t i = 0; r.ok && i < count; ++i) {
								forms.push_back(Resolve(readObject(), masters, pluginName));
							}
							if (scriptName == kScript) {
								arrays[name] = std::move(forms);
							}
							break;
						}
					case 12:
						{
							const auto count = r.Get<std::uint32_t>();
							for (std::uint32_t i = 0; r.ok && i < count; ++i) {
								r.WString();
							}
							break;
						}
					case 13:
					case 14:
						r.Skip(4 * static_cast<std::size_t>(r.Get<std::uint32_t>()));
						break;
					case 15:
						r.Skip(r.Get<std::uint32_t>());
						break;
					default:
						logger::warn("Plugin properties: unknown property type {} ({}); stopped reading", type, name);
						arrays.clear();
						return;
					}
				}
				if (scriptName == kScript) {
					break;
				}
			}
			if (!r.ok) {
				logger::warn("Plugin properties: the camp quest's script data ended early; not used");
				arrays.clear();
				return;
			}
			logger::info("Plugin properties: read {} object list(s) from {}", arrays.size(), pluginName);
		}
	}

	void Preload()
	{
		if (!loaded) {
			Load();
		}
	}

	const std::vector<RE::TESForm*>* ObjectArray(std::string_view a_name)
	{
		if (!loaded) {
			Load();
		}
		const auto it = arrays.find(a_name);
		return it != arrays.end() ? &it->second : nullptr;
	}
}
