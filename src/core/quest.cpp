#include "core/quest.hpp"

#include <cctype>

namespace oc {

namespace {
void decode(std::string& s) {
    static const std::pair<const char*, char> ents[] = {{"&lt;", '<'}, {"&gt;", '>'}, {"&quot;", '"'}, {"&apos;", '\''}, {"&amp;", '&'}};
    for (auto& e : ents) {
        std::string from = e.first;
        for (size_t p = s.find(from); p != std::string::npos; p = s.find(from, p + 1)) s.replace(p, from.size(), 1, e.second);
    }
}

struct XmlReader {
    const std::string& t;
    size_t i = 0;
    explicit XmlReader(const std::string& s) : t(s) {}

    void skipWs() { while (i < t.size() && isspace((unsigned char)t[i])) i++; }
    bool skipPast(const char* s) {
        size_t p = t.find(s, i);
        if (p == std::string::npos) { i = t.size(); return false; }
        i = p + std::string(s).size();
        return true;
    }
    static bool nameChar(char c) { return isalnum((unsigned char)c) || c == '_' || c == ':' || c == '-' || c == '.'; }

    // cursor on '<' of an element start tag
    bool element(XmlNode& n, int depth) {
        i++;
        size_t b = i;
        while (i < t.size() && nameChar(t[i])) i++;
        n.tag = t.substr(b, i - b);
        for (;;) {
            skipWs();
            if (i >= t.size()) return false;
            if (t[i] == '/') { i++; skipWs(); if (i < t.size() && t[i] == '>') i++; return true; }
            if (t[i] == '>') { i++; break; }
            size_t kb = i;
            while (i < t.size() && nameChar(t[i])) i++;
            if (i == kb) { i++; continue; }
            std::string key = t.substr(kb, i - kb), val;
            skipWs();
            if (i < t.size() && t[i] == '=') {
                i++; skipWs();
                if (i < t.size() && (t[i] == '"' || t[i] == '\'')) {
                    char q = t[i++];
                    size_t vb = i;
                    while (i < t.size() && t[i] != q) i++;
                    val = t.substr(vb, i - vb);
                    i++;
                }
            }
            decode(val);
            n.attr[key] = val;
        }
        while (i < t.size()) {
            size_t lt = t.find('<', i);
            if (lt == std::string::npos) { i = t.size(); return true; }
            i = lt;
            if (t.compare(i, 4, "<!--") == 0) { skipPast("-->"); continue; }
            if (t.compare(i, 9, "<![CDATA[") == 0) { skipPast("]]>"); continue; }
            if (t[i + 1] == '?' || t[i + 1] == '!') { skipPast(">"); continue; }
            if (t[i + 1] == '/') { skipPast(">"); return true; }
            if (depth > 200) return false;
            XmlNode kid;
            if (!element(kid, depth + 1)) return false;
            n.kids.push_back(std::move(kid));
        }
        return true;
    }
};
}  // namespace

bool parseXml(const std::string& text, XmlNode& root) {
    XmlReader r(text);
    while (r.i < text.size()) {
        size_t lt = text.find('<', r.i);
        if (lt == std::string::npos) return false;
        r.i = lt;
        if (text.compare(r.i, 4, "<!--") == 0) { r.skipPast("-->"); continue; }
        if (text[r.i + 1] == '?' || text[r.i + 1] == '!') { r.skipPast(">"); continue; }
        return r.element(root, 0);
    }
    return false;
}

static QuestPhase convertPhase(const XmlNode& p) {
    QuestPhase ph;
    ph.type = p.get("type");
    ph.name = p.get("name");
    ph.attr = p.attr;
    for (const char* k : {"type", "name", "pxsl_line"}) ph.attr.erase(k);
    for (auto& c : p.kids) {
        if (c.tag != "Path") { ph.objects.push_back(c); continue; }
        ph.paths.emplace_back();
        for (auto& s : c.kids)
            if (s.tag == "Phase") ph.paths.back().push_back(convertPhase(s));
    }
    return ph;
}

bool loadQuests(const std::string& xmlText, QuestFile& out) {
    XmlNode root;
    if (!parseXml(xmlText, root) || root.tag != "QuestsDefinitions") return false;
    out.level = root.get("level");
    for (auto& q : root.kids) {
        if (q.tag != "Quest") continue;
        Quest quest;
        quest.name = q.get("name");
        quest.parent = q.get("parent");
        quest.attr = q.attr;
        for (const char* k : {"name", "parent", "pxsl_line"}) quest.attr.erase(k);
        for (auto& p : q.kids)
            if (p.tag == "Phase") quest.phases.push_back(convertPhase(p));
        out.quests.push_back(std::move(quest));
    }
    return true;
}

}  // namespace oc
