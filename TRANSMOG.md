# Optional applied transmog export

The integration targets the [upstream mod-transmog contract](https://github.com/azerothcore/mod-transmog). The target server checkout was unavailable during implementation; its exact deployed revision remains to be checked. See [installation and configuration](README.md) and the [JSON contract](docs/json-contract.md).

Verified upstream contracts: Transmogrification.h defines HIDDEN_ITEM_ID as 1; Transmogrification.cpp SetFakeEntry writes GUID/FakeEntry/Owner to custom_transmogrification, and reads Transmogrification.Enable for module enablement. The supplied character database backup has matching unsigned integer columns and a primary key on GUID. GUID is an item-instance ID, FakeEntry is an item-template entry, and Owner is a character ID.

Availability requires all of:

- mod-transmog in AzerothCore's enabled module registry (ModuleMgr.h).
- RealmArmory.Transmog.Enable and Transmogrification.Enable enabled.
- All three expected integer columns present in the current character database, checked through information_schema before querying the optional table.

Transmogrification.Enable is read only when mod-transmog is loaded, so a server without it logs no "Missing property" warning.

## mod-transmog-plus

[mod-transmog-plus](https://github.com/buildthehomelab/wow-mod-transmog-plus) is supported the same way, and is preferred when both modules are loaded. It needs:

- mod-transmog-plus in the enabled module registry.
- RealmArmory.Transmog.Enable and Transmog.Enable (mod-transmog-plus's switch) enabled.
- `mod_transmog_plus` with integer `Owner` and `FakeEntry` and tinyint `Slot`, checked through information_schema.

It stores one appearance per character and equipment slot, not per item instance, so the export joins on `Owner` and `Slot`. Its hidden sentinel (999999) becomes the usual `{ "hidden": true }`, but only in armor slots. In game, mod-transmog-plus also skips a stored appearance that no longer suits the item now in the slot. Its full rules depend on its config and the player, so the export applies the two that don't: hiding only counts in armor slots, and the appearance must be the same item class (armor or weapon) as the equipped item. Anything else it would reject in game can still show in the export until the player changes it.

No mod-transmog header, library, schema migration or module dependency is added. Absent/disabled modules and missing/incompatible tables publish capability false and use the original equipment query. Disable RealmArmory.Transmog.Enable to opt out explicitly.

## Additive schema v1 fields

Both index.json and each profile contain `capabilities: { "transmogrification": true|false }`.

Every equipment item adds `transmog`, without replacing original item fields:

- null: no applied appearance, or support unavailable.
- `{ "hidden": true, "resolved": true }`: explicitly hidden. The sentinel is not resolved as an item.
- A visible appearance contains hidden=false, resolved=true, entry, name, displayId, quality, itemClass, subClass, inventoryType and icon when its display record provides one.
- A missing appearance item template retains entry, hidden=false, resolved=false. No original item data is disguised as resolved transmog data.

The equipped instance joins on both GUID and Owner. Bag items and another character's rows cannot supply an equipped appearance. Account identifiers and item-instance IDs are not added to published JSON. Stats, enchants, gems, damage, durability, original icons and existing privacy remain unchanged. Exports reflect saved database state at publication time.

## Verification and deployment

GNU C++20 syntax checking passed against the local Playerbot AzerothCore headers with no mod-transmog installed. Existing enchantment parser tests passed. tests/transmog_query_test.py executes the actual exporter SELECT statements against isolated SQLite fixtures, covering an absent optional table, original fields, hidden state, equipped-only filtering and owner isolation. This does not replace a MySQL/worldserver integration test.

The running server is inaccessible. Deploy this module, build/link worldserver with the server's installed core, restart it and publish with `.realmarmory publish`. Confirm both index and profile capabilities and a test character’s saved appearances in the resulting files. Verify a disabled/no-transmog server publishes normally. No deployment, database writes or live publication were performed here.
