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

## Phase tree and execution (loader + `oc_questrun`)

Structure (verified over all 305 files): a `Quest` is a sequence of phases. `AND` and `OR` phases (1148 + 427 of the ~15300 phases, counting nested ones) contain one or more `<Path>` elements, each a sequence of phases; every other phase carries `Destination` / `Trigger` / `Spawner` / `Object` / `SpecificLocation` children instead. `DebugPlayerPosition` children are editor teleports.

Execution model (guessed, produces sensible traces): phases of a sequence run in order; an AND phase runs its paths in parallel and ends when all paths ended, OR ends when one did (the others are dropped); a phase without paths ends at once. Instant phases act (enable, appear, set weather, hudgroup, player control, set dialog, ...); waiting phases (`go to`, `checkpoint`, `use`, `talk`, `kill`, `use life place`, `movie`, `take item`, `loot container`, `clear area`, every `* guard`) block until the game reports them done. `wait guard` waits `time` seconds, `started quest guard quest_name=X finished=true` waits for quest X to finish. A quest is startable once its `parent` has finished (`game_root` always).

`src/core/questrun.{hpp,cpp}` (`QuestRunner`, `QuestManager`, host callbacks `onRun` / `onWait`) and `oc_questrun <DW dir> <quest> [seconds]` implement this on simulated time. Check: `Prologue` starts by hiding the HUD groups and the watch/flashlight, freezing time at 9:50, populating the Tower with NPC life places, then waits for the wake-up movie trigger, shows the `Tutorial_PrologueMove` hint and sends the player to the exit doors, i.e. the order of the real game intro.

`oc_viewer <DW> <map> --quest <name>` runs a quest in the viewer: `set day night time` sets the clock, `go to` / `checkpoint` complete when the eye-minus-1.7 m position is within `distance` (min 1 m) of the named entity, a `checkpoint` teleports the player to its entity, the objective (`go to` name and distance) is shown in the window title, other waits pass after 0.3 s, the rest is only logged.

## Open

Semantics of each phase type and its attributes, the guard/AND/OR evaluation rules, how `Phase` nesting expresses sequence versus parallel, how `QuestObject` names resolve to map entities, and how texts (`&Q_..._Name&` keys) map to the localisation tables.

## Display names (verified)

A quest `X` has the English title in `DataEn.pak` text tables under key `X_Name` (203 of 1137 quests have one directly); the text may itself be a `&Other_Name&` reference to another entry, resolved by repeating the lookup with `Other`. Quests chained by `parent` (`University01`, `University02`, ...) share their title, so a numbered chain is the stages of one story quest. `oc_queststat <DW dir> <level>` prints the titles.
