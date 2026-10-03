#include "core/script.hpp"

#include <cctype>
#include <cstdlib>
#include <set>

namespace oc {

namespace {
struct Tok { enum K { Id, Str, Num, Punct, End } k = End; std::string s; double n = 0; int line = 0; };

bool idChar(char c) { return isalnum((unsigned char)c) || c == '_' || c == '.'; }

std::vector<Tok> lex(const std::string& t) {
    std::vector<Tok> out;
    int line = 1;
    size_t i = 0, n = t.size();
    if (n >= 3 && (uint8_t)t[0] == 0xef && (uint8_t)t[1] == 0xbb && (uint8_t)t[2] == 0xbf) i = 3;
    while (i < n) {
        char c = t[i];
        if (c == '\n') { line++; i++; continue; }
        if (isspace((unsigned char)c)) { i++; continue; }
        if (c == '/' && i + 1 < n && t[i + 1] == '/') { while (i < n && t[i] != '\n') i++; continue; }
        if (c == '/' && i + 1 < n && t[i + 1] == '*') {
            i += 2;
            while (i + 1 < n && !(t[i] == '*' && t[i + 1] == '/')) { if (t[i] == '\n') line++; i++; }
            i += 2;
            continue;
        }
        Tok k;
        k.line = line;
        if (c == '"') {
            k.k = Tok::Str;
            i++;
            while (i < n && t[i] != '"') {
                if (t[i] == '\\' && i + 1 < n && t[i + 1] == '"') i++;
                if (t[i] == '\n') line++;
                k.s += t[i++];
            }
            i++;
        } else if (isdigit((unsigned char)c) || (c == '.' && i + 1 < n && isdigit((unsigned char)t[i + 1]))) {
            char* e;
            k.k = Tok::Num;
            bool hex = t.compare(i, 2, "0x") == 0 || t.compare(i, 2, "0X") == 0;
            k.n = hex ? (double)strtoull(t.c_str() + i, &e, 16) : strtod(t.c_str() + i, &e);
            size_t len = (size_t)(e - (t.c_str() + i));
            if (len == 0) len = 1;
            k.s = t.substr(i, len);
            i += len;
            if (i < n && (t[i] == 'f' || t[i] == 'F')) i++;
        } else if (isalpha((unsigned char)c) || c == '_' || c == '$' || (c == '!' && i + 1 < n && isalpha((unsigned char)t[i + 1]))) {
            k.k = Tok::Id;
            k.s += t[i++];
            for (;;) {
                if (i < n && idChar(t[i])) k.s += t[i++];
                else if (i + 2 < n && t[i] == ':' && t[i + 1] == ':' && idChar(t[i + 2])) { k.s += "::"; i += 2; }
                else break;
            }
        } else {
            k.k = Tok::Punct;
            k.s = std::string(1, c);
            i++;
            if (i < n && ((c == '=' && t[i] == '=') || (c == '!' && t[i] == '=') || (c == '<' && t[i] == '=') || (c == '>' && t[i] == '=') ||
                          (c == '&' && t[i] == '&') || (c == '|' && t[i] == '|')))
                k.s += t[i++];
        }
        out.push_back(std::move(k));
    }
    out.push_back(Tok{});
    return out;
}

struct Parser {
    std::vector<Tok> t;
    size_t p = 0;
    int errors = 0;

    const Tok& cur() const { return t[p]; }
    bool end() const { return t[p].k == Tok::End; }
    bool isP(const char* s) const { return t[p].k == Tok::Punct && t[p].s == s; }

    static ScrValue value(const std::vector<Tok>& v) {
        ScrValue r;
        if (v.size() == 1 && v[0].k == Tok::Str) { r.kind = ScrValue::Str; r.text = v[0].s; }
        else if (v.size() == 1 && v[0].k == Tok::Num) { r.kind = ScrValue::Num; r.num = v[0].n; r.text = v[0].s; }
        else if (v.size() == 2 && v[0].k == Tok::Punct && v[0].s == "-" && v[1].k == Tok::Num) { r.kind = ScrValue::Num; r.num = -v[1].n; r.text = "-" + v[1].s; }
        else if (v.size() == 1 && v[0].k == Tok::Id) { r.kind = ScrValue::Ident; r.text = v[0].s; }
        else {
            for (auto& x : v) {
                if (!r.text.empty()) r.text += ' ';
                r.text += x.k == Tok::Str ? "\"" + x.s + "\"" : x.s;
            }
        }
        return r;
    }

    // comma separated expressions up to the matching ')' (cursor on '('); leaves the cursor after it
    std::vector<ScrValue> args() {
        std::vector<ScrValue> out;
        p++;
        std::vector<Tok> item;
        int depth = 0;
        while (!end()) {
            if (isP("(") || isP("[")) depth++;
            if (isP(")") || isP("]")) {
                if (depth == 0) break;
                depth--;
            }
            if (depth == 0 && isP(",")) { out.push_back(value(item)); item.clear(); p++; continue; }
            item.push_back(t[p++]);
        }
        if (!item.empty()) out.push_back(value(item));
        if (isP(")")) p++;
        return out;
    }

    void skipBalanced() {  // cursor on '{'
        int depth = 0;
        while (!end()) {
            if (isP("{")) depth++;
            else if (isP("}") && --depth == 0) { p++; return; }
            p++;
        }
    }

    void skipStatement() {
        int depth = 0;
        while (!end()) {
            if (isP("{")) { skipBalanced(); return; }
            if (isP("(")) depth++;
            if (isP(")")) depth--;
            if (depth <= 0 && isP(";")) { p++; return; }
            if (isP("}")) return;
            p++;
        }
    }

