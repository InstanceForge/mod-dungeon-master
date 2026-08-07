/*
 * mod-dungeon-master — Boss spawn-point SQL helpers.
 */

#ifndef DM_BOSS_SPAWN_QUERY_H
#define DM_BOSS_SPAWN_QUERY_H

#include "Define.h"

#include <string>

namespace DungeonMaster
{

inline std::string BuildBossSpawnPointQuery(uint32 mapId)
{
    return
        "SELECT c.position_x, c.position_y, c.position_z, c.orientation, "
        "COALESCE(ci.MechanicsMask, 0) AS MechanicsMask, ct.`rank`, ct.entry "
        "FROM creature c "
        "JOIN creature_template ct ON c.id = ct.entry "
        "LEFT JOIN creature_immunities ci ON ci.ID = ct.CreatureImmunitiesId "
        "WHERE c.map = " + std::to_string(mapId) + " "
        "AND COALESCE(ci.MechanicsMask, 0) > 0 "
        "AND ct.`rank` >= 1 "
        "ORDER BY COALESCE(ci.MechanicsMask, 0) DESC";
}

} // namespace DungeonMaster

#endif // DM_BOSS_SPAWN_QUERY_H
