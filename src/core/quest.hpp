// Quest definitions (data/quests/**/*.xml), see docs/formats/quests.md
#pragma once
#include <map>
#include <string>
#include <vector>

namespace oc {

// Generic XML element: attributes and child elements (text and comments are dropped).
struct XmlNode {
    std::string tag;
    std::map<std::string, std::string> attr;
    std::vector<XmlNode> kids;
    const std::string& get(const std::string& k) const { static const std::string none; auto i = attr.find(k); return i == attr.end() ? none : i->second; }
};

// Lenient XML reader; returns false if no root element was found.
bool parseXml(const std::string& text, XmlNode& root);

struct QuestPhase {
    std::string type, name;
    std::map<std::string, std::string> attr;   // everything else (distance, mode, state, speaker, ...)
    std::vector<XmlNode> objects;              // Destination, Trigger, Spawner, ... children
};

struct Quest {
    std::string name, parent;
    std::map<std::string, std::string> attr;   // glued, reward_set, quest_giver, difficulty, ...
    std::vector<QuestPhase> phases;
};

struct QuestFile {
    std::string level;
    std::vector<Quest> quests;
};

// False if the file is not a <QuestsDefinitions> document.
bool loadQuests(const std::string& xmlText, QuestFile& out);

}  // namespace oc
