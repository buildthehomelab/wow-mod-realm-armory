# mod-realm-armory

A small AzerothCore module that publishes a sanitized, read-only character armory feed for Portalkeeper or other clients.

It is **not a web server**. It writes static JSON files only. Serve the output directory with your existing HTTP server if desired.

## 0.1 scope

- Searchable character roster (`index.json`)
- One profile per character (`characters/<guid>.json`)
- Name, level, race, class, gender
- Equipped slots 0-18 with item entry, name, quality and item level
- Random Playerbot detection by configured account-name prefix
- Atomic JSON replacement
- Startup + periodic publication
- `.realmarmory status` and `.realmarmory publish`

No account IDs, account names, email addresses, IP addresses, mail, friends, credentials, or other private account data are exported.

## Portalkeeper

Point `ArmoryURL` at the published directory's `index.json`, for example:

```ini
[Updates]
ArmoryURL=https://example.invalid/data/armory/index.json
```

Portalkeeper derives character profile URLs from the index URL (`characters/<guid>.json`).

## Armory 0.5.1 equipment details

Existing schemaVersion 1 fields, raw `enchantments`, publication behavior, and
privacy filtering are preserved. New equipment fields are additive:

- `enchantmentsValid`: exactly 12 unsigned ID/duration/charges triples were parsed.
  Invalid data is retained raw; derived enchant and gem fields are omitted.
- `permanentEnchant`, `temporaryEnchant`, `prismaticEnchant`, `socketBonus`:
  null for an empty slot, otherwise ID, saved duration (milliseconds), charges,
  and `resolved`. Resolved records include English DBC description, condition ID,
  and effect type/amount/argument. Arguments are type-dependent, not always spells.
- `gems`: occupied socket slots with zero-based `socketIndex` and enchant details.
  Gem item IDs come from the enchantment DBC's GemID, never socket template Content.
  Available item names, quality, icons, and gem color masks are included.
- `sockets`: all three template positions, preserving empty positions;
  `socketBonusId`: template bonus ID. `socketBonus` reflects the saved applied slot,
  not a claim that a bonus or conditional meta gem is currently active in combat.
- `spells`: positive item-template spell IDs, triggers, charges, PPM rate, cooldown,
  category and category cooldown, plus names when SpellMgr resolves them. Cooldowns
  retain core sentinel values. This does not evaluate spell tooltip formulas.
- `weaponSpeed` (seconds), `weaponDps`: template weapon data using all damage
  components and millisecond delay. Zero-delay and non-weapon items omit these.
  Existing `damage` school types remain intact. These are not character-adjusted
  combat damage, scaling-item calculations, or enchant damage folded into base DPS.

Profiles still represent saved database equipment. Temporary durations are saved
snapshots. Random-property slots remain available in the unchanged raw field.
No additional character/account queries or private fields are introduced.

### Verification

The parser test covers triple alignment, gem and bonus positions, whitespace,
truncation, extra tokens, negative values, overflow, and malformed numbers:

```sh
g++ -std=c++17 -Wall -Wextra -Werror -Isrc tests/enchantments_test.cpp -o /tmp/armory-enchantments-test
/tmp/armory-enchantments-test
```

The module also passed GNU C++20 syntax checking against the local Playerbot core
headers. A linked worldserver build and live publication with server DBC/database
records still need to be run in the server build environment.
