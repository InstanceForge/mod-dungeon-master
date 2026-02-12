/*
 * mod-dungeon-master — dm_unit_script.cpp
 * Scales environmental/hazard damage for level-scaled sessions.
 */

#include "ScriptMgr.h"
#include "Player.h"
#include "Creature.h"
#include "SpellInfo.h"
#include "DungeonMasterMgr.h"
#include "DMConfig.h"

using namespace DungeonMaster;

// Maximum percentage of player max HP that a single non-session damage
static constexpr float ENV_DAMAGE_MAX_PCT = 0.03f;

class dm_unit_script : public UnitScript
{
public:
    dm_unit_script() : UnitScript("dm_unit_script") {}

    // -- Periodic aura ticks (Toxic Spores, poison clouds, fire patches) --
    void ModifyPeriodicDamageAurasTick(Unit* target, Unit* attacker, uint32& damage, SpellInfo const* /*spellInfo*/) override
    {
        ScaleEnvDamage(target, attacker, damage);
    }

    // -- Direct spell damage (bolts from stalkers, trap triggers) --
    void ModifySpellDamageTaken(Unit* target, Unit* attacker, int32& damage, SpellInfo const* /*spellInfo*/) override
    {
        if (damage <= 0) return;
        uint32 udmg = static_cast<uint32>(damage);
        ScaleEnvDamage(target, attacker, udmg);
        damage = static_cast<int32>(udmg);
    }

    // -- Melee damage (covers edge cases from env hazards) --
    void ModifyMeleeDamage(Unit* target, Unit* attacker, uint32& damage) override
    {
        ScaleEnvDamage(target, attacker, damage);
    }

    // Reliable creature death hook — fires for ALL unit deaths regardless of AI.
    // Bosses keep their native ScriptName AI for proper mechanics, so our
    // DungeonMasterCreatureAI::JustDied may never fire. This catches every kill.
    void OnUnitDeath(Unit* unit, Unit* killer) override
    {
        if (!sDMConfig->IsEnabled() || !unit)
            return;

        Creature* creature = unit->ToCreature();
        if (!creature)
            return;

        // Find session via killer (player or player's pet)
        Player* player = nullptr;
        if (killer)
        {
            player = killer->ToPlayer();
            if (!player && killer->GetOwner())
                player = killer->GetOwner()->ToPlayer();
        }

        Session* session = nullptr;
        if (player)
            session = sDungeonMasterMgr->GetSessionByPlayer(player->GetGUID());

        if (!session || !session->IsActive())
            return;

        if (creature->GetMapId() != session->MapId)
            return;

        sDungeonMasterMgr->HandleCreatureDeath(creature, session);
    }

private:
    void ScaleEnvDamage(Unit* target, Unit* attacker, uint32& damage)
    {
        if (!sDMConfig->IsEnabled() || damage == 0)
            return;

        Player* player = target ? target->ToPlayer() : nullptr;
        if (!player)
            return;

        // Skip if attacker is another player (PvP)
        if (attacker && attacker->ToPlayer())
            return;


        ObjectGuid playerGuid = player->GetGUID();

        // Our spawned creatures are already scaled — skip

        if (attacker)
        {
            ObjectGuid attackerGuid = attacker->GetGUID();
            if (sDungeonMasterMgr->IsSessionCreature(playerGuid, attackerGuid))
                return;
        }


        if (!sDungeonMasterMgr->GetSessionByPlayer(playerGuid))
            return;  // not in a session — don't modify

        // Level scaling

        float scale = sDungeonMasterMgr->GetEnvironmentalDamageScale(playerGuid);
        if (scale < 1.0f)
            damage = static_cast<uint32>(damage * scale);

        // Hard cap at 3% max HP
        uint32 maxHp = player->GetMaxHealth();
        uint32 cap   = static_cast<uint32>(maxHp * ENV_DAMAGE_MAX_PCT);
        if (cap < 1) cap = 1;

        if (damage > cap)
            damage = cap;

        if (damage == 0)
            damage = 1;   // Minimum 1 damage — never fully negate
    }
};

void AddSC_dm_unit_script()
{
    new dm_unit_script();
}
