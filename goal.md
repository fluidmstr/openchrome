# Goal

A playable Dying Light 1, started like the stock executable: point `openchrome` at your own copy of the game and play the campaign from the first cutscene to the last. The runtime is written from scratch (C++17, Vulkan, SDL2, PC first). No game assets or code are shipped; everything is read from the user's installation.

## Done means

- Story mode plays end to end: intro, missions and side quests, cutscenes, dialogue, save and load, credits.
- Parkour, melee and ranged combat, infected AI, day/night with Volatiles, co-op later.
- Audio: music, ambience, effects, voice-over with lip sync.
- The stock UI: menus, HUD, inventory, map, skill trees, crafting.
- Stable 60 fps on a mid-range PC at the original draw distance.

## Milestones

1. **Data layer** (mostly done): `.rpack`, `.pak`, textures, meshes, materials, map placement, lights, skeletons, static poses. Open: memory-mapped streaming (done for `.rpack`), `.to` vegetation, terrain layer blending, remaining `.exp` classes.
2. **World view**: sky, clouds, weather, vegetation, terrain, water, decals, streaming of neighbouring map regions, LOD.
3. **Animation**: ANM2 animated tracks, blending, IK, ragdoll, facial animation.
4. **Scripts and logic**: the `.scr` script format, game objects and their classes, triggers, spawn points, encounters, quest state machine, save games. This is the largest unknown and decides everything after it.
5. **Physics**: PhysX-cooked collision (`dkcP` blobs) with an open physics engine, character controller, climbing, vehicles.
6. **Gameplay**: player movement and parkour, weapons, crafting, infected and human AI, navigation mesh, night hunters.
7. **Audio**: reverse the sound banks and music format, 3D mixing, dialogue.
8. **Cutscenes and story**: camera tracks, character timelines, subtitles, localisation tables, mission flow of the campaign.
9. **UI**: Scaleform/GFx or the engine's own format for menus and HUD, input remapping, settings.
10. **Polish**: performance, shipping a `--game-dir` launcher, compatibility with game versions and mods.

## Rules

- Clean room: formats come from the user's own files, documented in `docs/formats/`, with verified and guessed parts marked.
- Nothing from the game is committed (`*.rpack`, `*.csb`, captured assets are ignored); screenshots in `docs/img` are the only exception and stay small.
- Each milestone ends with something visible or playable in `oc_viewer` / the runtime, not only a spec.
- Prefer decoding from data over guessing from behaviour; every guess gets a visual or numeric check.

## Order of attack

Streaming (done) → animation tracks → script format → physics collision → player controller → first mission loop → audio → cutscenes → UI → the rest of the campaign.
