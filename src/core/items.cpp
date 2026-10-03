#include "core/items.hpp"

namespace oc {

namespace {
void scan(const std::vector<ScrNode>& v, std::vector<ItemDef>& out) {
    for (const ScrNode& n : v) {
        if (n.kind == ScrNode::Sub) { scan(n.kids, out); continue; }
        if (n.kind != ScrNode::Call || n.name != "Item" || n.args.empty()) continue;
        ItemDef d;
        d.id = n.args[0].text;
        if (n.args.size() > 1) (n.args[1].kind == ScrValue::Str ? d.base : d.category) = n.args[1].text;
        for (const ScrNode& p : n.kids)
            if (p.kind == ScrNode::Call) d.props.emplace(p.name, p.args);
        out.push_back(std::move(d));
    }
}
}  // namespace

void collectItems(const ScrFile& f, std::vector<ItemDef>& out) { scan(f.nodes, out); }

}  // namespace oc
