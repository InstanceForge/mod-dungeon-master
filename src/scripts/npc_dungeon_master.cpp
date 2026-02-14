/*
 * mod-dungeon-master — npc_dungeon_master.cpp
 * Gossip NPC: menu flow for difficulty/theme/dungeon selection.
 */

#include "ScriptMgr.h"
#include "ScriptedCreature.h"
#include "ScriptedGossip.h"
#include "GossipDef.h"
#include "Player.h"
#include "Creature.h"
#include "Group.h"
#include "Log.h"
#include "Chat.h"
#include "ObjectAccessor.h"
#include "DungeonMasterMgr.h"
#include "RoguelikeMgr.h"
#include "RoguelikeTypes.h"
#include "DMConfig.h"
#include <cstdio>
#include <mutex>
#include <random>

using namespace DungeonMaster;

// Gossip action IDs (encoded so ranges never overlap)
enum DMGossipActions
{
    GOSSIP_ACTION_MAIN_START    = 1,
    GOSSIP_ACTION_MAIN_INFO     = 2,
    GOSSIP_ACTION_MAIN_STATS    = 3,   // unused legacy, kept for safety

    GOSSIP_ACTION_DIFF_BASE     = 100,   // +diffId
    GOSSIP_ACTION_THEME_BASE    = 200,   // +themeId
    GOSSIP_ACTION_DUNGEON_BASE  = 300,   // +mapId (maps go up to ~700)
    GOSSIP_ACTION_DUNGEON_RANDOM = 10000,

    GOSSIP_ACTION_CONFIRM       = 10001,
    GOSSIP_ACTION_CANCEL        = 10002,
    GOSSIP_ACTION_SCALE_PARTY   = 10003,
    GOSSIP_ACTION_SCALE_TIER    = 10004,
    GOSSIP_ACTION_LEADERBOARD   = 10005, // legacy — redirects to board menu

    // Roguelike Mode
    GOSSIP_ACTION_ROGUELIKE_START       = 10010,
    GOSSIP_ACTION_ROGUELIKE_SCALE_PARTY = 10011,
    GOSSIP_ACTION_ROGUELIKE_SCALE_TIER  = 10012,
    GOSSIP_ACTION_ROGUELIKE_THEME       = 10100, // +themeId
    GOSSIP_ACTION_ROGUELIKE_QUIT        = 10200,
    GOSSIP_ACTION_ROGUELIKE_BOARD       = 10201,

    // Statistics & Leaderboards sub-menus
    GOSSIP_ACTION_STATS_MENU          = 10300,  // Stats & Leaderboards hub
    GOSSIP_ACTION_STATS_NORMAL        = 10301,  // My Normal Run Stats
    GOSSIP_ACTION_STATS_ROGUELIKE     = 10302,  // My Roguelike Stats
    GOSSIP_ACTION_BOARD_MENU          = 10310,  // Leaderboards hub
    GOSSIP_ACTION_BOARD_NORMAL        = 10311,  // Normal — Fastest Clears
    GOSSIP_ACTION_BOARD_RL_TIER       = 10312,  // Roguelike — Highest Tier
    GOSSIP_ACTION_BOARD_RL_FLOORS     = 10313,  // Roguelike — Most Floors
};

struct PlayerDMSelection
{
    uint32 DifficultyId  = 0;
    uint32 ThemeId       = 0;
    uint32 MapId         = 0;
    bool   ScaleToParty  = true;
    bool   IsRoguelike   = false;
};

static std::unordered_map<ObjectGuid, PlayerDMSelection> sSelections;
static std::mutex sSelMutex;

class npc_dungeon_master : public CreatureScript
{
public:
    npc_dungeon_master() : CreatureScript("npc_dungeon_master") {}

