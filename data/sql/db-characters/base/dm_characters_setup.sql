-- =============================================
-- Dungeon Master Module - Characters DB Setup
-- =============================================
-- Run this on your CHARACTERS database (not world).
-- Creates tables for persistent player statistics
-- and dungeon clear-time leaderboards.
-- =============================================

-- ---------------------------------------------------------------------------
-- Normal run player stats (standalone challenges only)
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS `dm_player_stats` (
    `guid`               INT UNSIGNED NOT NULL,
    `total_runs`         INT UNSIGNED NOT NULL DEFAULT 0,
    `completed_runs`     INT UNSIGNED NOT NULL DEFAULT 0,
    `failed_runs`        INT UNSIGNED NOT NULL DEFAULT 0,
    `total_mobs_killed`  INT UNSIGNED NOT NULL DEFAULT 0,
    `total_bosses_killed` INT UNSIGNED NOT NULL DEFAULT 0,
    `total_deaths`       INT UNSIGNED NOT NULL DEFAULT 0,
    `fastest_clear`      INT UNSIGNED NOT NULL DEFAULT 0,  -- seconds (best across all dungeons)
    PRIMARY KEY (`guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- ---------------------------------------------------------------------------
-- Normal run leaderboard (fastest clears per dungeon/difficulty)
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS `dm_leaderboard` (
    `id`              INT UNSIGNED NOT NULL AUTO_INCREMENT,
    `guid`            INT UNSIGNED NOT NULL,
    `char_name`       VARCHAR(48)  NOT NULL DEFAULT '',
    `map_id`          INT UNSIGNED NOT NULL,
    `difficulty_id`   INT UNSIGNED NOT NULL,
    `clear_time`      INT UNSIGNED NOT NULL,  -- seconds
    `party_size`      TINYINT UNSIGNED NOT NULL DEFAULT 1,
    `scaled`          TINYINT UNSIGNED NOT NULL DEFAULT 0,
    `effective_level` TINYINT UNSIGNED NOT NULL DEFAULT 0,
    `mobs_killed`     INT UNSIGNED NOT NULL DEFAULT 0,
    `bosses_killed`   INT UNSIGNED NOT NULL DEFAULT 0,
    `deaths`          INT UNSIGNED NOT NULL DEFAULT 0,
    `completed_at`    DATETIME     NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (`id`),
    INDEX `idx_map_diff` (`map_id`, `difficulty_id`, `clear_time`),
    INDEX `idx_overall`  (`clear_time`),
    INDEX `idx_guid`     (`guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- ---------------------------------------------------------------------------
-- Roguelike player stats (roguelike mode only — separate from normal)
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS `dm_roguelike_player_stats` (
    `guid`                 INT UNSIGNED NOT NULL,
    `total_runs`           INT UNSIGNED NOT NULL DEFAULT 0,
    `highest_tier`         INT UNSIGNED NOT NULL DEFAULT 0,
    `most_floors_cleared`  INT UNSIGNED NOT NULL DEFAULT 0,
    `total_floors_cleared` INT UNSIGNED NOT NULL DEFAULT 0,
    `total_mobs_killed`    INT UNSIGNED NOT NULL DEFAULT 0,
    `total_bosses_killed`  INT UNSIGNED NOT NULL DEFAULT 0,
    `total_deaths`         INT UNSIGNED NOT NULL DEFAULT 0,
    `longest_run_time`     INT UNSIGNED NOT NULL DEFAULT 0,  -- seconds
    PRIMARY KEY (`guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- ---------------------------------------------------------------------------
-- Roguelike leaderboard (sorted by tier reached, then floors, then speed)
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS `dm_roguelike_leaderboard` (
    `id`               INT UNSIGNED NOT NULL AUTO_INCREMENT,
    `guid`             INT UNSIGNED NOT NULL,
    `char_name`        VARCHAR(64)  NOT NULL DEFAULT '',
    `tier_reached`     INT UNSIGNED NOT NULL DEFAULT 0,
    `dungeons_cleared` INT UNSIGNED NOT NULL DEFAULT 0,
    `total_kills`      INT UNSIGNED NOT NULL DEFAULT 0,
    `total_bosses`     INT UNSIGNED NOT NULL DEFAULT 0,
    `total_deaths`     INT UNSIGNED NOT NULL DEFAULT 0,
    `run_duration`     INT UNSIGNED NOT NULL DEFAULT 0,
    `party_size`       TINYINT UNSIGNED NOT NULL DEFAULT 1,
    `completed_at`     DATETIME     NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (`id`),
    INDEX `idx_tier`   (`tier_reached` DESC, `dungeons_cleared` DESC, `run_duration` ASC),
    INDEX `idx_floors` (`dungeons_cleared` DESC, `tier_reached` DESC),
    INDEX `idx_guid`   (`guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- ---------------------------------------------------------------------------
-- MIGRATION: If upgrading from a previous version, this adds new columns
-- to existing tables.  Safe to run on fresh installs (columns already exist).
-- ---------------------------------------------------------------------------
DROP PROCEDURE IF EXISTS `dm_migrate`;
DELIMITER //
CREATE PROCEDURE `dm_migrate`()
BEGIN
    IF NOT EXISTS (SELECT 1 FROM INFORMATION_SCHEMA.COLUMNS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'dm_leaderboard' AND COLUMN_NAME = 'effective_level') THEN
        ALTER TABLE `dm_leaderboard` ADD COLUMN `effective_level` TINYINT UNSIGNED NOT NULL DEFAULT 0 AFTER `scaled`;
    END IF;
    IF NOT EXISTS (SELECT 1 FROM INFORMATION_SCHEMA.COLUMNS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'dm_leaderboard' AND COLUMN_NAME = 'mobs_killed') THEN
        ALTER TABLE `dm_leaderboard` ADD COLUMN `mobs_killed` INT UNSIGNED NOT NULL DEFAULT 0 AFTER `effective_level`;
    END IF;
    IF NOT EXISTS (SELECT 1 FROM INFORMATION_SCHEMA.COLUMNS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'dm_leaderboard' AND COLUMN_NAME = 'bosses_killed') THEN
        ALTER TABLE `dm_leaderboard` ADD COLUMN `bosses_killed` INT UNSIGNED NOT NULL DEFAULT 0 AFTER `mobs_killed`;
    END IF;
    IF NOT EXISTS (SELECT 1 FROM INFORMATION_SCHEMA.COLUMNS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'dm_leaderboard' AND COLUMN_NAME = 'deaths') THEN
        ALTER TABLE `dm_leaderboard` ADD COLUMN `deaths` INT UNSIGNED NOT NULL DEFAULT 0 AFTER `bosses_killed`;
    END IF;
    IF NOT EXISTS (SELECT 1 FROM INFORMATION_SCHEMA.COLUMNS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'dm_roguelike_leaderboard' AND COLUMN_NAME = 'total_bosses') THEN
        ALTER TABLE `dm_roguelike_leaderboard` ADD COLUMN `total_bosses` INT UNSIGNED NOT NULL DEFAULT 0 AFTER `total_kills`;
    END IF;
    IF NOT EXISTS (SELECT 1 FROM INFORMATION_SCHEMA.COLUMNS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'dm_roguelike_leaderboard' AND COLUMN_NAME = 'total_deaths') THEN
        ALTER TABLE `dm_roguelike_leaderboard` ADD COLUMN `total_deaths` INT UNSIGNED NOT NULL DEFAULT 0 AFTER `total_bosses`;
    END IF;
END //
DELIMITER ;
CALL `dm_migrate`();
DROP PROCEDURE IF EXISTS `dm_migrate`;
