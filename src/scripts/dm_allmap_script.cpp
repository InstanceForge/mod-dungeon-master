/*
 * Copyright (C) 2025 AzerothCore - mod-dungeon-master
 *
 * dm_allmap_script.cpp — Fast-path dungeon population trigger.
 * Fires when any session player enters the instance map.
 * The Update() tick in DungeonMasterMgr provides a reliable fallback
 * in case this hook doesn't fire (e.g. async teleport edge cases).
 */

#include "ScriptMgr.h"
#include "Map.h"
#include "Player.h"
#include "DungeonMasterMgr.h"
#include "DMConfig.h"
#include "Chat.h"
#include "Log.h"
#include <cstdio>

using namespace DungeonMaster;

class dm_allmap_script : public AllMapScript
{
public:
    dm_allmap_script() : AllMapScript("dm_allmap_script") {}

    void OnPlayerEnterAll(Map* map, Player* player) override
    {
        if (!sDMConfig->IsEnabled() || !map || !player)
            return;

        Session* session = sDungeonMasterMgr->GetSessionByPlayer(player->GetGUID());
        if (!session || session->State != SessionState::InProgress)
            return;

        if (map->GetId() != session->MapId)
            return;

        // Only populate once — guard against duplicate triggers.
        // The Update tick also triggers populate as a reliable fallback.
        if (session->TotalMobs > 0 || session->TotalBosses > 0)
            return;

        if (!map->IsDungeon())
            return;

        InstanceMap* instance = map->ToInstanceMap();
        if (!instance)
            return;

        session->InstanceId = instance->GetInstanceId();

        ChatHandler(player->GetSession()).SendSysMessage(
            "|cFF00FF00[Dungeon Master]|r Preparing the challenge...");

        sDungeonMasterMgr->PopulateDungeon(session, instance);

        LOG_INFO("module", "DungeonMaster: Session {} — populated via OnPlayerEnterAll (player {}, map {})",
            session->SessionId, player->GetName(), map->GetId());

        char buf[256];
        snprintf(buf, sizeof(buf),
            "|cFF00FF00[Dungeon Master]|r |cFFFFFFFF%u|r enemies and |cFFFFFFFF%u|r boss(es) spawned. "
            "Creature levels: |cFFFFFFFF%u-%u|r. Good luck!",
            session->TotalMobs, session->TotalBosses,
            session->LevelBandMin, session->LevelBandMax);
        ChatHandler(player->GetSession()).SendSysMessage(buf);
    }
};

void AddSC_dm_allmap_script()
{
    new dm_allmap_script();
}