    bool OnGossipHello(Player* player, Creature* creature) override
    {
        if (!sDMConfig->IsEnabled())
        {
            ChatHandler(player->GetSession()).SendSysMessage(
                "|cFFFF0000[Dungeon Master]|r The Dungeon Master is currently unavailable.");
            player->PlayerTalkClass->SendCloseGossip();
            return true;
        }
        if (sDungeonMasterMgr->GetSessionByPlayer(player->GetGUID()))
        {
            LOG_INFO("module", "DungeonMaster: NPC blocked {} — still in active session",
                player->GetName());
            ChatHandler(player->GetSession()).SendSysMessage(
                "|cFFFF0000[Dungeon Master]|r You are already in an active challenge!");
            player->PlayerTalkClass->SendCloseGossip();
            return true;
        }
        if (sRoguelikeMgr->IsPlayerInRun(player->GetGUID()))
        {
            player->PlayerTalkClass->ClearMenus();

            // Player is in an active roguelike run (auto-transitions between dungeons)
            RoguelikeRun* run = sRoguelikeMgr->GetRunByPlayer(player->GetGUID());
            if (run)
            {
                char tierBuf[256];
                snprintf(tierBuf, sizeof(tierBuf),
                    "|cFF00FFFF[Roguelike]|r Active run — |cFFFFD700Tier %u|r, "
                    "|cFFFFFFFF%u|r floor%s cleared.",
                    run->CurrentTier, run->DungeonsCleared,
                    run->DungeonsCleared != 1 ? "s" : "");
                ChatHandler(player->GetSession()).SendSysMessage(tierBuf);
            }
            else
            {
                ChatHandler(player->GetSession()).SendSysMessage(
                    "|cFF00FFFF[Roguelike]|r You are in an active roguelike run!");
            }

            AddGossipItemFor(player, GOSSIP_ICON_BATTLE,
                "|cFFFF0000Quit Roguelike Run|r",
                GOSSIP_SENDER_MAIN, GOSSIP_ACTION_ROGUELIKE_QUIT);
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Never mind",
                GOSSIP_SENDER_MAIN, GOSSIP_ACTION_CANCEL);

            SendGossipMenuFor(player, DEFAULT_GOSSIP_MESSAGE, creature->GetGUID());
            return true;
        }
        if (sDungeonMasterMgr->IsOnCooldown(player->GetGUID()))
        {
            uint32 rem  = sDungeonMasterMgr->GetRemainingCooldown(player->GetGUID());
            LOG_INFO("module", "DungeonMaster: NPC blocked {} — cooldown {}s remaining",
                player->GetName(), rem);
            char buf[256];
            snprintf(buf, sizeof(buf),
                "|cFFFFFF00[Dungeon Master]|r Wait |cFFFFFFFF%u|r min |cFFFFFFFF%u|r sec before your next challenge.",
                rem / 60, rem % 60);
            ChatHandler(player->GetSession()).SendSysMessage(buf);
            player->PlayerTalkClass->SendCloseGossip();
            return true;
        }
        ShowMainMenu(player, creature);
        return true;
    }

    bool OnGossipSelect(Player* player, Creature* creature, uint32 /*sender*/, uint32 action) override
    {
        player->PlayerTalkClass->ClearMenus();

        if (action == GOSSIP_ACTION_MAIN_START)
        {
            if (!sDungeonMasterMgr->CanCreateNewSession())
            {
                ChatHandler(player->GetSession()).SendSysMessage(
                    "|cFFFF0000[Dungeon Master]|r Too many challenges running. Try again later.");
                player->PlayerTalkClass->SendCloseGossip();
                return true;
            }
            { std::lock_guard<std::mutex> lk(sSelMutex); sSelections[player->GetGUID()] = {}; }
            ShowDifficultyMenu(player, creature);
        }
        else if (action == GOSSIP_ACTION_MAIN_INFO)
            ShowInfoMenu(player, creature);
        else if (action == GOSSIP_ACTION_MAIN_STATS)
            ShowStatsAndBoardsMenu(player, creature);
        else if (action == GOSSIP_ACTION_LEADERBOARD)
            ShowBoardMenu(player, creature);
        else if (action >= GOSSIP_ACTION_DIFF_BASE && action < GOSSIP_ACTION_THEME_BASE)
        {
            uint32 diffId = action - GOSSIP_ACTION_DIFF_BASE;
            bool isRoguelike = false;
            { std::lock_guard<std::mutex> lk(sSelMutex);
              sSelections[player->GetGUID()].DifficultyId = diffId;
              isRoguelike = sSelections[player->GetGUID()].IsRoguelike; }
            if (isRoguelike)
                ShowRoguelikeScalingMenu(player, creature);
            else
                ShowScalingMenu(player, creature);
        }
        else if (action == GOSSIP_ACTION_SCALE_PARTY)
        {
            { std::lock_guard<std::mutex> lk(sSelMutex); sSelections[player->GetGUID()].ScaleToParty = true; }
            ShowThemeMenu(player, creature);
        }
        else if (action == GOSSIP_ACTION_SCALE_TIER)
        {
            { std::lock_guard<std::mutex> lk(sSelMutex); sSelections[player->GetGUID()].ScaleToParty = false; }
            ShowThemeMenu(player, creature);
        }
        else if (action >= GOSSIP_ACTION_THEME_BASE && action < GOSSIP_ACTION_DUNGEON_BASE)
        {
            { std::lock_guard<std::mutex> lk(sSelMutex); sSelections[player->GetGUID()].ThemeId = action - GOSSIP_ACTION_THEME_BASE; }
            ShowDungeonMenu(player, creature);
        }
        else if (action == GOSSIP_ACTION_DUNGEON_RANDOM)
        {
            { std::lock_guard<std::mutex> lk(sSelMutex); sSelections[player->GetGUID()].MapId = 0; }
            ShowConfirmMenu(player, creature);
        }
        else if (action >= GOSSIP_ACTION_DUNGEON_BASE && action < GOSSIP_ACTION_CONFIRM)
        {
            { std::lock_guard<std::mutex> lk(sSelMutex); sSelections[player->GetGUID()].MapId = action - GOSSIP_ACTION_DUNGEON_BASE; }
            ShowConfirmMenu(player, creature);
        }
        else if (action == GOSSIP_ACTION_CONFIRM)
            StartChallenge(player, creature);
        else if (action == GOSSIP_ACTION_CANCEL)
        {
            { std::lock_guard<std::mutex> lk(sSelMutex); sSelections.erase(player->GetGUID()); }
            ShowMainMenu(player, creature);
        }
        // ---- Roguelike Actions ----
        else if (action == GOSSIP_ACTION_ROGUELIKE_START)
        {
            if (sRoguelikeMgr->IsPlayerInRun(player->GetGUID()))
            {
                ChatHandler(player->GetSession()).SendSysMessage(
                    "|cFFFF0000[Roguelike]|r You are already in a roguelike run!");
                player->PlayerTalkClass->SendCloseGossip();
                return true;
            }
            { std::lock_guard<std::mutex> lk(sSelMutex);
              sSelections[player->GetGUID()] = {};
              sSelections[player->GetGUID()].IsRoguelike = true; }
            ShowRoguelikeDifficultyMenu(player, creature);
        }
        else if (action == GOSSIP_ACTION_ROGUELIKE_SCALE_PARTY)
        {
            { std::lock_guard<std::mutex> lk(sSelMutex); sSelections[player->GetGUID()].ScaleToParty = true; }
            ShowRoguelikeThemeMenu(player, creature);
        }
        else if (action == GOSSIP_ACTION_ROGUELIKE_SCALE_TIER)
        {
            { std::lock_guard<std::mutex> lk(sSelMutex); sSelections[player->GetGUID()].ScaleToParty = false; }
            ShowRoguelikeThemeMenu(player, creature);
        }
        else if (action >= GOSSIP_ACTION_ROGUELIKE_THEME && action < GOSSIP_ACTION_ROGUELIKE_QUIT)
        {
            uint32 themeId = action - GOSSIP_ACTION_ROGUELIKE_THEME;
            { std::lock_guard<std::mutex> lk(sSelMutex); sSelections[player->GetGUID()].ThemeId = themeId; }
            StartRoguelike(player, creature);
        }
        else if (action == GOSSIP_ACTION_ROGUELIKE_QUIT)
        {
            if (sRoguelikeMgr->IsPlayerInRun(player->GetGUID()))
            {
                sRoguelikeMgr->QuitRun(player->GetGUID());
                ChatHandler(player->GetSession()).SendSysMessage(
                    "|cFF00FFFF[Roguelike]|r Run abandoned.");
            }
            player->PlayerTalkClass->SendCloseGossip();
        }
        else if (action == GOSSIP_ACTION_ROGUELIKE_BOARD)
        {
            ShowRoguelikeLeaderboard(player, creature, false);
        }
        // ---- Statistics & Leaderboards ----
        else if (action == GOSSIP_ACTION_STATS_MENU)
            ShowStatsAndBoardsMenu(player, creature);
        else if (action == GOSSIP_ACTION_STATS_NORMAL)
            ShowNormalStats(player, creature);
        else if (action == GOSSIP_ACTION_STATS_ROGUELIKE)
            ShowRoguelikeStats(player, creature);
        else if (action == GOSSIP_ACTION_BOARD_MENU)
            ShowBoardMenu(player, creature);
        else if (action == GOSSIP_ACTION_BOARD_NORMAL)
            ShowNormalLeaderboard(player, creature);
        else if (action == GOSSIP_ACTION_BOARD_RL_TIER)
            ShowRoguelikeLeaderboard(player, creature, false);
        else if (action == GOSSIP_ACTION_BOARD_RL_FLOORS)
            ShowRoguelikeLeaderboard(player, creature, true);
        return true;
    }

