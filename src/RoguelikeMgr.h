/*
 * mod-dungeon-master — Roguelike run manager
 * Copyright (C) 2025 — AGPL v3
 */

#ifndef ROGUELIKE_MGR_H
#define ROGUELIKE_MGR_H

#include "RoguelikeTypes.h"
#include <mutex>
#include <unordered_map>

class Player;
class Group;

namespace DungeonMaster
{

class RoguelikeMgr
{
    RoguelikeMgr();
    ~RoguelikeMgr();

public:
    static RoguelikeMgr* Instance();

    void Initialize();

    // Run lifecycle
    bool StartRun(Player* leader, uint32 difficultyId, uint32 themeId, bool scaleToParty = true);
    void OnDungeonCompleted(uint32 runId, uint32 sessionId);
    void OnPartyWipe(uint32 runId);
    void EndRun(uint32 runId, bool announceResults);
    void AbandonRun(uint32 runId);
    void QuitRun(ObjectGuid playerGuid);

    // Queries
    RoguelikeRun* GetRun(uint32 runId);
    RoguelikeRun* GetRunBySession(uint32 sessionId);
    RoguelikeRun* GetRunByPlayer(ObjectGuid playerGuid);
    uint32        GetRunIdBySession(uint32 sessionId) const;

    // Tier scaling
    float GetTierHealthMultiplier(uint32 runId) const;
    float GetTierDamageMultiplier(uint32 runId) const;
    float GetTierArmorMultiplier(uint32 runId) const;
    void  GetAffixMultipliers(uint32 runId, bool isBoss, bool isElite,
                              float& outHpMult, float& outDmgMult,
                              float& outEliteChanceMult) const;

    // Buff system
    void IncrementBuffStacks(uint32 runId);
    void ApplyBuffStacks(Player* player, uint32 runId);
    void RemoveBuffStacks(Player* player, uint32 runId);
    void ApplyBuffAura(Player* player, uint32 stacks);

    static constexpr float BUFF_PCT_PER_STACK = 10.0f;

    void Update(uint32 diff);

    uint32 GetActiveRunCount() const;
    bool   IsPlayerInRun(ObjectGuid guid) const;

    // Leaderboard
    void SaveRoguelikeLeaderboard(const RoguelikeRun& run);
    std::vector<RoguelikeLeaderboardEntry> GetRoguelikeLeaderboard(uint32 limit = 10) const;

private:
    void BuildAffixPool();
    void SelectAffixesForTier(RoguelikeRun& run);
    uint32 SelectRandomDungeon(const RoguelikeRun& run) const;
    bool TransitionToNextDungeon(RoguelikeRun& run);
    void TeleportRunPlayersOut(RoguelikeRun& run);
    void AnnounceCountdown(const RoguelikeRun& run, uint32 remainingSec);
    void AnnounceToRun(const RoguelikeRun& run, const char* msg);

    std::unordered_map<uint32, RoguelikeRun>   _activeRuns;
    std::unordered_map<uint32, uint32>          _sessionToRun;  // sessionId -> runId
    std::unordered_map<ObjectGuid, uint32>      _playerToRun;   // guid -> runId
    uint32 _nextRunId = 1;
    mutable std::mutex _runMutex;

    std::vector<AffixDef> _affixDefs;

    uint32 _updateTimer = 0;
    static constexpr uint32 UPDATE_INTERVAL = 1000;
};

} // namespace DungeonMaster

#define sRoguelikeMgr DungeonMaster::RoguelikeMgr::Instance()

#endif