    // tokens up to a top-level ';' or ',' (or '}' when `stopAtBrace`), cursor left on the terminator
    std::vector<Tok> untilEnd(bool stopComma) {
        std::vector<Tok> v;
        int d = 0;
        while (!end()) {
            if (isP("(") || isP("[")) d++;
            if (isP(")") || isP("]")) d--;
            if (d <= 0 && (isP(";") || isP("}") || (stopComma && isP(",")))) break;
            v.push_back(t[p++]);
        }
        return v;
    }

    void block(ScrNode& n) {  // cursor on '{'
        p++;
        n.hasBlock = true;
        while (!end() && !isP("}")) statement(n.kids);
        if (isP("}")) p++;
    }

    void statement(std::vector<ScrNode>& out) {
        static const std::set<std::string> types = {"int", "float", "string", "bool", "var", "table", "void", "vec2", "vec3", "vec4", "mat3", "mat4", "double", "uint"};
        static const std::set<std::string> controls = {"for", "if", "while", "switch", "foreach", "do", "else", "return", "break", "continue", "case", "default"};
        if (isP(";")) { p++; return; }
        ScrNode n;
        n.line = cur().line;
        if (isP("{")) { n.kind = ScrNode::Control; n.name = "block"; block(n); out.push_back(std::move(n)); return; }
        if (cur().k != Tok::Id) {
            n.kind = ScrNode::Skipped; n.name = "?";
            errors++;
            size_t before = p;
            skipStatement();
            if (p == before) p++;
            out.push_back(std::move(n));
            return;
        }
        std::string id = cur().s;
        if (id == "use") {  // "use Name(args);" pulls in another definition
            n.kind = ScrNode::Import; n.type = id;
            p++;
            if (cur().k == Tok::Id) { n.name = cur().s; p++; } else if (cur().k == Tok::Str) { n.name = cur().s; p++; }
            if (isP("(")) n.args = args();
            if (isP(";")) p++;
            out.push_back(std::move(n));
            return;
        }
        if (id == "export" || id == "extern") {
            n.exported = id == "export";
            p++;
            if (cur().k != Tok::Id) { errors++; skipStatement(); return; }
            id = cur().s;
        }
        if (id == "import" || id == "include" || id == "!include" || id == "!import") {
            n.kind = ScrNode::Import; n.type = id;
            p++;
            bool paren = isP("(");
            if (paren) p++;
            if (cur().k == Tok::Str) { n.name = cur().s; p++; }
            if (paren) { while (!end() && !isP(")")) p++; if (isP(")")) p++; }
            if (isP(";")) p++;
            out.push_back(std::move(n));
            return;
        }
        if (id == "sub") {
            n.kind = ScrNode::Sub;
            p++;
            if (cur().k == Tok::Id) { n.name = cur().s; p++; }
            if (isP("(")) n.args = args();
            if (isP("{")) block(n);
            out.push_back(std::move(n));
            return;
        }
        if (types.count(id)) {
            n.kind = ScrNode::Decl; n.type = id;
            p++;
            if (cur().k == Tok::Id) { n.name = cur().s; p++; }
            if (isP("(")) {  // function-style "float f(...) { }"
                n.args = args();
                if (isP("{")) block(n);
            } else {
                if (isP("=")) { p++; auto init = untilEnd(true); if (!init.empty()) n.args.push_back(value(init)); }
                while (isP(",")) { p++; untilEnd(true); }  // further names of the same declaration are dropped
                if (isP(";")) p++;
            }
            out.push_back(std::move(n));
            return;
        }
        if (controls.count(id)) {
            n.kind = ScrNode::Control; n.name = id;
            p++;
            if (isP("(")) {
                p++;
                std::vector<Tok> c;
                int d = 0;
                while (!end() && !(d == 0 && isP(")"))) { if (isP("(")) d++; if (isP(")")) d--; c.push_back(t[p++]); }
                if (isP(")")) p++;
                if (!c.empty()) n.args.push_back(value(c));
            }
            if (isP("{")) block(n);
            else if (id == "return") { untilEnd(false); if (isP(";")) p++; }
            else if (id != "break" && id != "continue" && !end() && !isP("}")) statement(n.kids);
            else if (isP(";")) p++;
            out.push_back(std::move(n));
            return;
        }
        // call, assignment or bare identifier
        p++;
        n.name = id;
        if (isP("(")) {
            n.kind = ScrNode::Call;
            n.args = args();
            if (isP("{")) block(n);
            if (isP(";")) p++;
        } else if (isP("=") || ((isP("+") || isP("-") || isP("*") || isP("/")) && t[p + 1].k == Tok::Punct && t[p + 1].s == "=")) {
            n.kind = ScrNode::Assign;
            if (!isP("=")) p++;
            p++;
            auto v = untilEnd(false);
            if (!v.empty()) n.args.push_back(value(v));
            if (isP(";")) p++;
        } else if (isP("{")) {
            n.kind = ScrNode::Call;
            block(n);
        } else {
            n.kind = ScrNode::Skipped;
            errors++;
            skipStatement();
        }
        out.push_back(std::move(n));
    }
};
}  // namespace

ScrFile parseScript(const std::string& text) {
    Parser ps;
    ps.t = lex(text);
    ScrFile f;
    while (!ps.end()) {
        if (ps.isP("}")) { ps.p++; ps.errors++; continue; }
        ps.statement(f.nodes);
    }
    f.errors = ps.errors;
    return f;
}

}  // namespace oc
