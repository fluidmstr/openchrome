# Foliage lists (`*_foliage_<kind>.to`, Data2.pak, `data/maps/<map>/`)

Kinds seen: `grass`, `grass_high`, `nature`, `stones`, `rubble`, `trash`, `rooftrash`, `gore`, `burned`, `dead_birds`, `waterp`, `underwater_plants`. Sizes 8 bytes (empty) to 450 KB (guessed from sizes, not decoded).

Observations (partial, not decoded):
- No strings in the file: mesh names are not stored here, so the plant types are resolved elsewhere (probably per kind by a script).
- Header: `u16 0x0401 | 0x0801` (1025 / 2049, guessed grid size + 1), `u32 count`, `u32 0x000cfff8 / 0x000cfffd`, then zeros.
- Size minus a small header is a multiple of 32 bytes (`rubble` 14048 = 439 x 32; `grass` of `old_town_outposts_museum` 8264 = 8 + 258 x 32), and the record bodies look like bit masks (runs of `ff`/`f0`), i.e. a painted density mask rather than a list of instances.