private:
    // ---- Menu builders ----

    void ShowMainMenu(Player* player, Creature* creature)
    {
        player->PlayerTalkClass->ClearMenus();
        AddGossipItemFor(player, GOSSIP_ICON_BATTLE, "Begin Challenge",
            GOSSIP_SENDER_MAIN, GOSSIP_ACTION_MAIN_START);
        if (sDMConfig->IsRoguelikeEnabled())
            AddGossipItemFor(player, GOSSIP_ICON_BATTLE, "|cFF00FFFFRoguelike Mode|r",
                GOSSIP_SENDER_MAIN, GOSSIP_ACTION_ROGUELIKE_START);
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "How does this work?",
            GOSSIP_SENDER_MAIN, GOSSIP_ACTION_MAIN_INFO);
        AddGossipItemFor(player, GOSSIP_ICON_TABARD, "|cFFFFD700Statistics & Leaderboards|r",
            GOSSIP_SENDER_MAIN, GOSSIP_ACTION_STATS_MENU);
        SendGossipMenuFor(player, DEFAULT_GOSSIP_MESSAGE, creature->GetGUID());
    }

    void ShowDifficultyMenu(Player* player, Creature* creature)
    {
        player->PlayerTalkClass->ClearMenus();
        uint8 lvl = player->GetLevel();

        for (const auto& d : sDMConfig->GetDifficulties())
        {
            char buf[256];
            if (!d.IsValidForLevel(lvl))
                snprintf(buf, sizeof(buf), "|cFF808080%s|r (Lv %u-%u) - |cFFFF0000Requires %u+|r",
                    d.Name.c_str(), d.MinLevel, d.MaxLevel, d.MinLevel);
            else if (!d.IsOnLevelFor(lvl))
                snprintf(buf, sizeof(buf), "%s |cFF808080(Lv %u-%u — Easy)|r",
                    d.Name.c_str(), d.MinLevel, d.MaxLevel);
            else
                snprintf(buf, sizeof(buf), "|cFF00FF00%s|r (Lv %u-%u)",
                    d.Name.c_str(), d.MinLevel, d.MaxLevel);

            AddGossipItemFor(player,
                d.IsValidForLevel(lvl) ? GOSSIP_ICON_BATTLE : GOSSIP_ICON_CHAT,
                buf, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_DIFF_BASE + d.Id);
        }
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "|cFFFF0000<< Back|r", GOSSIP_SENDER_MAIN, GOSSIP_ACTION_CANCEL);
        SendGossipMenuFor(player, DEFAULT_GOSSIP_MESSAGE, creature->GetGUID());
    }

    void ShowScalingMenu(Player* player, Creature* creature)
    {
        player->PlayerTalkClass->ClearMenus();

        PlayerDMSelection sel;
        { std::lock_guard<std::mutex> lk(sSelMutex);
          auto it = sSelections.find(player->GetGUID());
          if (it == sSelections.end()) { player->PlayerTalkClass->SendCloseGossip(); return; }
          sel = it->second; }

        const DifficultyTier* diff = sDMConfig->GetDifficulty(sel.DifficultyId);
        if (!diff) { player->PlayerTalkClass->SendCloseGossip(); return; }

        uint8 partyLevel = sDungeonMasterMgr->ComputeEffectiveLevel(player);

        char buf1[256], buf2[256];
        snprintf(buf1, sizeof(buf1),
            "|cFF00FF00Scale to Party Level|r (Lv %u) — Full challenge at your level",
            partyLevel);
        snprintf(buf2, sizeof(buf2),
            "|cFFFFD700Use Dungeon Difficulty|r (Lv %u-%u) — Original difficulty range",
            diff->MinLevel, diff->MaxLevel);

        AddGossipItemFor(player, GOSSIP_ICON_BATTLE, buf1,
            GOSSIP_SENDER_MAIN, GOSSIP_ACTION_SCALE_PARTY);
        AddGossipItemFor(player, GOSSIP_ICON_INTERACT_1, buf2,
            GOSSIP_SENDER_MAIN, GOSSIP_ACTION_SCALE_TIER);
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "|cFFFF0000<< Back|r",
            GOSSIP_SENDER_MAIN, GOSSIP_ACTION_CANCEL);
        SendGossipMenuFor(player, DEFAULT_GOSSIP_MESSAGE, creature->GetGUID());
    }

    void ShowThemeMenu(Player* player, Creature* creature)
    {
        player->PlayerTalkClass->ClearMenus();
        for (const auto& t : sDMConfig->GetThemes())
            AddGossipItemFor(player, GOSSIP_ICON_BATTLE, t.Name, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_THEME_BASE + t.Id);
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "|cFFFF0000<< Back|r", GOSSIP_SENDER_MAIN, GOSSIP_ACTION_CANCEL);
        SendGossipMenuFor(player, DEFAULT_GOSSIP_MESSAGE, creature->GetGUID());
    }

    void ShowDungeonMenu(Player* player, Creature* creature)
    {
        player->PlayerTalkClass->ClearMenus();

        uint32 diffId;
        { std::lock_guard<std::mutex> lk(sSelMutex);
          auto it = sSelections.find(player->GetGUID());
          if (it == sSelections.end()) { player->PlayerTalkClass->SendCloseGossip(); return; }
          diffId = it->second.DifficultyId; }

        const DifficultyTier* diff = sDMConfig->GetDifficulty(diffId);
        if (!diff) { player->PlayerTalkClass->SendCloseGossip(); return; }

        auto dungeons = sDMConfig->GetDungeonsForLevel(diff->MinLevel, diff->MaxLevel);

        AddGossipItemFor(player, GOSSIP_ICON_BATTLE, "|cFFFFD700Random Dungeon|r",
            GOSSIP_SENDER_MAIN, GOSSIP_ACTION_DUNGEON_RANDOM);

        for (const auto* dg : dungeons)
        {
            char buf[128];
            snprintf(buf, sizeof(buf), "%s (Lv %u-%u)", dg->Name.c_str(), dg->MinLevel, dg->MaxLevel);
            AddGossipItemFor(player, GOSSIP_ICON_INTERACT_1, buf,
                GOSSIP_SENDER_MAIN, GOSSIP_ACTION_DUNGEON_BASE + dg->MapId);
        }

        if (dungeons.empty())
            AddGossipItemFor(player, GOSSIP_ICON_CHAT,
                "|cFF808080No dungeons available|r", GOSSIP_SENDER_MAIN, GOSSIP_ACTION_CANCEL);

        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "|cFFFF0000<< Back|r", GOSSIP_SENDER_MAIN, GOSSIP_ACTION_CANCEL);
        SendGossipMenuFor(player, DEFAULT_GOSSIP_MESSAGE, creature->GetGUID());
    }

    void ShowConfirmMenu(Player* player, Creature* creature)
    {
        player->PlayerTalkClass->ClearMenus();

        PlayerDMSelection sel;
        { std::lock_guard<std::mutex> lk(sSelMutex);
          auto it = sSelections.find(player->GetGUID());
          if (it == sSelections.end()) { player->PlayerTalkClass->SendCloseGossip(); return; }
          sel = it->second; }

        const DifficultyTier* diff = sDMConfig->GetDifficulty(sel.DifficultyId);
        const Theme*          theme = sDMConfig->GetTheme(sel.ThemeId);

        std::string dgName = "Random Dungeon";
        if (sel.MapId > 0)
            if (const DungeonInfo* dg = sDMConfig->GetDungeon(sel.MapId))
                dgName = dg->Name;

        Group* g = player->GetGroup();
        uint32 ps = g ? g->GetMembersCount() : 1;

        char buf[256];
        ChatHandler(player->GetSession()).SendSysMessage("|cFFFFD700========== Challenge Summary ==========|r");
        snprintf(buf, sizeof(buf), "  Difficulty: |cFF00FF00%s|r", diff ? diff->Name.c_str() : "?");
        ChatHandler(player->GetSession()).SendSysMessage(buf);
        snprintf(buf, sizeof(buf), "  Scaling:    |cFF00FF00%s|r",
            sel.ScaleToParty ? "Party Level" : "Dungeon Difficulty");
        ChatHandler(player->GetSession()).SendSysMessage(buf);
        snprintf(buf, sizeof(buf), "  Theme:      |cFF00FF00%s|r", theme ? theme->Name.c_str() : "?");
        ChatHandler(player->GetSession()).SendSysMessage(buf);
        snprintf(buf, sizeof(buf), "  Dungeon:    |cFF00FF00%s|r", dgName.c_str());
        ChatHandler(player->GetSession()).SendSysMessage(buf);
        snprintf(buf, sizeof(buf), "  Party Size: |cFFFFFFFF%u|r player(s)", ps);
        ChatHandler(player->GetSession()).SendSysMessage(buf);
        if (ps > 1)
            ChatHandler(player->GetSession()).SendSysMessage("|cFFFFFF00  All party members will be teleported!|r");
        ChatHandler(player->GetSession()).SendSysMessage("|cFFFFD700========================================|r");

        AddGossipItemFor(player, GOSSIP_ICON_BATTLE, "|cFF00FF00>> START CHALLENGE <<|r",
            GOSSIP_SENDER_MAIN, GOSSIP_ACTION_CONFIRM);
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "|cFFFF0000<< Cancel|r",
            GOSSIP_SENDER_MAIN, GOSSIP_ACTION_CANCEL);
        SendGossipMenuFor(player, DEFAULT_GOSSIP_MESSAGE, creature->GetGUID());
    }

    void ShowInfoMenu(Player* player, Creature* creature)
    {
        player->PlayerTalkClass->ClearMenus();
        ChatHandler(player->GetSession()).SendSysMessage("|cFFFFD700========= Dungeon Master Challenge =========|r");
        ChatHandler(player->GetSession()).SendSysMessage("|cFFFFFFFF1.|r Choose a difficulty tier");
        ChatHandler(player->GetSession()).SendSysMessage("|cFFFFFFFF2.|r Pick scaling: party level or dungeon difficulty");
        ChatHandler(player->GetSession()).SendSysMessage("|cFFFFFFFF3.|r Pick a creature theme");
        ChatHandler(player->GetSession()).SendSysMessage("|cFFFFFFFF4.|r Select a dungeon or go random");
        ChatHandler(player->GetSession()).SendSysMessage("|cFFFFFFFF5.|r You'll be teleported to a cleared instance");
        ChatHandler(player->GetSession()).SendSysMessage("|cFFFFFFFF6.|r Defeat the boss to complete the challenge");
        ChatHandler(player->GetSession()).SendSysMessage("|cFFFFFFFF7.|r Collect gold and gear rewards!");
        ChatHandler(player->GetSession()).SendSysMessage("|cFFFFD700==========================================|r");
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "<< Back", GOSSIP_SENDER_MAIN, GOSSIP_ACTION_CANCEL);
        SendGossipMenuFor(player, DEFAULT_GOSSIP_MESSAGE, creature->GetGUID());
    }

    // ---- Statistics & Leaderboards Hub ----

    void ShowStatsAndBoardsMenu(Player* player, Creature* creature)
    {
        player->PlayerTalkClass->ClearMenus();
        AddGossipItemFor(player, GOSSIP_ICON_TABARD, "My Normal Run Stats",
            GOSSIP_SENDER_MAIN, GOSSIP_ACTION_STATS_NORMAL);
        if (sDMConfig->IsRoguelikeEnabled())
            AddGossipItemFor(player, GOSSIP_ICON_TABARD, "|cFF00FFFFMy Roguelike Stats|r",
                GOSSIP_SENDER_MAIN, GOSSIP_ACTION_STATS_ROGUELIKE);
        AddGossipItemFor(player, GOSSIP_ICON_TABARD, "|cFFFFD700Leaderboards|r",
            GOSSIP_SENDER_MAIN, GOSSIP_ACTION_BOARD_MENU);
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "|cFFFF0000<< Back|r",
            GOSSIP_SENDER_MAIN, GOSSIP_ACTION_CANCEL);
        SendGossipMenuFor(player, DEFAULT_GOSSIP_MESSAGE, creature->GetGUID());
    }

    static void FormatTime(uint32 seconds, char* buf, size_t len)
    {
        if (seconds >= 3600)
            snprintf(buf, len, "%uh %02um %02us", seconds / 3600, (seconds % 3600) / 60, seconds % 60);
        else
            snprintf(buf, len, "%um %02us", seconds / 60, seconds % 60);
    }

    void ShowNormalStats(Player* player, Creature* creature)
    {
        player->PlayerTalkClass->ClearMenus();
        PlayerStats st = sDungeonMasterMgr->GetPlayerStats(player->GetGUID());
        auto chat = ChatHandler(player->GetSession());
        char buf[256];

        chat.SendSysMessage("|cFFFFD700═══════════ Normal Run Stats ═══════════|r");

        snprintf(buf, sizeof(buf), "  Runs: |cFFFFFFFF%u|r  —  Completed: |cFF00FF00%u|r  —  Failed: |cFFFF0000%u|r",
            st.TotalRuns, st.CompletedRuns, st.FailedRuns);
        chat.SendSysMessage(buf);

        if (st.TotalRuns > 0)
        {
            float winRate = st.CompletedRuns * 100.0f / st.TotalRuns;
            snprintf(buf, sizeof(buf), "  Win Rate: |cFFFFD700%.1f%%|r", winRate);
            chat.SendSysMessage(buf);
        }

        chat.SendSysMessage(" ");

        snprintf(buf, sizeof(buf), "  Mobs Killed:  |cFFFFFFFF%u|r  —  Bosses Slain: |cFFFFFFFF%u|r",
            st.TotalMobsKilled, st.TotalBossesKilled);
        chat.SendSysMessage(buf);

        snprintf(buf, sizeof(buf), "  Deaths: |cFFFF0000%u|r", st.TotalDeaths);
        chat.SendSysMessage(buf);

        if (st.TotalDeaths > 0 && st.TotalMobsKilled > 0)
        {
            float kd = static_cast<float>(st.TotalMobsKilled + st.TotalBossesKilled) / st.TotalDeaths;
            snprintf(buf, sizeof(buf), "  Kill/Death Ratio: |cFFFFD700%.1f|r", kd);
            chat.SendSysMessage(buf);
        }

        if (st.FastestClear > 0)
        {
            char timeBuf[64];
            FormatTime(st.FastestClear, timeBuf, sizeof(timeBuf));
            snprintf(buf, sizeof(buf), "  Fastest Clear: |cFF00FFFF%s|r", timeBuf);
            chat.SendSysMessage(buf);
        }

        chat.SendSysMessage("|cFFFFD700══════════════════════════════════════════|r");

        AddGossipItemFor(player, GOSSIP_ICON_TABARD, "|cFFFFD700View Leaderboards|r",
            GOSSIP_SENDER_MAIN, GOSSIP_ACTION_BOARD_MENU);
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "|cFFFF0000<< Back|r",
            GOSSIP_SENDER_MAIN, GOSSIP_ACTION_STATS_MENU);
        SendGossipMenuFor(player, DEFAULT_GOSSIP_MESSAGE, creature->GetGUID());
    }

    void ShowRoguelikeStats(Player* player, Creature* creature)
    {
        player->PlayerTalkClass->ClearMenus();
        RoguelikePlayerStats st = sRoguelikeMgr->GetRoguelikePlayerStats(player->GetGUID());
        auto chat = ChatHandler(player->GetSession());
        char buf[256];

        chat.SendSysMessage("|cFF00FFFF═══════════ Roguelike Stats ═══════════|r");

        snprintf(buf, sizeof(buf), "  Total Runs: |cFFFFFFFF%u|r", st.TotalRuns);
        chat.SendSysMessage(buf);

        snprintf(buf, sizeof(buf), "  Highest Tier: |cFFFFD700%u|r  —  Most Floors: |cFFFFD700%u|r",
            st.HighestTier, st.MostFloorsCleared);
        chat.SendSysMessage(buf);

        snprintf(buf, sizeof(buf), "  Total Floors Cleared: |cFFFFFFFF%u|r", st.TotalFloorsCleared);
        chat.SendSysMessage(buf);

        chat.SendSysMessage(" ");

        snprintf(buf, sizeof(buf), "  Mobs Killed: |cFFFFFFFF%u|r  —  Bosses Slain: |cFFFFFFFF%u|r",
            st.TotalMobsKilled, st.TotalBossesKilled);
        chat.SendSysMessage(buf);

        snprintf(buf, sizeof(buf), "  Deaths: |cFFFF0000%u|r", st.TotalDeaths);
        chat.SendSysMessage(buf);

        if (st.TotalDeaths > 0 && (st.TotalMobsKilled + st.TotalBossesKilled) > 0)
        {
            float kd = static_cast<float>(st.TotalMobsKilled + st.TotalBossesKilled) / st.TotalDeaths;
            snprintf(buf, sizeof(buf), "  Kill/Death Ratio: |cFFFFD700%.1f|r", kd);
            chat.SendSysMessage(buf);
        }

        if (st.TotalRuns > 0)
        {
            float avgFloors = static_cast<float>(st.TotalFloorsCleared) / st.TotalRuns;
            snprintf(buf, sizeof(buf), "  Avg Floors/Run: |cFF00FFFF%.1f|r", avgFloors);
            chat.SendSysMessage(buf);
        }

        if (st.LongestRunTime > 0)
        {
            char timeBuf[64];
            FormatTime(st.LongestRunTime, timeBuf, sizeof(timeBuf));
            snprintf(buf, sizeof(buf), "  Longest Run: |cFF00FFFF%s|r", timeBuf);
            chat.SendSysMessage(buf);
        }

        chat.SendSysMessage("|cFF00FFFF══════════════════════════════════════════|r");

        AddGossipItemFor(player, GOSSIP_ICON_TABARD, "|cFFFFD700View Leaderboards|r",
            GOSSIP_SENDER_MAIN, GOSSIP_ACTION_BOARD_MENU);
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "|cFFFF0000<< Back|r",
            GOSSIP_SENDER_MAIN, GOSSIP_ACTION_STATS_MENU);
        SendGossipMenuFor(player, DEFAULT_GOSSIP_MESSAGE, creature->GetGUID());
    }

    // ---- Leaderboard Hub ----

    void ShowBoardMenu(Player* player, Creature* creature)
    {
        player->PlayerTalkClass->ClearMenus();
        AddGossipItemFor(player, GOSSIP_ICON_TABARD,
            "|cFFFFD700Normal Runs — Fastest Clears|r",
            GOSSIP_SENDER_MAIN, GOSSIP_ACTION_BOARD_NORMAL);
        if (sDMConfig->IsRoguelikeEnabled())
        {
            AddGossipItemFor(player, GOSSIP_ICON_TABARD,
                "|cFF00FFFFRoguelike — Highest Tier|r",
                GOSSIP_SENDER_MAIN, GOSSIP_ACTION_BOARD_RL_TIER);
            AddGossipItemFor(player, GOSSIP_ICON_TABARD,
                "|cFF00FFFFRoguelike — Most Floors|r",
                GOSSIP_SENDER_MAIN, GOSSIP_ACTION_BOARD_RL_FLOORS);
        }
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "|cFFFF0000<< Back|r",
            GOSSIP_SENDER_MAIN, GOSSIP_ACTION_STATS_MENU);
        SendGossipMenuFor(player, DEFAULT_GOSSIP_MESSAGE, creature->GetGUID());
    }

    void ShowNormalLeaderboard(Player* player, Creature* creature)
    {
        player->PlayerTalkClass->ClearMenus();

        auto entries = sDungeonMasterMgr->GetOverallLeaderboard(10);
        auto chat = ChatHandler(player->GetSession());

        chat.SendSysMessage("|cFFFFD700═══════ Normal Runs — Fastest Clears ═══════|r");

        if (entries.empty())
        {
            chat.SendSysMessage("  |cFF808080No runs recorded yet.|r");
        }
        else
        {
            uint32 rank = 0;
            uint32 myGuid = player->GetGUID().GetCounter();
            for (const auto& e : entries)
            {
                ++rank;
                char timeBuf[64];
                FormatTime(e.ClearTime, timeBuf, sizeof(timeBuf));

                const DifficultyTier* diff = sDMConfig->GetDifficulty(e.DifficultyId);
                const DungeonInfo* dg = sDMConfig->GetDungeon(e.MapId);

                bool isMe = (e.Guid == myGuid);
                char buf[384];
                snprintf(buf, sizeof(buf),
                    "  %s#%u%s |cFFFFFFFF%s|r — |cFF00FFFF%s|r — %s (%s) %uP%s%s",
                    isMe ? "|cFF00FF00" : "|cFFFFD700",
                    rank,
                    isMe ? "|r" : "|r",
                    e.CharName.c_str(),
                    timeBuf,
                    dg ? dg->Name.c_str() : "?",
                    diff ? diff->Name.c_str() : "?",
                    e.PartySize,
                    e.Scaled ? " |cFF00FF00[Scaled]|r" : "",
                    isMe ? " |cFF00FF00<< YOU|r" : "");
                chat.SendSysMessage(buf);
            }
        }

        chat.SendSysMessage("|cFFFFD700════════════════════════════════════════════════|r");
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "|cFFFF0000<< Back|r",
            GOSSIP_SENDER_MAIN, GOSSIP_ACTION_BOARD_MENU);
        SendGossipMenuFor(player, DEFAULT_GOSSIP_MESSAGE, creature->GetGUID());
    }

    void ShowRoguelikeLeaderboard(Player* player, Creature* creature, bool sortByFloors)
    {
        player->PlayerTalkClass->ClearMenus();

        auto entries = sRoguelikeMgr->GetRoguelikeLeaderboard(10, sortByFloors);
        auto chat = ChatHandler(player->GetSession());

        if (sortByFloors)
            chat.SendSysMessage("|cFF00FFFF═══════ Roguelike — Most Floors ═══════|r");
        else
            chat.SendSysMessage("|cFF00FFFF═══════ Roguelike — Highest Tier ═══════|r");

        if (entries.empty())
            chat.SendSysMessage("  |cFF808080No roguelike runs recorded yet.|r");
        else
        {
            uint32 rank = 0;
            uint32 myGuid = player->GetGUID().GetCounter();
            for (const auto& e : entries)
            {
                ++rank;
                char timeBuf[64];
                FormatTime(e.RunDuration, timeBuf, sizeof(timeBuf));

                bool isMe = (e.Guid == myGuid);
                char buf[384];
                snprintf(buf, sizeof(buf),
                    "  %s#%u%s |cFFFFFFFF%s|r — Tier |cFFFFD700%u|r — |cFF00FF00%u|r floor%s — |cFF00FFFF%s|r — %u kills — %uP%s",
                    isMe ? "|cFF00FF00" : "|cFFFFD700",
                    rank,
                    isMe ? "|r" : "|r",
                    e.CharName.c_str(),
                    e.TierReached,
                    e.DungeonsCleared,
                    e.DungeonsCleared != 1 ? "s" : "",
                    timeBuf,
                    e.TotalKills,
                    e.PartySize,
                    isMe ? " |cFF00FF00<< YOU|r" : "");
                chat.SendSysMessage(buf);
            }
        }

        chat.SendSysMessage("|cFF00FFFF══════════════════════════════════════════════|r");
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "|cFFFF0000<< Back|r",
            GOSSIP_SENDER_MAIN, GOSSIP_ACTION_BOARD_MENU);
        SendGossipMenuFor(player, DEFAULT_GOSSIP_MESSAGE, creature->GetGUID());
    }

    // ---- Roguelike Menus ----

    void ShowRoguelikeDifficultyMenu(Player* player, Creature* creature)
    {
        player->PlayerTalkClass->ClearMenus();
        uint8 lvl = player->GetLevel();

        ChatHandler(player->GetSession()).SendSysMessage(
            "|cFF00FFFF========== Roguelike Mode ==========|r");
        ChatHandler(player->GetSession()).SendSysMessage(
            "|cFFFFFFFFClear dungeons back-to-back. Each clear increases the tier.|r");
        ChatHandler(player->GetSession()).SendSysMessage(
            "|cFFFFFFFFEnemies get harder, but you gain powerful buffs.|r");
        ChatHandler(player->GetSession()).SendSysMessage(
            "|cFFFF0000One wipe ends the run!|r");
        ChatHandler(player->GetSession()).SendSysMessage(
            "|cFF00FFFF========================================|r");

        for (const auto& d : sDMConfig->GetDifficulties())
        {
            char buf[256];
            if (!d.IsValidForLevel(lvl))
                snprintf(buf, sizeof(buf), "|cFF808080%s|r (Lv %u-%u) - |cFFFF0000Requires %u+|r",
                    d.Name.c_str(), d.MinLevel, d.MaxLevel, d.MinLevel);
            else
                snprintf(buf, sizeof(buf), "|cFF00FFFF%s|r (Lv %u-%u)",
                    d.Name.c_str(), d.MinLevel, d.MaxLevel);

            AddGossipItemFor(player,
                d.IsValidForLevel(lvl) ? GOSSIP_ICON_BATTLE : GOSSIP_ICON_CHAT,
                buf, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_DIFF_BASE + d.Id);
        }
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "|cFFFF0000<< Back|r",
            GOSSIP_SENDER_MAIN, GOSSIP_ACTION_CANCEL);
        SendGossipMenuFor(player, DEFAULT_GOSSIP_MESSAGE, creature->GetGUID());
    }

    void ShowRoguelikeScalingMenu(Player* player, Creature* creature)
    {
        player->PlayerTalkClass->ClearMenus();

        uint8 partyLevel = sDungeonMasterMgr->ComputeEffectiveLevel(player);

        char buf1[256], buf2[256];
        snprintf(buf1, sizeof(buf1),
            "|cFF00FF00Scale to Party Level|r (Lv %u)", partyLevel);
        snprintf(buf2, sizeof(buf2),
            "|cFFFFD700Use Dungeon Difficulty|r — Original level ranges");

        AddGossipItemFor(player, GOSSIP_ICON_BATTLE, buf1,
            GOSSIP_SENDER_MAIN, GOSSIP_ACTION_ROGUELIKE_SCALE_PARTY);
        AddGossipItemFor(player, GOSSIP_ICON_INTERACT_1, buf2,
            GOSSIP_SENDER_MAIN, GOSSIP_ACTION_ROGUELIKE_SCALE_TIER);
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "|cFFFF0000<< Back|r",
            GOSSIP_SENDER_MAIN, GOSSIP_ACTION_CANCEL);
        SendGossipMenuFor(player, DEFAULT_GOSSIP_MESSAGE, creature->GetGUID());
    }

    void ShowRoguelikeThemeMenu(Player* player, Creature* creature)
    {
        player->PlayerTalkClass->ClearMenus();
        for (const auto& t : sDMConfig->GetThemes())
            AddGossipItemFor(player, GOSSIP_ICON_BATTLE, t.Name,
                GOSSIP_SENDER_MAIN, GOSSIP_ACTION_ROGUELIKE_THEME + t.Id);
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "|cFFFF0000<< Back|r",
            GOSSIP_SENDER_MAIN, GOSSIP_ACTION_CANCEL);
        SendGossipMenuFor(player, DEFAULT_GOSSIP_MESSAGE, creature->GetGUID());
    }

    void StartRoguelike(Player* player, Creature* /*creature*/)
    {
        player->PlayerTalkClass->SendCloseGossip();

        PlayerDMSelection sel;
        { std::lock_guard<std::mutex> lk(sSelMutex);
          auto it = sSelections.find(player->GetGUID());
          if (it == sSelections.end()) {
              ChatHandler(player->GetSession()).SendSysMessage(
                  "|cFFFF0000[Roguelike]|r Selection expired. Try again.");
              return; }
          sel = it->second;
          sSelections.erase(it); }

        const DifficultyTier* diff = sDMConfig->GetDifficulty(sel.DifficultyId);
        if (!diff || !diff->IsValidForLevel(player->GetLevel()))
        {
            ChatHandler(player->GetSession()).SendSysMessage(
                "|cFFFF0000[Roguelike]|r Level requirement not met!");
            return;
        }

        uint32 runId = 0; // unused, StartRun returns bool
        if (!sRoguelikeMgr->StartRun(player, sel.DifficultyId,
            sel.ThemeId, sel.ScaleToParty))
        {
            ChatHandler(player->GetSession()).SendSysMessage(
                "|cFFFF0000[Roguelike]|r Failed to start roguelike run!");
            return;
        }

        ChatHandler(player->GetSession()).SendSysMessage(
            "|cFF00FFFF[Roguelike]|r Run started! Clear dungeons to progress. Good luck!");
    }

    // ---- Launch ----
    void StartChallenge(Player* player, Creature* /*creature*/)
    {
        player->PlayerTalkClass->SendCloseGossip();

        PlayerDMSelection sel;
        { std::lock_guard<std::mutex> lk(sSelMutex);
          auto it = sSelections.find(player->GetGUID());
          if (it == sSelections.end()) {
              ChatHandler(player->GetSession()).SendSysMessage("|cFFFF0000[Dungeon Master]|r Selection expired. Try again.");
              return; }
          sel = it->second;
          sSelections.erase(it); }

        const DifficultyTier* diff = sDMConfig->GetDifficulty(sel.DifficultyId);
        if (!diff || !diff->IsValidForLevel(player->GetLevel()))
        {
            ChatHandler(player->GetSession()).SendSysMessage("|cFFFF0000[Dungeon Master]|r Level requirement not met!");
            return;
        }

        // Resolve random dungeon
        uint32 mapId = sel.MapId;
        if (mapId == 0)
        {
            auto dgs = sDMConfig->GetDungeonsForLevel(diff->MinLevel, diff->MaxLevel);
            if (dgs.empty()) {
                ChatHandler(player->GetSession()).SendSysMessage("|cFFFF0000[Dungeon Master]|r No dungeons available!");
                return; }
            static thread_local std::mt19937 rng{ std::random_device{}() };
            mapId = dgs[std::uniform_int_distribution<size_t>(0, dgs.size()-1)(rng)]->MapId;
        }

        Session* s = sDungeonMasterMgr->CreateSession(player, sel.DifficultyId, sel.ThemeId, mapId, sel.ScaleToParty);
        if (!s) {
            ChatHandler(player->GetSession()).SendSysMessage("|cFFFF0000[Dungeon Master]|r Failed to create session!");
            return; }

        if (!sDungeonMasterMgr->StartDungeon(s)) {
            ChatHandler(player->GetSession()).SendSysMessage("|cFFFF0000[Dungeon Master]|r Failed to initialize dungeon!");
            sDungeonMasterMgr->AbandonSession(s->SessionId); return; }

        if (!sDungeonMasterMgr->TeleportPartyIn(s)) {
            ChatHandler(player->GetSession()).SendSysMessage("|cFFFF0000[Dungeon Master]|r Teleport failed!");
            sDungeonMasterMgr->AbandonSession(s->SessionId); return; }

        if (sDMConfig->ShouldAnnounceCompletion())
        {
            const Theme* theme = sDMConfig->GetTheme(sel.ThemeId);
            const DungeonInfo* dg = sDMConfig->GetDungeon(mapId);
            char buf[256];
            snprintf(buf, sizeof(buf),
                "|cFF00FF00[Dungeon Master]|r |cFFFFFFFF%s|r started a |cFFFFD700%s|r |cFF00FFFF%s|r challenge!",
                player->GetName().c_str(), diff->Name.c_str(),
                theme ? theme->Name.c_str() : "Random");

            char detail[256];
            snprintf(detail, sizeof(detail),
                "|cFFFFD700[Dungeon Master]|r Difficulty: |cFF00FF00%s|r  Theme: |cFF00FF00%s|r  Dungeon: |cFF00FF00%s|r  Scaling: |cFF00FF00%s|r",
                diff->Name.c_str(),
                theme ? theme->Name.c_str() : "Random",
                dg ? dg->Name.c_str() : "Random",
                sel.ScaleToParty ? "Party Level" : "Dungeon Difficulty");

            // Broadcast to ALL party members
            for (const auto& pd : s->Players)
                if (Player* p = ObjectAccessor::FindPlayer(pd.PlayerGuid))
                {
                    ChatHandler(p->GetSession()).SendSysMessage(buf);
                    ChatHandler(p->GetSession()).SendSysMessage(detail);
                }
        }
    }
};

void AddSC_npc_dungeon_master()
{
    new npc_dungeon_master();
}
