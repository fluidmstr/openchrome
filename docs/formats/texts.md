# Localisation tables (`data/maps/*_texts_*.bin` in `Data<Lang>.pak`)

Every language pak (`DataEn.pak`, `DataRu.pak`, ...) carries the same set of tables: `common_texts_all.bin` (largest, ~2.2 MB in English), per-platform `common_texts_pc|ps4|xbo.bin`, and per-map `<map>_texts_all.bin`. Dialogue audio lives in the separate `Speech<Lang>.pak`.

## Layout (verified: all 595 tables across all languages parse exactly to their last byte)

```
u32 version = 1
u32 count
count x { u16 keyLength; char key[keyLength] (latin1); u16 textLength; u16 text[textLength] (UTF-16LE, no terminator) }
```

Keys are identifiers such as the speaker/dialogue ids the quest and script data refer to (quest names appear in quests as `&Q_..._Name&` keys, items by `Name`/`Description` properties). 424 694 entries in total over all languages.

Code: `src/core/texts.{hpp,cpp}` (`parseTexts`, `toUtf8`); `oc_textstat <DW dir> [key]` checks every table or prints one key in every language.

Open: format of the markup inside strings (the `&key&` references and control characters), and which table a given key lives in (lookup order common -> platform -> map).
