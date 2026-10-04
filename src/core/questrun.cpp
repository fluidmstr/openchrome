#include "core/questrun.hpp"

#include <cstdlib>
#include <filesystem>

#include "core/zip.hpp"

namespace oc {

bool isWaiting(const std::string& t) {
    static const std::set<std::string> w = {"checkpoint", "go to", "kill", "use", "talk", "use life place", "movie", "take item", "loot container",
                                            "clear area", "ensure spawner clear", "fill area", "wait guard"};
    return w.count(t) || (t.size() > 6 && t.compare(t.size() - 6, 6, " guard") == 0);
}

struct QuestExec {
    const QuestPhase* ph = nullptr;       // null for the quest's own sequence
    const std::vector<QuestPhase>* seq = nullptr;
    // sequence state (quest root or one path)
    size_t pos = 0;
    std::unique_ptr<QuestExec> cur;
    // AND / OR: one sequence per path
    std::vector<std::unique_ptr<QuestExec>> paths;
    bool started = false;
    bool done = false;
};

QuestRunner::QuestRunner(const Quest& q, QuestHost& host) : quest_(&q), host_(&host), root_(new QuestExec) { root_->seq = &q.phases; }
QuestRunner::~QuestRunner() = default;

// Steps one node; returns true when it is complete.

static bool step(const Quest& q, QuestHost& host, QuestExec& e, float dt);

bool QuestRunner::update(float dt) {
    if (!finished_) finished_ = step(*quest_, *host_, *root_, dt);
    return finished_;
}

static bool step(const Quest& q, QuestHost& host, QuestExec& e, float dt) {
    if (e.done) return true;
    if (e.seq) {  // sequence: run phases in order; instant ones chain within the same update
        while (e.pos < e.seq->size()) {
            if (!e.cur) { e.cur.reset(new QuestExec); e.cur->ph = &(*e.seq)[e.pos]; }
            if (!step(q, host, *e.cur, dt)) return false;
            e.cur.reset();
            e.pos++;
        }
        return e.done = true;
    }
    const QuestPhase& p = *e.ph;
    if (p.type == "AND" || p.type == "OR") {
        if (!e.started) {
            e.started = true;
            for (auto& path : p.paths) { e.paths.emplace_back(new QuestExec); e.paths.back()->seq = &path; }
        }
        bool all = true, any = false;
        for (auto& pe : e.paths) { bool d = step(q, host, *pe, dt); all &= d; any |= d; }
        if (p.paths.empty() || (p.type == "AND" ? all : any)) return e.done = true;
        return false;
    }
    if (!isWaiting(p.type)) { host.run(q, p); return e.done = true; }
    return e.done = host.done(q, p, dt);
}

void QuestManager::add(const QuestFile& f) {
    for (const Quest& q : f.quests) { quests_.emplace(q.name, q); levels_.emplace(q.name, f.level); }
}

void QuestManager::loadAll(const std::string& dwDir) {
    namespace fs = std::filesystem;
    for (auto& e : fs::directory_iterator(dwDir)) {
        std::string f = e.path().filename().string();
        if (e.path().extension() != ".pak" || f.rfind("Data", 0) != 0) continue;
        for (auto& name : listZip(e.path().string())) {
            if (name.rfind("data/quests/", 0) != 0 || name.size() < 4 || name.compare(name.size() - 4, 4, ".xml") != 0 || name.find("_underlay") != std::string::npos) continue;
            std::vector<uint8_t> b;
            QuestFile qf;
            if (readZipEntry(e.path().string(), name, b) && loadQuests(std::string(b.begin(), b.end()), qf)) add(qf);
        }
    }
}

const Quest* QuestManager::find(const std::string& name) const {
    auto it = quests_.find(name);
    return it == quests_.end() ? nullptr : &it->second;
}

bool QuestManager::start(const std::string& name) {
    const Quest* q = find(name);
    if (!q || runners_.count(name)) return false;
    runners_[name].reset(new QuestRunner(*q, *this));
    return true;
}

std::vector<const Quest*> QuestManager::startable() const {
    std::vector<const Quest*> out;
    for (auto& [n, q] : quests_)
        if (!runners_.count(n) && (q.parent == "game_root" || q.parent.empty() || done_.count(q.parent))) out.push_back(&q);
    return out;
}

void QuestManager::update(float dt) {
    for (auto& [n, r] : runners_)  // runners_ is not modified while stepping: quests started by phases are picked up next frame
        if (!done_.count(n) && r->update(dt)) done_.insert(n);
}

bool QuestManager::done(const Quest& q, const QuestPhase& p, float dt) {
    if (p.type == "started quest guard") {
        const std::string& n = p.attr.count("quest_name") ? p.attr.at("quest_name") : std::string();
        bool wantFinished = !p.attr.count("finished") || p.attr.at("finished") == "true";
        return wantFinished ? finished(n) : started(n);
    }
    if (p.type == "wait guard") {
        float t = timers_[&p] += dt;
        return t >= (p.attr.count("time") ? (float)atof(p.attr.at("time").c_str()) : 0.0f);
    }
    if (onWait) return onWait(q, p, dt);
    return (timers_[&p] += dt) >= 1.0f;
}

}  // namespace oc
