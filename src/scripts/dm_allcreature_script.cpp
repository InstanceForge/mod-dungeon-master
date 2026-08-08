/*
 * mod-dungeon-master - dm_allcreature_script.cpp
 *
 * Keeps Dungeon Master creature levels from being reverted, and clamps adds
 * summoned by Dungeon Master creatures.
 *
 * Two independent mechanisms revert an assigned level:
 *
 *   1. Creature::SelectLevel() re-rolls urand(minlevel, maxlevel) from
 *      creature_template. It runs on the respawn path and from
 *      UpdateEntry(updateAI = true). OnBeforeCreatureSelectLevel lets the module
 *      supply the owned level instead.
 *
 *   2. Another module calling SetLevel() from its own update hook. mod-autobalance
 *      is the common case: it hooks OnPlayerLevelChanged, marks every creature on
 *      the map stale, and ResetCreatureIfNeeded() then calls
 *      SetLevel(UnmodifiedLevel) — the template value. This is why Dungeon Master
 *      mobs sprout a skull the moment a party member levels up.
 *
 * Case 2 is a plain SetLevel() from outside this module, so no core hook can
 * intercept it. The level is therefore re-asserted on this module's own update
 * tick, which makes the fix self-healing and independent of script registration
 * order — it behaves the same whether or not AutoBalance is present. Correcting
 * on the following tick is imperceptible in play.
 */

#include "ScriptMgr.h"
#include "Creature.h"
#include "Map.h"
#include "Player.h"
#include "TemporarySummon.h"
#include "DungeonMasterMgr.h"
#include "DMConfig.h"

using namespace DungeonMaster;

class dm_allcreature_script : public AllCreatureScript
{
public:
    dm_allcreature_script() : AllCreatureScript("dm_allcreature_script") {}

    // Runs inside Creature::SelectLevel(), before the rolled level is applied.
    void OnBeforeCreatureSelectLevel(CreatureTemplate const* /*cinfo*/, Creature* creature, uint8& level) override
    {
        if (!creature || !sDMConfig->IsEnabled() || !DungeonMasterMgr::HasOwnedCreatures())
            return;

        uint8  owned   = 0;
        uint32 ownedHp = 0;
        if (sDungeonMasterMgr->GetOwnedCreature(creature->GetGUID(), owned, ownedHp) && owned)
            level = owned;
    }

    // Clamp adds summoned by Dungeon Master creatures. Player pets, totems and
    // guardians are left alone — they belong to the player, not the dungeon.
    void OnCreatureAddWorld(Creature* creature) override
    {
        if (!creature || !sDMConfig->IsEnabled())
            return;

        Map* map = creature->GetMap();
        if (!map || !map->IsDungeon())
            return;

        uint32 instanceId = map->GetInstanceId();
        if (!instanceId)
            return;

        if (creature->IsPet() || creature->IsTotem() || creature->IsGuardian())
            return;

        if (creature->GetCharmerOrOwnerPlayerOrPlayerItself())
            return;

        // Already owned (the module's own spawn path registers during scaling).
        uint8  owned   = 0;
        uint32 ownedHp = 0;
        if (sDungeonMasterMgr->GetOwnedCreature(creature->GetGUID(), owned, ownedHp))
            return;

        Session* session = sDungeonMasterMgr->GetSessionByInstance(instanceId);
        if (!session || !session->IsActive())
            return;

        if (creature->GetLevel() == session->EffectiveLevel)
            return;

        sDungeonMasterMgr->ScaleSummonedCreature(creature, session);
    }

    // Re-assert against silent external SetLevel() calls.
    void OnAllCreatureUpdate(Creature* creature, uint32 /*diff*/) override
    {
        if (!creature || !sDMConfig->IsEnabled() || !DungeonMasterMgr::HasOwnedCreatures())
            return;

        // Cheap gate: creatures are only ever owned inside instanced dungeons.
        Map* map = creature->GetMap();
        if (!map || !map->IsDungeon() || !map->GetInstanceId())
            return;

        uint8  owned   = 0;
        uint32 ownedHp = 0;
        if (!sDungeonMasterMgr->GetOwnedCreature(creature->GetGUID(), owned, ownedHp) || !owned)
            return;

        if (creature->GetLevel() == owned)
            return;

        creature->SetLevel(owned);

        // SetLevel() alone leaves the creature with whatever max health the
        // reverting module computed for the template level, so restore that too.
        if (ownedHp && creature->GetMaxHealth() != ownedHp)
        {
            uint32 cur = creature->GetHealth();
            creature->SetMaxHealth(ownedHp);
            creature->SetHealth(std::min(cur ? cur : ownedHp, ownedHp));
        }
    }

    void OnCreatureRemoveWorld(Creature* creature) override
    {
        if (!creature || !DungeonMasterMgr::HasOwnedCreatures())
            return;

        sDungeonMasterMgr->ForgetOwnedCreature(creature->GetGUID());
    }
};

void AddSC_dm_allcreature_script()
{
    new dm_allcreature_script();
}
