// mod-dungeon-master — module entry point
// Function name must match AC_ADD_SCRIPT_LOADER convention

#include "ScriptMgr.h"
#include "Log.h"

void AddSC_npc_dungeon_master();
void AddSC_dm_player_script();
void AddSC_dm_world_script();
void AddSC_dm_allmap_script();
void AddSC_dm_command_script();
void AddSC_dm_unit_script();

void Addmod_dungeon_masterScripts()
{
    LOG_INFO("module", "DungeonMaster: Registering scripts...");

    AddSC_npc_dungeon_master();
    AddSC_dm_player_script();
    AddSC_dm_world_script();
    AddSC_dm_allmap_script();
    AddSC_dm_command_script();
    AddSC_dm_unit_script();
}
