// Quest interpreter: runs the phase tree of a quest. Semantics (guessed from the data, see docs/formats/quests.md):
// a quest and every <Path> is a sequence of phases, AND runs its paths in parallel and finishes when all are done,
// OR finishes when one is done; leaf phases either act at once or wait for the game (see isWaiting).
#pragma once
#include <map>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "core/quest.hpp"

namespace oc {

struct QuestHost {
    virtual ~QuestHost() = default;
    virtual void run(const Quest& q, const QuestPhase& p) = 0;           // phases that act at once (enable, appear, set weather, ...)
    virtual bool done(const Quest& q, const QuestPhase& p, float dt) = 0;  // waiting phases: true once the game satisfied them
};

// go to, checkpoint, talk, use, kill, wait guard, ... : phases that block their sequence until the host reports them done.
bool isWaiting(const std::string& type);

struct QuestExec;

class QuestRunner {
public:
    QuestRunner(const Quest& q, QuestHost& host);
    ~QuestRunner();
    // Advances by dt seconds; true once the quest is finished.
    bool update(float dt);
    bool finished() const { return finished_; }
    const Quest& quest() const { return *quest_; }

private:
    const Quest* quest_;
    QuestHost* host_;
    std::unique_ptr<QuestExec> root_;
    bool finished_ = false;
};

// Owns the loaded quests of a game and runs the active ones. A quest becomes startable when its parent has finished
// ("game_root" is always finished).
class QuestManager : public QuestHost {
public:
    void add(const QuestFile& f);
    const Quest* find(const std::string& name) const;
    const std::string& levelOf(const std::string& quest) const { static const std::string none; auto i = levels_.find(quest); return i == levels_.end() ? none : i->second; }
    bool start(const std::string& name);
    bool started(const std::string& name) const { return runners_.count(name) > 0; }
    bool finished(const std::string& name) const { return done_.count(name) > 0; }
    std::vector<const Quest*> startable() const;  // not started, parent finished
    void update(float dt);
    size_t active() const { return runners_.size() - done_.size(); }

    // Host side: effects and conditions are reported here and decided by `onRun` / `onWait` (defaults: log nothing, satisfy waits after 1 s).
    std::function<void(const Quest&, const QuestPhase&)> onRun;
    std::function<bool(const Quest&, const QuestPhase&, float)> onWait;

    void run(const Quest& q, const QuestPhase& p) override { if (onRun) onRun(q, p); }
    bool done(const Quest& q, const QuestPhase& p, float dt) override;

private:
    std::map<std::string, Quest> quests_;
    std::map<std::string, std::string> levels_;  // quest -> map it is defined for
    std::map<std::string, std::unique_ptr<QuestRunner>> runners_;
    std::set<std::string> done_;
    std::map<const QuestPhase*, float> timers_;
};

}  // namespace oc
