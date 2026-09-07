# mod-realm-armory

AzerothCore module publishing a read-only, schema-v1 character Armory for Portalkeeper and other consumers. It writes static JSON; it is not an HTTP server. Profiles describe saved character/database state, not live combat calculations.

## Installation

Place this repository at `<core-source>/modules/mod-realm-armory`, reconfigure and build your AzerothCore worldserver using that core's normal build instructions, then install the rebuilt server. [CMakeLists.txt](CMakeLists.txt) registers the module source with AzerothCore; this is not a standalone CMake application.

Copy [conf/mod_realm_armory.conf.dist](conf/mod_realm_armory.conf.dist) to the server's module configuration directory as `mod_realm_armory.conf` (normally `<server-config>/modules/`). Set the output directory explicitly before starting worldserver. The current distributed template contains a deployment-specific absolute output path; replace it with a location such as `armory` or `/srv/realm-public/armory`. The code fallback, when the option is absent, is `armory`.

Required: a compatible AzerothCore source/build environment, its normal character/login databases, loaded item templates and DBC stores, and filesystem write access for worldserver. The source uses the core module registry API in `ModuleMgr.h`. Neither Portalkeeper, an HTTP server, mod-playerbots nor mod-transmog is a build dependency. HTTP hosting is needed only for remote consumers.

## Configuration

All settings belong under `[worldserver]`. These are **code defaults**, which can differ from a copied deployment configuration:

| Option | Default | Behavior |
|---|---|---|
| `RealmArmory.Enable` | `1` | Enables publication. Disabling does not remove previously published files. |
| `RealmArmory.OutputDirectory` | `"armory"` | Absolute path or relative to worldserver's working directory; creates `characters/`. |
| `RealmArmory.UpdateIntervalMinutes` | `15` | Publication interval; minimum 1 minute. |
| `RealmArmory.MinimumLevel` | `1` | Minimum included level; configured threshold capped at 80, not a maximum character level. |
| `RealmArmory.IncludePlayerbots` | `1` | Include accounts identified by the configured random-bot prefix. |
| `RealmArmory.PlayerbotAccountPrefix` | `"rndbot"` | Match the random-bot account prefix, normally `AiPlayerbot.RandomBotAccountPrefix`. |
| `RealmArmory.Transmog.Enable` | `1` | Allow optional applied-appearance export when module, settings and schema are compatible. |

## Commands and publication

Administrator commands are `.realmarmory status` and `.realmarmory publish`. In the worldserver console omit the leading dot. Status reports configuration and last publication status/count/time; publish requests a full export and does not override a disabled module.

Publication runs on startup, periodically, and after configuration reload when enabled. It writes each profile first, then `index.json`, using one UTC `generatedAt` timestamp for the pass:

```text
<output-directory>/
  index.json
  characters/
    <character-guid>.json
```

Files are written through `.tmp` files, but replacement currently removes the old destination before rename. This is **not an atomic whole-feed snapshot or guaranteed atomic replacement**. Deleted or newly excluded characters disappear from the next roster; their old profile files are not automatically deleted. See [implementation issues](docs/implementation-issues.md) before treating exclusion as removal from a public website.

Characters are selected from saved database rows at or above the threshold, including offline characters. Equipped inventory slots 0–18 in bag 0 are exported; bags and bank contents are not. Bot classification uses account-name prefix matching in the login database, not an online AI state or a mandatory Playerbot API. It can classify only accounts matching that convention; ordinary-account bots may appear as players.

No account IDs/names, email addresses, IPs, credentials, mail or friends are published. Character GUIDs, names, appearance and equipment are deliberately public. This is not a per-character privacy/opt-out system.

## Portalkeeper integration

Serve or copy the output directory to an HTTP(S) static-file host, retaining its directory structure. Do not expose the server configuration or database backups. Publication and hosting are separate operations; this module does not upload files or configure web access.

Configure Portalkeeper's realm file:

```ini
[Updates]
ArmoryURL=https://realm.example.com/armory/index.json
```

Portalkeeper resolves `characters/<guid>.json` relative to that URL, caches roster/profiles, and uses the player's local 3.3.5a client for static previews. It does not connect to the server database or download model assets from this feed. Both roster and profile must advertise transmog support before Portalkeeper applies appearances. Older feeds without that capability are unsupported for transmog, while original equipment remains usable.

## Data and optional integrations

[JSON contract](docs/json-contract.md) documents character appearance, original item stats, damage/speed/DPS, enchants, actual gems, sockets, spell metadata and null/unresolved behavior. All additions retain `schemaVersion: 1`.

[Optional mod-transmog integration](TRANSMOG.md) documents capability detection, owner-scoped appearance lookup, hidden items and operation without the module. Real equipped item data is never replaced with appearance stats.

## Verification

From this repository root, the existing focused checks are:

```sh
g++ -std=c++17 -Wall -Wextra -Werror -Isrc tests/enchantments_test.cpp -o /tmp/armory-enchantments-test
/tmp/armory-enchantments-test
python3 tests/transmog_query_test.py
```

Earlier Linux work exercised parser/query checks and C++20 syntax checking against Playerbot-core headers. SQLite query fixtures are not MySQL integration tests. A linked worldserver build and controlled live publication with the target server's installed modules/schema remain to be verified. This documentation review did not rerun builds or rendering suites. See [implementation issues and verification gaps](docs/implementation-issues.md).

See [LICENSE](LICENSE).
