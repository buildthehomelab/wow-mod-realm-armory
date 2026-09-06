#include "Chat.h"
#include "Config.h"
#include "DatabaseEnv.h"
#include "DBCStores.h"
#include "ObjectMgr.h"
#include "ScriptMgr.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <string>
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
        bool Playerbot = false;
    };

    struct ItemInfo
    {
        uint32 Entry = 0;
        std::string Name;
        std::string Icon;
        uint32 Quality = 0;
        uint32 ItemLevel = 0;
    };

    struct EquippedItem
    {
        uint8 Slot = 0;
        ItemInfo Item;
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

    std::set<uint32> LoadPlayerbotAccounts()
    {
        std::set<uint32> accounts;
        if (PlayerbotAccountPrefix.empty())
            return accounts;

        std::string prefix = PlayerbotAccountPrefix;
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

    std::vector<CharacterRow> LoadCharacters()
    {
        std::vector<CharacterRow> rows;
        auto botAccounts = LoadPlayerbotAccounts();
        QueryResult result = CharacterDatabase.Query(
            "SELECT guid, account, name, race, class, gender, level "
            "FROM characters WHERE level >= {} ORDER BY name", MinimumLevel);
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
            row.Playerbot = botAccounts.count(row.Account) != 0;
            if (!row.Playerbot || IncludePlayerbots)
                rows.push_back(std::move(row));
        } while (result->NextRow());
        return rows;
    }

    std::vector<EquippedItem> LoadEquipment(uint32 guid)
    {
        std::vector<EquippedItem> equipment;
        QueryResult result = CharacterDatabase.Query(
            "SELECT ci.slot, ii.itemEntry FROM character_inventory ci "
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

            if (ItemTemplate const* itemTemplate = sObjectMgr->GetItemTemplate(equipped.Item.Entry))
            {
                equipped.Item.Name = itemTemplate->Name1;
                equipped.Item.Quality = itemTemplate->Quality;
                equipped.Item.ItemLevel = itemTemplate->ItemLevel;

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

    std::string BuildProfile(CharacterRow const& row, std::string const& generatedAt)
    {
        auto equipment = LoadEquipment(row.Guid);
        std::ostringstream out;
        out << "{\n  \"schemaVersion\": 1,\n";
        out << "  \"generatedAt\": \"" << generatedAt << "\",\n";
        out << "  \"character\": {\n";
        out << "    \"id\": " << row.Guid << ",\n";
        out << "    \"name\": \"" << JsonEscape(row.Name) << "\",\n";
        out << "    \"level\": " << uint32(row.Level) << ",\n";
        out << "    \"race\": " << uint32(row.Race) << ",\n";
        out << "    \"class\": " << uint32(row.Class) << ",\n";
        out << "    \"gender\": " << uint32(row.Gender) << ",\n";
        out << "    \"playerbot\": " << (row.Playerbot ? "true" : "false") << ",\n";
        out << "    \"equipment\": [\n";
        for (size_t i = 0; i < equipment.size(); ++i)
        {
            auto const& e = equipment[i];
            out << "      {\"slot\": " << uint32(e.Slot)
                << ", \"entry\": " << e.Item.Entry
                << ", \"name\": \"" << JsonEscape(e.Item.Name)
                << "\", \"icon\": \"" << JsonEscape(e.Item.Icon)
                << "\", \"quality\": " << e.Item.Quality
                << ", \"itemLevel\": " << e.Item.ItemLevel << "}";
            if (i + 1 != equipment.size()) out << ',';
            out << '\n';
        }
        out << "    ]\n  }\n}\n";
        return out.str();
    }

    bool Publish()
    {
        if (!Enabled)
        {
            LastStatus = "Realm Armory is disabled.";
            return false;
        }

        auto characters = LoadCharacters();
        std::string generatedAt = IsoNowUtc();
        fs::path root(OutputDirectory);
        fs::path characterDir = root / "characters";

        try { fs::create_directories(characterDir); }
        catch (...) { LastStatus = "Unable to create output directory."; return false; }

        for (auto const& row : characters)
        {
            if (!AtomicWrite(characterDir / (std::to_string(row.Guid) + ".json"), BuildProfile(row, generatedAt)))
            {
                LastStatus = "Failed while writing character profile files.";
                return false;
            }
        }

        std::ostringstream index;
        index << "{\n  \"schemaVersion\": 1,\n";
        index << "  \"generatedAt\": \"" << generatedAt << "\",\n";
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
            LastStatus = "Failed while writing index.json.";
            return false;
        }

        LastPublishedAt = generatedAt;
        LastCharacterCount = static_cast<uint32>(characters.size());
        LastStatus = "Published " + std::to_string(LastCharacterCount) + " characters.";
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
        void OnAfterConfigLoad(bool reload) override { if (reload && Enabled) Publish(); }
        void OnStartup() override { if (Enabled) Publish(); }
        void OnUpdate(uint32 diff) override
        {
            if (!Enabled) return;
            if (UpdateTimerMs <= diff)
            {
                Publish();
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
            handler->PSendSysMessage("Realm Armory: {}", Enabled ? "enabled" : "disabled");
            handler->PSendSysMessage("Output: {}", OutputDirectory);
            handler->PSendSysMessage("Last publish: {}", LastPublishedAt.empty() ? "never" : LastPublishedAt);
            handler->PSendSysMessage("Characters: {}", LastCharacterCount);
            handler->PSendSysMessage("Status: {}", LastStatus);
            return true;
        }

        static bool HandlePublish(ChatHandler* handler)
        {
            bool ok = Publish();
            handler->PSendSysMessage("[Realm Armory] {}", LastStatus);
            return ok;
        }
    };
}

void Addmod_realm_armoryScripts()
{
    new realm_armory::RealmArmoryWorldScript();
    new realm_armory::RealmArmoryCommandScript();
}
