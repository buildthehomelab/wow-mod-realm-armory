# Current JSON contract

The authoritative writers are `BuildProfile`, `WriteInstanceDetails`, `WriteTransmog` and `Publish` in [mod_realm_armory.cpp](../src/mod_realm_armory.cpp). All numeric enum values use the core's 3.3.5 definitions. Unknown/additive fields should be tolerated by consumers.

Both documents contain `schemaVersion: 1`, UTC ISO timestamp `generatedAt`, and `capabilities: { "transmogrification": true|false }`.

`index.json` contains `characters`, with `id` (character GUID), `name`, `level`, `race`, `class`, `gender` and `playerbot` per entry. A profile contains a single `character` object with those fields plus `appearance` and `equipment`. Appearance fields are `skin`, `face`, `hairStyle`, `hairColor`, `facialStyle`: saved customization indices, not texture paths or guessed defaults.

## Original equipment

Only occupied equipment slots are emitted. Slots are: 0 head, 1 neck, 2 shoulders, 3 shirt, 4 chest, 5 waist, 6 legs, 7 feet, 8 wrists, 9 hands, 10–11 fingers, 12–13 trinkets, 14 back, 15 main hand, 16 off hand, 17 ranged/relic, 18 tabard.

| Fields | Meaning |
|---|---|
| `slot`, `entry`, `displayId`, `name`, `icon` | Real equipped item identity; icon is a client icon name, not a URL. |
| `quality`, `itemLevel`, `itemClass`, `subClass`, `inventoryType` | Original template metadata. |
| `requiredLevel`, `bonding`, `description` | Requirement, binding enum and flavor text. |
| `armor`, `block`, `holyRes`, `fireRes`, `natureRes`, `frostRes`, `shadowRes`, `arcaneRes` | Template defenses. |
| `currentDurability`, `maxDurability` | Saved instance durability and template maximum. |
| `stats` | Nonzero `{type,value}` template stat entries. |
| `damage` | Nonzero `{min,max,type}` components; type identifies damage school. |
| `delay` | Template attack delay in milliseconds. |
| `weaponSpeed`, `weaponDps` | For weapons with nonzero delay only: seconds and summed average template damage per second. No auras, enchant damage or character adjustments. |
| `socketColors` | Legacy list of nonzero template colors; does not preserve empty positions. |
| `sockets`, `socketBonusId` | All three `{index,color}` positions, including color 0; template bonus ID. Present when the item template resolves. |
| `enchantments` | Unchanged raw saved instance enchantment string. |
| `transmog` | Separate optional applied appearance; see [TRANSMOG.md](../TRANSMOG.md). |

## Enchants, gems and bonus

`enchantmentsValid` reports whether exactly 12 unsigned ID/duration/charges triples parsed successfully. Truncated, extra, negative, overflowing or malformed tokens are rejected. Invalid raw data remains exported; derived enchant fields and `gems` are omitted rather than invented.

On valid parsing, `permanentEnchant`, `temporaryEnchant`, `prismaticEnchant` and `socketBonus` are null for empty slots. Nonempty objects have `id`, `duration` (saved milliseconds), `charges`, `resolved`. Resolved DBC entries add English `description`, `conditionId`, and `effects` containing `type`, `amount`, `argument`. Effect arguments depend on type; they are not universally spell IDs. Unresolved objects retain identifiers without invented text.

`gems` contains occupied socket enchant slots with zero-based `socketIndex` and an `enchant` object. The enchant DBC's GemID supplies `entry`; a resolved gem template adds `name`, `quality`, and, when available, `icon` and `color` mask. Template socket contents are not treated as equipped gems.

`socketBonus` reflects the saved applied enchant slot. `socketBonusId` identifies the template bonus; neither guarantees the bonus or a conditional meta-gem effect is currently active. Temporary duration is a saved snapshot, not a running timer. Random-property enchant slots remain in the raw string; this is not a complete combat-stat calculator.

## Item spells

`spells` contains positive template spell IDs with `id`, `trigger`, `charges`, `ppmRate`, `cooldown`, `category`, `categoryCooldown`, plus `name` when the spell resolves. Cooldown values retain core units/sentinels. Triggers permit labels such as Use or Equip, but the exporter does not resolve full spell tooltip formulas. Consumers must not invent missing effect descriptions.

## Snapshot boundaries

There is no online status, skill/quest requirement evaluation, guild-tabard emblem customization or animation state in this contract. Roster and profiles are updated sequentially, not transactionally. Consumers should handle absent, null and unresolved optional fields and temporary network/publication failures.
