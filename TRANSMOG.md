# Optional applied transmog export

The installed source is on an inaccessible server; the user authorized the upstream reference instead:
https://github.com/azerothcore/mod-transmog

Verified upstream contracts: Transmogrification.h defines HIDDEN_ITEM_ID as 1; Transmogrification.cpp SetFakeEntry writes GUID/FakeEntry/Owner to custom_transmogrification, and reads Transmogrification.Enable for module enablement. The supplied character database backup has matching unsigned integer columns and a primary key on GUID. GUID is an item-instance ID, FakeEntry is an item-template entry, and Owner is a character ID.

Availability requires all of:

- mod-transmog in AzerothCore's enabled module registry (ModuleMgr.h).
- RealmArmory.Transmog.Enable and Transmogrification.Enable enabled.
- All three expected integer columns present in the current character database, checked through information_schema before querying the optional table.

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

The running server is inaccessible. Deploy this module, build/link worldserver with the server's installed core, restart it and publish with `.realmarmory publish`. Confirm both index and profile capabilities and Hipally's saved appearances in the resulting files. Verify a disabled/no-transmog server publishes normally. No deployment, database writes or live publication were performed here.
