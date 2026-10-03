// Inventory item definitions from data/scripts/inventory/*.scr, see docs/formats/items.md
#pragma once
#include <map>
#include <string>
#include <vector>

#include "core/script.hpp"

namespace oc {

struct ItemDef {
    std::string id, category;  // Item("Craft_Upgrade_Dam", CategoryType_CraftPart)
    std::string base;          // Item("Name", "OtherItem"): derived from another item (second argument is a string)
    std::multimap<std::string, std::vector<ScrValue>> props;  // Mesh("x.msh"), MaxStackCount(30), Color(Color_Green), ...
    const ScrValue* first(const std::string& prop, size_t arg = 0) const {
        auto i = props.find(prop);
        return i == props.end() || arg >= i->second.size() ? nullptr : &i->second[arg];
    }
};

// Collects every top-level `Item(id, category) { ... }` found in the `sub main` bodies of a parsed script.
void collectItems(const ScrFile& f, std::vector<ItemDef>& out);

}  // namespace oc
