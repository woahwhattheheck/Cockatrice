-- Servatrice db migration from version 41 to version 42.
--
-- Version 41 was independently assigned to deck sync (#7490-#7497) and
-- registered-account bans. Accept either complete layout, or a recognized
-- journaled continuation of this migration. Stop all Servatrice writers first.
-- Run with the mysql/mariadb client (DELIMITER support), without --force.
-- Replace the default cockatrice prefix consistently for a custom deployment.
--
-- DDL commits implicitly. A durable origin journal admits only the known
-- intermediate layouts of this migration. The backfill, version publication
-- and journal completion commit together; interrupted work resumes at 41.
-- Existing account identities, including intentional NULLs, are never remapped.

DELIMITER //
DROP PROCEDURE IF EXISTS cockatrice_migrate_0041_to_0042//
CREATE PROCEDURE cockatrice_migrate_0041_to_0042()
SQL SECURITY INVOKER
migration: BEGIN
    DECLARE schema_name VARCHAR(64);
    DECLARE schema_rows INT DEFAULT 0;
    DECLARE current_version INT DEFAULT 0;
    DECLARE ban_columns INT DEFAULT 0;
    DECLARE valid_ban_columns INT DEFAULT 0;
    DECLARE deck_columns INT DEFAULT 0;
    DECLARE valid_deck_columns INT DEFAULT 0;
    DECLARE version_tables INT DEFAULT 0;
    DECLARE valid_columns INT DEFAULT 0;
    DECLARE valid_indexes INT DEFAULT 0;
    DECLARE foreign_keys INT DEFAULT 0;
    DECLARE primary_columns TEXT;
    DECLARE has_bans BOOLEAN DEFAULT FALSE;
    DECLARE has_decks BOOLEAN DEFAULT FALSE;
    DECLARE lock_name VARCHAR(64);
    DECLARE lock_owned INT DEFAULT 0;
    DECLARE journal_tables INT DEFAULT 0;
    DECLARE journal_rows INT DEFAULT 0;
    DECLARE journal_completed INT DEFAULT 0;
    DECLARE migration_origin VARCHAR(32) DEFAULT '';

    DECLARE EXIT HANDLER FOR SQLEXCEPTION
    BEGIN
        ROLLBACK;
        IF lock_owned = 1 THEN
            DO RELEASE_LOCK(lock_name);
        END IF;
        RESIGNAL;
    END;

    SET schema_name = DATABASE();
    IF schema_name IS NULL THEN
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'Select the Servatrice database before migrating';
    END IF;
    SET lock_name = CONCAT('cockatrice_0042_', LEFT(SHA2(schema_name, 256), 32));
    SELECT GET_LOCK(lock_name, 0) INTO lock_owned;
    IF NOT (lock_owned <=> 1) THEN
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'Another schema 42 migration owns this database';
    END IF;

    SELECT COUNT(*) INTO valid_columns
    FROM information_schema.TABLES
    WHERE TABLE_SCHEMA = schema_name AND ENGINE = 'InnoDB'
      AND TABLE_NAME IN ('cockatrice_schema_version', 'cockatrice_users',
                         'cockatrice_bans', 'cockatrice_decklist_files');
    IF valid_columns <> 4 THEN
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'Expected InnoDB version, users, bans and deck tables';
    END IF;

    SELECT COUNT(*), COALESCE(MIN(version), 0)
    INTO schema_rows, current_version FROM cockatrice_schema_version;
    IF schema_rows <> 1 OR current_version NOT IN (41, 42) THEN
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'Expected exactly one schema row at version 41 or 42';
    END IF;


    SELECT COUNT(*) INTO journal_tables FROM information_schema.TABLES
    WHERE TABLE_SCHEMA = schema_name AND TABLE_NAME = 'cockatrice_schema_migrations';
    IF journal_tables = 1 THEN
        SELECT COUNT(*) INTO valid_columns FROM information_schema.COLUMNS
        WHERE TABLE_SCHEMA = schema_name AND TABLE_NAME = 'cockatrice_schema_migrations'
          AND (
            (COLUMN_NAME = 'target_version' AND DATA_TYPE = 'int'
             AND COLUMN_TYPE LIKE '%unsigned%' AND IS_NULLABLE = 'NO')
            OR (COLUMN_NAME = 'source_layout' AND DATA_TYPE = 'varchar'
                AND CHARACTER_MAXIMUM_LENGTH = 32 AND IS_NULLABLE = 'NO')
            OR (COLUMN_NAME = 'completed' AND DATA_TYPE = 'tinyint' AND IS_NULLABLE = 'NO')
          );
        SELECT GROUP_CONCAT(COLUMN_NAME ORDER BY SEQ_IN_INDEX SEPARATOR ',')
        INTO primary_columns FROM information_schema.STATISTICS
        WHERE TABLE_SCHEMA = schema_name AND TABLE_NAME = 'cockatrice_schema_migrations'
          AND INDEX_NAME = 'PRIMARY';
        IF valid_columns <> 3 OR NOT (primary_columns <=> 'target_version')
           OR NOT EXISTS (SELECT 1 FROM information_schema.TABLES
                          WHERE TABLE_SCHEMA = schema_name AND TABLE_NAME = 'cockatrice_schema_migrations'
                            AND ENGINE = 'InnoDB') THEN
            SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'Unrecognized schema migration journal';
        END IF;
        SELECT COUNT(*), COALESCE(MAX(m.source_layout), ''), COALESCE(MAX(m.completed), 0)
        INTO journal_rows, migration_origin, journal_completed
        FROM cockatrice_schema_migrations m WHERE m.target_version = 42;
        IF journal_rows = 1 AND
           (migration_origin NOT IN ('deck-sync-41', 'account-bans-41') OR journal_completed NOT IN (0, 1)) THEN
            SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'Unrecognized schema 42 migration journal entry';
        END IF;
    END IF;

    SELECT COUNT(*), COALESCE(SUM(
        (COLUMN_NAME = 'id' AND DATA_TYPE = 'bigint'
         AND COLUMN_TYPE LIKE '%unsigned%' AND IS_NULLABLE = 'NO'
         AND EXTRA LIKE '%auto_increment%')
        OR
        (COLUMN_NAME = 'user_id' AND DATA_TYPE = 'int'
         AND COLUMN_TYPE LIKE '%unsigned%' AND IS_NULLABLE = 'YES')
    ), 0) INTO ban_columns, valid_ban_columns
    FROM information_schema.COLUMNS
    WHERE TABLE_SCHEMA = schema_name AND TABLE_NAME = 'cockatrice_bans'
      AND COLUMN_NAME IN ('id', 'user_id');

    SELECT GROUP_CONCAT(COLUMN_NAME ORDER BY SEQ_IN_INDEX SEPARATOR ',')
    INTO primary_columns FROM information_schema.STATISTICS
    WHERE TABLE_SCHEMA = schema_name AND TABLE_NAME = 'cockatrice_bans'
      AND INDEX_NAME = 'PRIMARY';

    IF ban_columns = 0 THEN
        IF NOT (primary_columns <=> 'user_name,time_from') THEN
            SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'Unrecognized legacy ban primary key';
        END IF;
    ELSEIF ban_columns = 2 AND valid_ban_columns = 2
           AND primary_columns = 'id' THEN
        SELECT COUNT(*) INTO valid_indexes
        FROM (
            SELECT INDEX_NAME
            FROM information_schema.STATISTICS
            WHERE TABLE_SCHEMA = schema_name AND TABLE_NAME = 'cockatrice_bans'
              AND INDEX_NAME IN ('user_name_time_from', 'user_id_time_from')
            GROUP BY INDEX_NAME
            HAVING MAX(NON_UNIQUE) = 1 AND SUM(SUB_PART IS NOT NULL) = 0 AND
                ((INDEX_NAME = 'user_name_time_from' AND
                  GROUP_CONCAT(COLUMN_NAME ORDER BY SEQ_IN_INDEX SEPARATOR ',') = 'user_name,time_from')
                 OR
                 (INDEX_NAME = 'user_id_time_from' AND
                  GROUP_CONCAT(COLUMN_NAME ORDER BY SEQ_IN_INDEX SEPARATOR ',') = 'user_id,time_from'))
        ) AS ban_indexes;
        SELECT COUNT(*) INTO foreign_keys
        FROM information_schema.KEY_COLUMN_USAGE
        WHERE TABLE_SCHEMA = schema_name AND TABLE_NAME = 'cockatrice_bans'
          AND COLUMN_NAME = 'user_id' AND REFERENCED_TABLE_NAME IS NOT NULL;
        IF valid_indexes <> 2 OR foreign_keys <> 0 THEN
            SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'Incomplete account-ban indexes or unsafe user foreign key';
        END IF;
        SET has_bans = TRUE;
    ELSE
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'Partial or unrecognized account-ban layout';
    END IF;

    SELECT COUNT(*), COALESCE(SUM(
        (COLUMN_NAME = 'guid' AND DATA_TYPE = 'char' AND CHARACTER_MAXIMUM_LENGTH = 36 AND IS_NULLABLE = 'YES')
        OR (COLUMN_NAME = 'hash' AND DATA_TYPE = 'char' AND CHARACTER_MAXIMUM_LENGTH = 40 AND IS_NULLABLE = 'YES')
        OR (COLUMN_NAME = 'last_modified' AND DATA_TYPE = 'datetime' AND IS_NULLABLE = 'YES')
        OR (COLUMN_NAME = 'deleted' AND DATA_TYPE = 'tinyint' AND IS_NULLABLE = 'NO' AND COLUMN_DEFAULT = '0')
    ), 0) INTO deck_columns, valid_deck_columns
    FROM information_schema.COLUMNS
    WHERE TABLE_SCHEMA = schema_name AND TABLE_NAME = 'cockatrice_decklist_files'
      AND COLUMN_NAME IN ('guid', 'hash', 'last_modified', 'deleted');

    SELECT COUNT(*) INTO version_tables FROM information_schema.TABLES
    WHERE TABLE_SCHEMA = schema_name AND TABLE_NAME = 'cockatrice_deck_versions';

    IF NOT ((deck_columns = 0 AND version_tables = 0)
            OR (deck_columns = 4 AND valid_deck_columns = 4 AND version_tables IN (0, 1))) THEN
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'Partial or unrecognized deck-sync layout';
    END IF;
    IF version_tables = 1 THEN
        SELECT COUNT(*) INTO valid_columns FROM information_schema.COLUMNS
        WHERE TABLE_SCHEMA = schema_name AND TABLE_NAME = 'cockatrice_deck_versions'
          AND (
            (COLUMN_NAME = 'id' AND DATA_TYPE = 'int' AND COLUMN_TYPE LIKE '%unsigned%'
             AND IS_NULLABLE = 'NO' AND EXTRA LIKE '%auto_increment%')
            OR (COLUMN_NAME = 'deck_id' AND DATA_TYPE = 'int' AND COLUMN_TYPE LIKE '%unsigned%' AND IS_NULLABLE = 'NO')
            OR (COLUMN_NAME = 'version_no' AND DATA_TYPE = 'int' AND COLUMN_TYPE LIKE '%unsigned%' AND IS_NULLABLE = 'NO')
            OR (COLUMN_NAME = 'content' AND DATA_TYPE IN ('text', 'mediumtext', 'longtext') AND IS_NULLABLE = 'NO')
            OR (COLUMN_NAME = 'hash' AND DATA_TYPE = 'char' AND CHARACTER_MAXIMUM_LENGTH = 40 AND IS_NULLABLE = 'YES')
            OR (COLUMN_NAME = 'created_at' AND DATA_TYPE = 'datetime' AND IS_NULLABLE = 'NO')
            OR (COLUMN_NAME = 'created_by_clientid' AND DATA_TYPE = 'varchar'
                AND CHARACTER_MAXIMUM_LENGTH = 15 AND IS_NULLABLE = 'YES')
          );
        SELECT GROUP_CONCAT(COLUMN_NAME ORDER BY SEQ_IN_INDEX SEPARATOR ',')
        INTO primary_columns FROM information_schema.STATISTICS
        WHERE TABLE_SCHEMA = schema_name AND TABLE_NAME = 'cockatrice_deck_versions' AND INDEX_NAME = 'PRIMARY';
        SELECT COUNT(*) INTO valid_indexes FROM information_schema.STATISTICS
        WHERE TABLE_SCHEMA = schema_name AND TABLE_NAME = 'cockatrice_deck_versions'
          AND COLUMN_NAME = 'deck_id' AND SEQ_IN_INDEX = 1 AND SUB_PART IS NULL;
        SELECT COUNT(*) INTO foreign_keys
        FROM information_schema.KEY_COLUMN_USAGE k
        INNER JOIN information_schema.REFERENTIAL_CONSTRAINTS r
          ON r.CONSTRAINT_SCHEMA = k.CONSTRAINT_SCHEMA
          AND r.TABLE_NAME = k.TABLE_NAME AND r.CONSTRAINT_NAME = k.CONSTRAINT_NAME
        WHERE k.TABLE_SCHEMA = schema_name AND k.TABLE_NAME = 'cockatrice_deck_versions'
          AND k.COLUMN_NAME = 'deck_id' AND k.REFERENCED_TABLE_SCHEMA = schema_name
          AND k.REFERENCED_TABLE_NAME = 'cockatrice_decklist_files' AND k.REFERENCED_COLUMN_NAME = 'id'
          AND r.DELETE_RULE = 'CASCADE' AND r.UPDATE_RULE = 'CASCADE';
        IF valid_columns <> 7 OR NOT (primary_columns <=> 'id') OR valid_indexes = 0 OR foreign_keys <> 1
           OR NOT EXISTS (SELECT 1 FROM information_schema.TABLES
                          WHERE TABLE_SCHEMA = schema_name AND TABLE_NAME = 'cockatrice_deck_versions'
                            AND ENGINE = 'InnoDB') THEN
            SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'Incomplete deck-version table, keys or engine';
        END IF;
        SET has_decks = TRUE;
    END IF;

    IF current_version = 42 THEN
        IF NOT has_bans OR NOT has_decks OR (journal_rows = 1 AND journal_completed <> 1) THEN
            SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'Schema 42 is missing a required feature layout';
        END IF;
        DO RELEASE_LOCK(lock_name);
        SET lock_owned = 0;
        LEAVE migration;
    END IF;
    IF journal_rows = 0 THEN
        IF has_bans = has_decks OR (has_bans AND deck_columns <> 0) THEN
            SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'Ambiguous schema 41 without a migration origin';
        END IF;
        SET migration_origin = IF(has_decks, 'deck-sync-41', 'account-bans-41');
        IF journal_tables = 0 THEN
            CREATE TABLE cockatrice_schema_migrations (
          target_version int unsigned NOT NULL,
          source_layout varchar(32) NOT NULL,
          completed tinyint(1) NOT NULL DEFAULT 0,
          PRIMARY KEY (target_version)
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 DEFAULT COLLATE utf8mb4_unicode_ci;
        END IF;
        -- Commit recognized origin before the first implicit-commit DDL.
        START TRANSACTION;
        INSERT INTO cockatrice_schema_migrations (target_version, source_layout, completed)
        VALUES (42, migration_origin, 0);
        COMMIT;
    ELSEIF journal_completed <> 0
           OR (migration_origin = 'deck-sync-41' AND NOT has_decks)
           OR (migration_origin = 'account-bans-41' AND NOT has_bans) THEN
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'Schema 41 does not match its recorded migration origin';
    END IF;

    IF migration_origin = 'deck-sync-41' THEN
        -- Upgrade only legacy bans. Preserve all existing deck/version data.
        IF NOT has_bans THEN
            ALTER TABLE cockatrice_bans
          ADD COLUMN id bigint unsigned NOT NULL AUTO_INCREMENT FIRST,
          ADD COLUMN user_id int(7) unsigned NULL AFTER id,
          DROP PRIMARY KEY,
          ADD PRIMARY KEY (id),
          ADD KEY user_name_time_from (user_name, time_from),
          ADD KEY user_id_time_from (user_id, time_from);
        END IF;
    ELSE
        -- Already deployed account bans, including NULL identities, stay intact.
        IF deck_columns = 0 THEN
            ALTER TABLE cockatrice_decklist_files
          ADD COLUMN guid char(36) NULL AFTER tags,
          ADD COLUMN hash char(40) NULL AFTER guid,
          ADD COLUMN last_modified datetime NULL AFTER hash,
          ADD COLUMN deleted tinyint(1) NOT NULL DEFAULT 0 AFTER last_modified;
        END IF;
        IF version_tables = 0 THEN
            CREATE TABLE cockatrice_deck_versions (
          id int(7) unsigned zerofill NOT NULL auto_increment,
          deck_id int(7) unsigned zerofill NOT NULL,
          version_no int(7) unsigned NOT NULL,
          content text NOT NULL,
          hash char(40) NULL,
          created_at datetime NOT NULL,
          created_by_clientid varchar(15) NULL,
          PRIMARY KEY (id),
          KEY deck_id (deck_id),
          FOREIGN KEY (deck_id) REFERENCES cockatrice_decklist_files (id)
            ON DELETE CASCADE ON UPDATE CASCADE
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 DEFAULT COLLATE utf8mb4_unicode_ci;
        END IF;
    END IF;

    START TRANSACTION;
    IF migration_origin = 'deck-sync-41' THEN
        -- Link only accounts that already existed at the recorded ban time.
        -- This never runs against the separately deployed account-ban layout.
        UPDATE cockatrice_bans b
        INNER JOIN cockatrice_users u ON u.name = b.user_name
        SET b.user_id = u.id
        WHERE b.user_id IS NULL AND b.user_name <> '' AND b.time_from >= u.registrationDate;
    END IF;
    -- Preserve #7496's polling backfill, including an earlier deck-sync 41
    -- database that received the schema before that backfill was introduced.
    UPDATE cockatrice_decklist_files
    SET last_modified = upload_time WHERE last_modified IS NULL;
UPDATE cockatrice_schema_version SET version=42 WHERE version=41;
    IF ROW_COUNT() <> 1 THEN
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'Schema version changed during migration';
    END IF;
    UPDATE cockatrice_schema_migrations SET completed = 1
    WHERE target_version = 42 AND source_layout = migration_origin AND completed = 0;
    IF ROW_COUNT() <> 1 THEN
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'Migration origin changed during migration';
    END IF;
    COMMIT;
    DO RELEASE_LOCK(lock_name);
    SET lock_owned = 0;
END//
CALL cockatrice_migrate_0041_to_0042()//
DROP PROCEDURE cockatrice_migrate_0041_to_0042//
DELIMITER ;
