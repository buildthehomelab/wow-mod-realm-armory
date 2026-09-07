# Implementation issues and verification gaps

Recorded by source review; no behavior was changed in this documentation update.

- `AtomicWrite` removes the destination before rename. Readers can encounter a missing file; a failed pass can leave mixed generations because profiles precede the roster.
- Old profile files are never pruned. Removing a character from the roster, disabling bots or disabling publication does not revoke access to previously hosted profile URLs. Operators must remove stale public files separately when needed.
- The distributed output-directory value is deployment-specific, whereas the code default is `armory`. Installation instructions require replacing it; configuration values were deliberately not changed in this prose-only review.
- Publication performs database/file work synchronously in worldserver hooks. Large-roster performance and publication consistency under changing saved equipment need target-server measurement.
- Prefix-based bot classification is a convention, not exhaustive bot detection.

Pending: full worldserver link/build on the target core; MySQL/schema and permission checks; controlled publication with enabled, disabled, absent and incompatible mod-transmog; saved hidden/unresolved appearances and stale-profile cleanup operations. Earlier Linux syntax/parser/SQLite checks are limited evidence, not server deployment verification. No expensive checks were repeated for documentation edits.
