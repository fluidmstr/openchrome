# Quests (`data/quests/**/*.xml`)

305 XML files in `DW/Data*.pak`, one per map or storyline (`data/quests/<level>/<level>_*.xml`). Root `<QuestsDefinitions level="..." pxsl_line="...">` (4 files add `source` / `quest_tree`). `pxsl_line` is a source line from the authoring tool and carries no meaning at runtime. `data/quests/_patch/*.scr` are save-compatibility diffs between versions (`QuestTree`/`Quest`/`Phase` with numeric ids) and `questspatch/*.xml` are similar; neither is needed for play. `data/quests/_underlay/quests_colors.xml` only colours phase types in the editor.

Parser and loader: `src/core/quest.{hpp,cpp}` (small lenient XML reader + `loadQuests`), `oc_queststat <DW dir> [level]` prints totals or one level's quests and phases. Result: 1137 quests, 5709 phases (loader view; the raw XML has ~15000 `Phase` tags because phases also nest).

## Structure

```
Quest name parent [glued reward_set difficulty quest_giver leading_portal new_chapter replayable final ...]
  Phase type name [distance show_locations mode state speaker time group desired_state auto_start ...]
    Destination | Object | Trigger | Spawner | LifePlace | ... children holding <QuestObject class name/>
```

`parent` links a quest to the quest that starts or contains it (root `Q_Start01` / `game_root`); `QuestObject class="SpawnPoint" name="MapStart"` references an object placed in the map by class and name (the `.exp` entities, see `exp.md`). Quests are therefore a graph of phases, each phase of a given type with condition/target objects.

## Phase types (count over all files)

Flow and guards: `checkpoint` 952, `started quest guard` 372, `wait guard` 260, `gather players guard` 162, `AND` 618, `OR` 141, `semaphore` 19. Actions: `enable` 961, `appear` 698, `go to` 446, `use` 213, `talk` 179, `use life place` 136, `set state` 123, `kill` 69, `set dialog` 64, `set reward` 63, `set skin` 53, `movie` 44, `take item` 26, `take challenge` 22, `player control` 19, plus ~25 rarer ones (`set weather`, `set day night time`, `force encounter`, `loot container`, `clear area`, ...).

## Open

Semantics of each phase type and its attributes, the guard/AND/OR evaluation rules, how `Phase` nesting expresses sequence versus parallel, how `QuestObject` names resolve to map entities, and how texts (`&Q_..._Name&` keys) map to the localisation tables.
