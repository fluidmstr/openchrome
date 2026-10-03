# Scripts (`*.scr`)

About 3900 files in `DW/Data*.pak` (28 MB), mostly plain text (Latin-1/UTF-8, CRLF). Eleven are binary (`*_hooks.scr`, float tables) and ~100 under `editor/` are UTF-16 (BOM `ff fe`); these are not parsed. Parser: `src/core/script.{hpp,cpp}`; `oc_scrstat <DW dir> [-v]` parses all of them and lists failures (3825 text files, 477k nodes, 3035 distinct call names; 4 files still have one skipped construct).

## Language (as observed)

C-like, but used mostly as a *data description* format: a file is a list of statements, each a call that may carry a block.

```
import "other.scr"            // also !include("x.scr"), include(...), use Name(args);
sub main() { ... }            // entry point of most files
Name(arg, "str", 1.5) { child(...); child(...) }   // call with optional block; ';' optional
export string x = "..." ;     // declarations: int float string bool var table vec2..4; export/extern prefixes
$MACRO (s, "text")            // $-identifiers are macro calls (animscripts)
if / for / while / switch / return    // rare: ~70 if, ~250 for in the whole set
```

Comments `//` and `/* */`. Strings are double-quoted, backslashes are literal (Windows paths), `\"` escapes a quote. Numbers: decimal, exponent, `0x` hex, optional `f` suffix. Identifiers may contain `.` and `::` (`EUsageType::Normal`). Arguments are comma-separated expressions; bare identifiers (`_MESH_`) and operator expressions are kept as text.

## Parser output

`ScrFile{nodes, errors}`; `ScrNode{kind (Call, Sub, Decl, Import, Control, Assign, Skipped), name, type, args, kids, line}`. Semantics (what `Quest`, `Event`, `Phase`, `Item`, `SpawnPoint`, `SeqTrack`... mean) are not interpreted yet; the most frequent calls over all files: `Event` 78k, `Phase` 25k, `Quest` 16k, `Item` 11k, `Chatter` 7k, `Sound` 7k.
