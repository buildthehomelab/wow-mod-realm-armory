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
