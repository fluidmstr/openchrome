// Parser for the engine's text scripts (*.scr), see docs/formats/scr.md
#pragma once
#include <string>
#include <vector>

namespace oc {

struct ScrValue {
    enum Kind { Str, Num, Ident, Expr } kind = Expr;
    std::string text;
    double num = 0;
};

struct ScrNode {
    enum Kind { Call, Sub, Decl, Import, Control, Assign, Skipped } kind = Call;
    std::string name;             // call/sub/variable name, import path, control keyword
    std::string type;             // Decl: declared type; Import: import/include keyword
    bool exported = false;
    int line = 0;
    std::vector<ScrValue> args;   // call arguments, sub parameters, declaration initialiser, control condition
    std::vector<ScrNode> kids;    // block body
    bool hasBlock = false;
};

struct ScrFile {
    std::vector<ScrNode> nodes;
    int errors = 0;               // constructs the parser had to skip
};

// Lenient: never throws, unparsable constructs are recorded as Skipped nodes and counted in `errors`.
ScrFile parseScript(const std::string& text);

}  // namespace oc
