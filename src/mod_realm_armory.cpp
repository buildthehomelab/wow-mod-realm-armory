#include "Chat.h"
#include "Config.h"
#include "DatabaseEnv.h"
#include "DBCStores.h"
#include "ObjectMgr.h"
#include "ModuleMgr.h"
#include "Item.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "ArmoryEnchantments.h"
#include "ScriptMgr.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace realm_armory
{
    namespace fs = std::filesystem;

    struct CharacterRow
    {
        uint32 Guid = 0;
        uint32 Account = 0;
        std::string Name;
        uint8 Race = 0;
        uint8 Class = 0;
        uint8 Gender = 0;
        uint8 Level = 0;
        uint8 Skin = 0;
        uint8 Face = 0;
        uint8 HairStyle = 0;
        uint8 HairColor = 0;
        uint8 FacialStyle = 0;
        bool Playerbot = false;
    };

    struct ItemStatInfo
    {
        uint32 Type = 0;
        int32 Value = 0;
    };

    struct ItemDamageInfo
    {
        float Min = 0.0f;
        float Max = 0.0f;
        uint32 Type = 0;
    };

    struct ItemInfo
    {
        uint32 Entry = 0;
        uint32 DisplayId = 0;
        std::string Name;
        std::string Icon;
        uint32 Quality = 0;
        uint32 ItemLevel = 0;
        uint32 ItemClass = 0;
        uint32 SubClass = 0;
        uint32 InventoryType = 0;
        uint32 RequiredLevel = 0;
        uint32 Bonding = 0;
        uint32 Armor = 0;
        uint32 Block = 0;
        uint32 Delay = 0;
        uint32 MaxDurability = 0;
        int32 HolyRes = 0;
        int32 FireRes = 0;
        int32 NatureRes = 0;
        int32 FrostRes = 0;
        int32 ShadowRes = 0;
        int32 ArcaneRes = 0;
        std::string Description;
        std::vector<ItemStatInfo> Stats;
        std::vector<ItemDamageInfo> Damage;
        std::vector<uint32> SocketColors;
    };

    struct EquippedItem
    {
        uint8 Slot = 0;
        uint32 CurrentDurability = 0;
        std::string Enchantments;
        uint32 TransmogEntry = 0;
        ItemInfo Item;
    };

    struct PublishJob
    {
        std::string OutputDirectory;
        uint32 MinimumLevel = 1;
        bool IncludePlayerbots = true;
        std::string PlayerbotAccountPrefix;
        bool TransmogEnabled = true;
    };

    bool Enabled = true;
    std::string OutputDirectory = "armory";
    uint32 UpdateIntervalMinutes = 15;
    uint32 MinimumLevel = 1;
    bool IncludePlayerbots = true;
    std::string PlayerbotAccountPrefix = "rndbot";
    uint32 UpdateTimerMs = 0;
    std::string LastStatus = "Not published yet.";
    std::string LastPublishedAt;
    uint32 LastCharacterCount = 0;
    std::mutex StatusMutex;

    std::string JsonEscape(std::string const& value)
    {
        std::ostringstream out;
        for (unsigned char c : value)
        {
            switch (c)
            {
                case '\\': out << "\\\\"; break;
                case '"': out << "\\\""; break;
                case '\b': out << "\\b"; break;
                case '\f': out << "\\f"; break;
                case '\n': out << "\\n"; break;
                case '\r': out << "\\r"; break;
                case '\t': out << "\\t"; break;
                default:
                    if (c < 0x20)
                        out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << int(c) << std::dec;
                    else
                        out << c;
            }
        }
        return out.str();
    }

    std::string IsoNowUtc()
    {
        auto now = std::chrono::system_clock::now();
        std::time_t value = std::chrono::system_clock::to_time_t(now);
        std::tm tm{};
#ifdef _WIN32
        gmtime_s(&tm, &value);
#else
        gmtime_r(&value, &tm);
#endif
        std::ostringstream out;
        out << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
        return out.str();
    }

    bool AtomicWrite(fs::path const& path, std::string const& contents)
    {
        try
        {
            fs::create_directories(path.parent_path());
            fs::path temp = path;
            temp += ".tmp";
            {
                std::ofstream file(temp, std::ios::binary | std::ios::trunc);
                if (!file)
                    return false;
                file << contents;
                file.flush();
                if (!file)
                    return false;
            }
            std::error_code ec;
            fs::remove(path, ec);
            ec.clear();
            fs::rename(temp, path, ec);
            if (ec)
            {
                fs::remove(temp, ec);
                return false;
            }
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    std::set<uint32> LoadPlayerbotAccounts(std::string const& playerbotAccountPrefix)
    {
        std::set<uint32> accounts;
        if (playerbotAccountPrefix.empty())
            return accounts;

        std::string prefix = playerbotAccountPrefix;
        LoginDatabase.EscapeString(prefix);
        QueryResult result = LoginDatabase.Query(
            "SELECT id FROM account WHERE username LIKE '{}%'", prefix);
        if (!result)
            return accounts;

        do
        {
            Field* fields = result->Fetch();
            accounts.insert(fields[0].Get<uint32>());
        } while (result->NextRow());
        return accounts;
    }

    std::vector<CharacterRow> LoadCharacters(PublishJob const& job)
    {
        std::vector<CharacterRow> rows;
        auto botAccounts = LoadPlayerbotAccounts(job.PlayerbotAccountPrefix);
        QueryResult result = CharacterDatabase.Query(
            "SELECT guid, account, name, race, class, gender, level, "
            "skin, face, hairStyle, hairColor, facialStyle "
            "FROM characters WHERE level >= {} ORDER BY name", job.MinimumLevel);
        if (!result)
            return rows;

        do
        {
            Field* f = result->Fetch();
            CharacterRow row;
            row.Guid = f[0].Get<uint32>();
            row.Account = f[1].Get<uint32>();
            row.Name = f[2].Get<std::string>();
            row.Race = f[3].Get<uint8>();
            row.Class = f[4].Get<uint8>();
            row.Gender = f[5].Get<uint8>();
            row.Level = f[6].Get<uint8>();
            row.Skin = f[7].Get<uint8>();
            row.Face = f[8].Get<uint8>();
            row.HairStyle = f[9].Get<uint8>();
            row.HairColor = f[10].Get<uint8>();
            row.FacialStyle = f[11].Get<uint8>();
            row.Playerbot = botAccounts.count(row.Account) != 0;
            if (!row.Playerbot || job.IncludePlayerbots)
                rows.push_back(std::move(row));
        } while (result->NextRow());
        return rows;
    }

    bool TransmogAvailable(bool transmogEnabled)
    {
        auto const modules = Acore::Module::GetEnableModulesList();
        if (!transmogEnabled ||
            std::find(modules.begin(), modules.end(), "mod-transmog") == modules.end())
            return false;

        // Inspect metadata first; a server without the optional table must never query it.
        QueryResult schema = CharacterDatabase.Query(
            "SELECT COUNT(*) FROM information_schema.COLUMNS "
            "WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'custom_transmogrification' "
            "AND COLUMN_NAME IN ('GUID', 'FakeEntry', 'Owner') AND DATA_TYPE = 'int'");
        return schema && schema->Fetch()[0].Get<uint64>() == 3;
    }

    std::vector<EquippedItem> LoadEquipment(uint32 guid, bool transmogSupported)
    {
        std::vector<EquippedItem> equipment;
        QueryResult result = transmogSupported ? CharacterDatabase.Query(
            "SELECT ci.slot, ii.itemEntry, ii.durability, ii.enchantments, tm.FakeEntry "
            "FROM character_inventory ci "
            "INNER JOIN item_instance ii ON ii.guid = ci.item "
            "LEFT JOIN custom_transmogrification tm ON tm.GUID = ii.guid AND tm.Owner = ci.guid "
            "WHERE ci.guid = {} AND ci.bag = 0 AND ci.slot BETWEEN 0 AND 18 "
            "ORDER BY ci.slot", guid) : CharacterDatabase.Query(
            "SELECT ci.slot, ii.itemEntry, ii.durability, ii.enchantments, 0 "
            "FROM character_inventory ci "
            "INNER JOIN item_instance ii ON ii.guid = ci.item "
            "WHERE ci.guid = {} AND ci.bag = 0 AND ci.slot BETWEEN 0 AND 18 "
            "ORDER BY ci.slot", guid);
        if (!result)
            return equipment;

        do
        {
            Field* f = result->Fetch();
            EquippedItem equipped;
            equipped.Slot = f[0].Get<uint8>();
            equipped.Item.Entry = f[1].Get<uint32>();
            equipped.CurrentDurability = f[2].Get<uint16>();
            equipped.Enchantments = f[3].Get<std::string>();
            equipped.TransmogEntry = transmogSupported && !f[4].IsNull() ? f[4].Get<uint32>() : 0;

            if (ItemTemplate const* itemTemplate = sObjectMgr->GetItemTemplate(equipped.Item.Entry))
            {
                equipped.Item.DisplayId = itemTemplate->DisplayInfoID;
                equipped.Item.Name = itemTemplate->Name1;
                equipped.Item.Quality = itemTemplate->Quality;
                equipped.Item.ItemLevel = itemTemplate->ItemLevel;
                equipped.Item.ItemClass = itemTemplate->Class;
                equipped.Item.SubClass = itemTemplate->SubClass;
                equipped.Item.InventoryType = itemTemplate->InventoryType;
                equipped.Item.RequiredLevel = itemTemplate->RequiredLevel;
                equipped.Item.Bonding = itemTemplate->Bonding;
                equipped.Item.Armor = itemTemplate->Armor;
                equipped.Item.Block = itemTemplate->Block;
                equipped.Item.Delay = itemTemplate->Delay;
                equipped.Item.MaxDurability = itemTemplate->MaxDurability;
                equipped.Item.HolyRes = itemTemplate->HolyRes;
                equipped.Item.FireRes = itemTemplate->FireRes;
                equipped.Item.NatureRes = itemTemplate->NatureRes;
                equipped.Item.FrostRes = itemTemplate->FrostRes;
                equipped.Item.ShadowRes = itemTemplate->ShadowRes;
                equipped.Item.ArcaneRes = itemTemplate->ArcaneRes;
                equipped.Item.Description = itemTemplate->Description;

                for (uint32 i = 0; i < itemTemplate->StatsCount && i < MAX_ITEM_PROTO_STATS; ++i)
                {
                    ItemStatInfo stat;
                    stat.Type = itemTemplate->ItemStat[i].ItemStatType;
                    stat.Value = itemTemplate->ItemStat[i].ItemStatValue;
                    if (stat.Value != 0)
                        equipped.Item.Stats.push_back(stat);
                }

                for (uint32 i = 0; i < MAX_ITEM_PROTO_DAMAGES; ++i)
                {
                    if (itemTemplate->Damage[i].DamageMin == 0.0f &&
                        itemTemplate->Damage[i].DamageMax == 0.0f)
                        continue;

                    ItemDamageInfo damage;
                    damage.Min = itemTemplate->Damage[i].DamageMin;
                    damage.Max = itemTemplate->Damage[i].DamageMax;
                    damage.Type = itemTemplate->Damage[i].DamageType;
                    equipped.Item.Damage.push_back(damage);
                }

                for (uint32 i = 0; i < MAX_ITEM_PROTO_SOCKETS; ++i)
                {
                    if (itemTemplate->Socket[i].Color != 0)
                        equipped.Item.SocketColors.push_back(itemTemplate->Socket[i].Color);
                }

                if (ItemDisplayInfoEntry const* displayInfo =
                        sItemDisplayInfoStore.LookupEntry(itemTemplate->DisplayInfoID))
                {
                    if (displayInfo->inventoryIcon)
                        equipped.Item.Icon = displayInfo->inventoryIcon;
                }
            }

            equipment.push_back(std::move(equipped));
        } while (result->NextRow());

        return equipment;
    }

    // DBC effect arguments are type-dependent: stat IDs, spell IDs, resistance masks, etc.
    void WriteEnchant(std::ostream& out, ArmoryEnchantments::Slot const& slot)
    {
        if (!slot.Id)
        {
            out << "null";
            return;
        }
        auto const* enchant = sSpellItemEnchantmentStore.LookupEntry(slot.Id);
        out << "{\"id\":" << slot.Id << ",\"duration\":" << slot.Duration
            << ",\"charges\":" << slot.Charges << ",\"resolved\":" << (enchant ? "true" : "false");
        if (enchant)
        {
            out << ",\"description\":\"" << JsonEscape(enchant->description[0] ? enchant->description[0] : "")
                << "\",\"conditionId\":" << enchant->EnchantmentCondition << ",\"effects\":[";
            bool first = true;
            for (uint32 i = 0; i < MAX_SPELL_ITEM_ENCHANTMENT_EFFECTS; ++i)
            {
                if (!enchant->type[i]) continue;
                if (!first) out << ',';
                first = false;
                out << "{\"type\":" << enchant->type[i] << ",\"amount\":" << enchant->amount[i]
                    << ",\"argument\":" << enchant->spellid[i] << '}';
            }
            out << ']';
        }
        out << '}';
    }

    void WriteInstanceDetails(std::ostream& out, EquippedItem const& item)
    {
        static_assert(MAX_ENCHANTMENT_SLOT == ArmoryEnchantments::SlotCount);
        static_assert(MAX_ENCHANTMENT_OFFSET == 3);
        auto parsed = ArmoryEnchantments::Parse(item.Enchantments);
        out << ",\"enchantmentsValid\":" << (parsed.Valid ? "true" : "false");
        // Invalid fields remain available verbatim, but never become plausible derived data.
        if (parsed.Valid)
        {
            out << ",\"permanentEnchant\":";
            WriteEnchant(out, parsed.Slots[PERM_ENCHANTMENT_SLOT]);
            out << ",\"temporaryEnchant\":";
            WriteEnchant(out, parsed.Slots[TEMP_ENCHANTMENT_SLOT]);
            out << ",\"prismaticEnchant\":";
            WriteEnchant(out, parsed.Slots[PRISMATIC_ENCHANTMENT_SLOT]);
            out << ",\"socketBonus\":";
            WriteEnchant(out, parsed.Slots[BONUS_ENCHANTMENT_SLOT]);
            out << ",\"gems\":[";
            bool first = true;
            for (uint32 i = 0; i < MAX_ITEM_PROTO_SOCKETS; ++i)
            {
                auto const& slot = parsed.Slots[SOCK_ENCHANTMENT_SLOT + i];
                if (!slot.Id) continue;
                if (!first) out << ',';
                first = false;
                out << "{\"socketIndex\":" << i << ",\"enchant\":";
                WriteEnchant(out, slot);
                auto const* enchant = sSpellItemEnchantmentStore.LookupEntry(slot.Id);
                if (enchant && enchant->GemID)
                {
                    out << ",\"entry\":" << enchant->GemID;
                    if (auto const* gem = sObjectMgr->GetItemTemplate(enchant->GemID))
                    {
                        out << ",\"name\":\"" << JsonEscape(gem->Name1) << "\",\"quality\":" << gem->Quality;
                        if (auto const* display = sItemDisplayInfoStore.LookupEntry(gem->DisplayInfoID))
                            out << ",\"icon\":\"" << JsonEscape(display->inventoryIcon ? display->inventoryIcon : "") << '"';
                        if (auto const* properties = sGemPropertiesStore.LookupEntry(gem->GemProperties))
                            out << ",\"color\":" << properties->color;
                    }
                }
                out << '}';
            }
            out << ']';
        }
        if (auto const* proto = sObjectMgr->GetItemTemplate(item.Item.Entry))
        {
            out << ",\"socketBonusId\":" << proto->socketBonus << ",\"sockets\":[";
            for (uint32 i = 0; i < MAX_ITEM_PROTO_SOCKETS; ++i)
            {
                if (i) out << ',';
                out << "{\"index\":" << i << ",\"color\":" << proto->Socket[i].Color << '}';
            }
            out << "],\"spells\":[";
            bool first = true;
            for (auto const& spell : proto->Spells)
            {
                if (spell.SpellId <= 0) continue;
                if (!first) out << ',';
                first = false;
                out << "{\"id\":" << spell.SpellId << ",\"trigger\":" << spell.SpellTrigger
                    << ",\"charges\":" << spell.SpellCharges << ",\"ppmRate\":" << spell.SpellPPMRate
                    << ",\"cooldown\":" << spell.SpellCooldown << ",\"category\":" << spell.SpellCategory
                    << ",\"categoryCooldown\":" << spell.SpellCategoryCooldown;
                if (auto const* info = sSpellMgr->GetSpellInfo(spell.SpellId))
                    out << ",\"name\":\"" << JsonEscape(info->SpellName[0] ? info->SpellName[0] : "") << '"';
                out << '}';
            }
            out << ']';
        }
        // Template weapon damage, not character combat damage after bonuses and auras.
        if (item.Item.ItemClass == ITEM_CLASS_WEAPON && item.Item.Delay)
        {
            double minimum = 0, maximum = 0;
            for (auto const& damage : item.Item.Damage)
            {
                minimum += damage.Min;
                maximum += damage.Max;
            }
            out << ",\"weaponSpeed\":" << item.Item.Delay / 1000.0
                << ",\"weaponDps\":" << (minimum + maximum) * 500.0 / item.Item.Delay;
        }
    }

    void WriteTransmog(std::ostream& out, uint32 entry)
    {
        out << ",\"transmog\":";
        if (!entry) { out << "null"; return; }
        // mod-transmog's HIDDEN_ITEM_ID is a sentinel, not an item template.
        if (entry == 1) { out << "{\"hidden\":true,\"resolved\":true}"; return; }
        auto const* item = sObjectMgr->GetItemTemplate(entry);
        out << "{\"hidden\":false,\"entry\":" << entry
            << ",\"resolved\":" << (item ? "true" : "false");
        if (item)
        {
            out << ",\"name\":\"" << JsonEscape(item->Name1) << "\",\"displayId\":" << item->DisplayInfoID
                << ",\"quality\":" << item->Quality << ",\"itemClass\":" << item->Class
                << ",\"subClass\":" << item->SubClass << ",\"inventoryType\":" << item->InventoryType;
            if (auto const* display = sItemDisplayInfoStore.LookupEntry(item->DisplayInfoID))
                out << ",\"icon\":\"" << JsonEscape(display->inventoryIcon ? display->inventoryIcon : "") << '\"';
        }
        out << '}';
    }

    std::string BuildProfile(CharacterRow const& row, std::string const& generatedAt, bool transmogSupported)
    {
        auto equipment = LoadEquipment(row.Guid, transmogSupported);
        std::ostringstream out;
        out << "{\n  \"schemaVersion\": 1,\n";
        out << "  \"generatedAt\": \"" << generatedAt << "\",\n";
        out << "  \"capabilities\": {\"transmogrification\": " << (transmogSupported ? "true" : "false") << "},\n";
        out << "  \"character\": {\n";
        out << "    \"id\": " << row.Guid << ",\n";
        out << "    \"name\": \"" << JsonEscape(row.Name) << "\",\n";
        out << "    \"level\": " << uint32(row.Level) << ",\n";
        out << "    \"race\": " << uint32(row.Race) << ",\n";
        out << "    \"class\": " << uint32(row.Class) << ",\n";
        out << "    \"gender\": " << uint32(row.Gender) << ",\n";
        out << "    \"playerbot\": " << (row.Playerbot ? "true" : "false") << ",\n";
        out << "    \"appearance\": {"
            << "\"skin\": " << uint32(row.Skin)
            << ", \"face\": " << uint32(row.Face)
            << ", \"hairStyle\": " << uint32(row.HairStyle)
            << ", \"hairColor\": " << uint32(row.HairColor)
            << ", \"facialStyle\": " << uint32(row.FacialStyle)
            << "},\n";
        out << "    \"equipment\": [\n";

        for (size_t i = 0; i < equipment.size(); ++i)
        {
            auto const& e = equipment[i];
            out << "      {\"slot\": " << uint32(e.Slot)
                << ", \"entry\": " << e.Item.Entry
                << ", \"displayId\": " << e.Item.DisplayId
                << ", \"name\": \"" << JsonEscape(e.Item.Name)
                << "\", \"icon\": \"" << JsonEscape(e.Item.Icon)
                << "\", \"quality\": " << e.Item.Quality
                << ", \"itemLevel\": " << e.Item.ItemLevel
                << ", \"itemClass\": " << e.Item.ItemClass
                << ", \"subClass\": " << e.Item.SubClass
                << ", \"inventoryType\": " << e.Item.InventoryType
                << ", \"requiredLevel\": " << e.Item.RequiredLevel
                << ", \"bonding\": " << e.Item.Bonding
                << ", \"armor\": " << e.Item.Armor
                << ", \"block\": " << e.Item.Block
                << ", \"delay\": " << e.Item.Delay
                << ", \"currentDurability\": " << e.CurrentDurability
                << ", \"maxDurability\": " << e.Item.MaxDurability
                << ", \"holyRes\": " << e.Item.HolyRes
                << ", \"fireRes\": " << e.Item.FireRes
                << ", \"natureRes\": " << e.Item.NatureRes
                << ", \"frostRes\": " << e.Item.FrostRes
                << ", \"shadowRes\": " << e.Item.ShadowRes
                << ", \"arcaneRes\": " << e.Item.ArcaneRes
                << ", \"description\": \"" << JsonEscape(e.Item.Description) << "\"";

            out << ", \"stats\": [";
            for (size_t s = 0; s < e.Item.Stats.size(); ++s)
            {
                auto const& stat = e.Item.Stats[s];
                out << "{\"type\": " << stat.Type << ", \"value\": " << stat.Value << "}";
                if (s + 1 != e.Item.Stats.size()) out << ',';
            }
            out << ']';

            out << ", \"damage\": [";
            for (size_t d = 0; d < e.Item.Damage.size(); ++d)
            {
                auto const& damage = e.Item.Damage[d];
                out << "{\"min\": " << damage.Min
                    << ", \"max\": " << damage.Max
                    << ", \"type\": " << damage.Type << "}";
                if (d + 1 != e.Item.Damage.size()) out << ',';
            }
            out << ']';

            out << ", \"socketColors\": [";
            for (size_t s = 0; s < e.Item.SocketColors.size(); ++s)
            {
                out << e.Item.SocketColors[s];
                if (s + 1 != e.Item.SocketColors.size()) out << ',';
            }
            out << ']';

            // Keep the raw instance enchantment field available for later
            // gem/enchant-aware rendering without exposing any private data.
            out << ", \"enchantments\": \"" << JsonEscape(e.Enchantments) << "\"";
            WriteInstanceDetails(out, e);
            WriteTransmog(out, e.TransmogEntry);
            out << '}';

            if (i + 1 != equipment.size()) out << ',';
            out << '\n';
        }

        out << "    ]\n  }\n}\n";
        return out.str();
    }

    bool Publish(PublishJob const& job)
    {
        bool const transmogSupported = TransmogAvailable(job.TransmogEnabled);
        auto characters = LoadCharacters(job);
        std::string generatedAt = IsoNowUtc();
        fs::path root(job.OutputDirectory);
        fs::path characterDir = root / "characters";

        try { fs::create_directories(characterDir); }
        catch (...)
        {
            std::lock_guard<std::mutex> lock(StatusMutex);
            LastStatus = "Unable to create output directory.";
            return false;
        }

        for (auto const& row : characters)
        {
            if (!AtomicWrite(characterDir / (std::to_string(row.Guid) + ".json"), BuildProfile(row, generatedAt, transmogSupported)))
            {
                std::lock_guard<std::mutex> lock(StatusMutex);
                LastStatus = "Failed while writing character profile files.";
                return false;
            }
        }

        std::ostringstream index;
        index << "{\n  \"schemaVersion\": 1,\n";
        index << "  \"generatedAt\": \"" << generatedAt << "\",\n";
        index << "  \"capabilities\": {\"transmogrification\": " << (transmogSupported ? "true" : "false") << "},\n";
        index << "  \"characters\": [\n";
        for (size_t i = 0; i < characters.size(); ++i)
        {
            auto const& row = characters[i];
            index << "    {\"id\": " << row.Guid
                  << ", \"name\": \"" << JsonEscape(row.Name)
                  << "\", \"level\": " << uint32(row.Level)
                  << ", \"race\": " << uint32(row.Race)
                  << ", \"class\": " << uint32(row.Class)
                  << ", \"gender\": " << uint32(row.Gender)
                  << ", \"playerbot\": " << (row.Playerbot ? "true" : "false") << "}";
            if (i + 1 != characters.size()) index << ',';
            index << '\n';
        }
        index << "  ]\n}\n";

        if (!AtomicWrite(root / "index.json", index.str()))
        {
            std::lock_guard<std::mutex> lock(StatusMutex);
            LastStatus = "Failed while writing index.json.";
            return false;
        }

        {
            std::lock_guard<std::mutex> lock(StatusMutex);
            LastPublishedAt = generatedAt;
            LastCharacterCount = static_cast<uint32>(characters.size());
            LastStatus = "Published " + std::to_string(LastCharacterCount) + " characters.";
        }
        return true;
    }

    PublishJob MakePublishJob()
    {
        PublishJob job;
        job.OutputDirectory = OutputDirectory;
        job.MinimumLevel = MinimumLevel;
        job.IncludePlayerbots = IncludePlayerbots;
        job.PlayerbotAccountPrefix = PlayerbotAccountPrefix;
        job.TransmogEnabled =
            sConfigMgr->GetOption<bool>("RealmArmory.Transmog.Enable", true) &&
            sConfigMgr->GetOption<bool>("Transmogrification.Enable", true);
        return job;
    }

    class PublishWorker
    {
    public:
        void Start()
        {
            std::lock_guard<std::mutex> lock(_mutex);
            if (_thread.joinable())
                return;

            _stopping = false;
            _thread = std::thread([this]() { Run(); });
        }

        void Stop()
        {
            {
                std::lock_guard<std::mutex> lock(_mutex);
                _stopping = true;
                _pending = false;
            }
            _condition.notify_one();
            if (_thread.joinable())
                _thread.join();
        }

        bool Request(PublishJob job)
        {
            {
                std::lock_guard<std::mutex> lock(_mutex);
                if (_stopping || !_thread.joinable())
                    return false;

                // Keep only the newest request. If a publish is already running,
                // this becomes one coalesced follow-up publish instead of allowing
                // slow storage to create an unbounded backlog.
                _job = std::move(job);
                _pending = true;
            }
            _condition.notify_one();
            return true;
        }

        bool Busy() const
        {
            std::lock_guard<std::mutex> lock(_mutex);
            return _running || _pending;
        }

    private:
        void Run()
        {
            for (;;)
            {
                PublishJob job;
                {
                    std::unique_lock<std::mutex> lock(_mutex);
                    _condition.wait(lock, [this]() { return _stopping || _pending; });
                    if (_stopping)
                        break;

                    job = _job;
                    _pending = false;
                    _running = true;
                }

                Publish(job);

                {
                    std::lock_guard<std::mutex> lock(_mutex);
                    _running = false;
                }
            }
        }

        mutable std::mutex _mutex;
        std::condition_variable _condition;
        std::thread _thread;
        PublishJob _job;
        bool _pending = false;
        bool _running = false;
        bool _stopping = false;
    };

    PublishWorker Worker;

    bool RequestPublish()
    {
        if (!Enabled)
        {
            std::lock_guard<std::mutex> lock(StatusMutex);
            LastStatus = "Realm Armory is disabled.";
            return false;
        }

        if (!Worker.Request(MakePublishJob()))
        {
            std::lock_guard<std::mutex> lock(StatusMutex);
            LastStatus = "Publish worker is not running.";
            return false;
        }

        {
            std::lock_guard<std::mutex> lock(StatusMutex);
            LastStatus = "Publish queued.";
        }
        return true;
    }

    void LoadConfig()
    {
        Enabled = sConfigMgr->GetOption<bool>("RealmArmory.Enable", true);
        OutputDirectory = sConfigMgr->GetOption<std::string>("RealmArmory.OutputDirectory", "armory");
        UpdateIntervalMinutes = std::max<uint32>(1, sConfigMgr->GetOption<uint32>("RealmArmory.UpdateIntervalMinutes", 15));
        MinimumLevel = std::min<uint32>(80, sConfigMgr->GetOption<uint32>("RealmArmory.MinimumLevel", 1));
        IncludePlayerbots = sConfigMgr->GetOption<bool>("RealmArmory.IncludePlayerbots", true);
        PlayerbotAccountPrefix = sConfigMgr->GetOption<std::string>("RealmArmory.PlayerbotAccountPrefix", "rndbot");
        UpdateTimerMs = UpdateIntervalMinutes * 60 * 1000;
    }

    class RealmArmoryWorldScript : public WorldScript
    {
    public:
        RealmArmoryWorldScript() : WorldScript("RealmArmoryWorldScript") { }

        void OnBeforeConfigLoad(bool /*reload*/) override { LoadConfig(); }
        void OnAfterConfigLoad(bool reload) override { if (reload && Enabled) RequestPublish(); }
        void OnStartup() override
        {
            Worker.Start();
            if (Enabled)
                RequestPublish();
        }
        void OnShutdown() override { Worker.Stop(); }
        void OnUpdate(uint32 diff) override
        {
            if (!Enabled) return;
            if (UpdateTimerMs <= diff)
            {
                RequestPublish();
                UpdateTimerMs = UpdateIntervalMinutes * 60 * 1000;
            }
            else UpdateTimerMs -= diff;
        }
    };

    class RealmArmoryCommandScript : public CommandScript
    {
    public:
        RealmArmoryCommandScript() : CommandScript("RealmArmoryCommandScript") { }

        std::vector<Acore::ChatCommands::ChatCommandBuilder> GetCommands() const override
        {
            using Acore::ChatCommands::ChatCommandBuilder;
            using Acore::ChatCommands::Console;

            static std::vector<ChatCommandBuilder> sub = {
                { "status", HandleStatus, SEC_ADMINISTRATOR, Console::Yes },
                { "publish", HandlePublish, SEC_ADMINISTRATOR, Console::Yes }
            };

            static std::vector<ChatCommandBuilder> root = {
                { "realmarmory", sub }
            };

            return root;
        }

        static bool HandleStatus(ChatHandler* handler)
        {
            bool const workerBusy = Worker.Busy();
            std::lock_guard<std::mutex> lock(StatusMutex);
            handler->PSendSysMessage("Realm Armory: {}", Enabled ? "enabled" : "disabled");
            handler->PSendSysMessage("Output: {}", OutputDirectory);
            handler->PSendSysMessage("Last publish: {}", LastPublishedAt.empty() ? "never" : LastPublishedAt);
            handler->PSendSysMessage("Characters: {}", LastCharacterCount);
            handler->PSendSysMessage("Worker: {}", workerBusy ? "busy" : "idle");
            handler->PSendSysMessage("Status: {}", LastStatus);
            return true;
        }

        static bool HandlePublish(ChatHandler* handler)
        {
            bool ok = RequestPublish();
            handler->PSendSysMessage("[Realm Armory] {}", ok ? "Publish queued." : "Unable to queue publish.");
            return ok;
        }
    };
}

void Addmod_realm_armoryScripts()
{
    new realm_armory::RealmArmoryWorldScript();
    new realm_armory::RealmArmoryCommandScript();
}
