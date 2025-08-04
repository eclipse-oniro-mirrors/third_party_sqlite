/*
 * Copyright (c) 2025 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include <gtest/gtest.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <chrono>
#include <cstdio>
#include <fstream>
#include <random>
#include <string>
#include <unistd.h>
#include <sstream>
#include <vector>

#include "common.h"
#include "sqlite3sym.h"

using namespace testing::ext;
using namespace UnitTest::SQLiteTest;

#define TEST_DIR "./sqlitecksumtest"
#define TEST_DB (TEST_DIR "/test.db")
#define TEST_PRESET_TABLE_COUNT 20
#define TEST_PRESET_DATA_COUNT 1000
#define TEST_DATA_REAL (16.1)

namespace Test {
class SQLiteCksumTest : public testing::Test {
public:
    static void SetUpTestCase(void);
    static void TearDownTestCase(void);
    void SetUp();
    void TearDown();

    static void UtSqliteLogPrint(const void *data, int err, const char *msg);
    static void UtBackupDatabase(sqlite3 *srcDb, sqlite3 *destDb);
    static void UtPresetDb(const std::string &dbFile, bool enableCksum);
    static int UtSqliteExecCallback(void *data, int argc, char **argv, char **azColName);
};

void SQLiteCksumTest::SetUpTestCase(void)
{
    Common::RemoveDir(TEST_DIR);
    Common::MakeDir(TEST_DIR);
}

void SQLiteCksumTest::TearDownTestCase(void)
{
}

void SQLiteCksumTest::SetUp(void)
{
    sqlite3_config(SQLITE_CONFIG_LOG, &SQLiteCksumTest::UtSqliteLogPrint, NULL);
    EXPECT_EQ(sqlite3_register_cksumvfs(NULL), SQLITE_OK);
}

void SQLiteCksumTest::TearDown(void)
{
    EXPECT_EQ(sqlite3_unregister_cksumvfs(), SQLITE_OK);
    sqlite3_config(SQLITE_CONFIG_LOG, NULL, NULL);
}

void SQLiteCksumTest::UtSqliteLogPrint(const void *data, int err, const char *msg)
{
    std::cout << "SQLiteCksumTest xLog err:" << err << ", msg:" << msg << std::endl;
}

int SQLiteCksumTest::UtSqliteExecCallback(void *data, int argc, char **argv, char **azColName)
{
    std::cout << (const char *)data << " result:" << std::endl;
    for (int i = 0; i < argc; i++) {
        std::cout << azColName[i] << " = " << (argv[i] ? argv[i] : "NULL") << std::endl;
    }
    std::cout <<std::endl;
    return 0;
}

void SQLiteCksumTest::UtBackupDatabase(sqlite3 *srcDb, sqlite3 *destDb)
{
    sqlite3_backup *back = nullptr;
    back = sqlite3_backup_init(destDb, "main", srcDb, "main");
    EXPECT_TRUE(back != nullptr);
    EXPECT_EQ(sqlite3_backup_step(back, -1), SQLITE_DONE);
    sqlite3_backup_finish(back);
}

void SQLiteCksumTest::UtPresetDb(const std::string &dbFile, bool enableCksum)
{
    /**
     * @tc.steps: step1. Prepare db used to simulate corrupted
     * @tc.expected: step1. Execute successfully
     */
    sqlite3 *db = NULL;
    std::string dbFileUri = enableCksum ? "file:" : "";
    dbFileUri += dbFile;
    dbFileUri += enableCksum ? "?vfs=cksmvfs" : "";
    EXPECT_EQ(sqlite3_open(dbFileUri.c_str(), &db), SQLITE_OK);
    /**
     * @tc.steps: step1. Enable cksumvfs using PRAGMA checksum_persist_enable,
     * @tc.expected: step1. Execute successfully
     */
    if (enableCksum) {
        EXPECT_EQ(sqlite3_exec(db, "PRAGMA checksum_persist_enable=ON;", NULL, NULL, NULL), SQLITE_OK);
    }
    EXPECT_EQ(sqlite3_exec(db, "PRAGMA meta_double_write=enabled;", NULL, NULL, NULL), SQLITE_OK);
    EXPECT_EQ(sqlite3_exec(db, "PRAGMA journal_mode=WAL;", NULL, NULL, NULL), SQLITE_OK);
    /**
     * @tc.steps: step1. Create table and fill it, make db size big enough
     * @tc.expected: step1. Execute successfully
     */
    static const char *UT_DDL_CREATE_TABLE = "CREATE TABLE salary("
        "entryId INTEGER PRIMARY KEY,"
        "entryName Text,"
        "salary REAL,"
        "class INTEGER);";
    EXPECT_EQ(sqlite3_exec(db, UT_DDL_CREATE_TABLE, NULL, NULL, NULL), SQLITE_OK);
    std::string tmp = "CREATE TABLE test";
    for (size_t i = 0; i < TEST_PRESET_TABLE_COUNT; i++) {
        std::string ddl = "CREATE TABLE test";
        ddl += std::to_string(i);
        ddl += "(entryId INTEGER PRIMARY KEY,entryName Text,salary REAL,class INTEGER);";
        EXPECT_EQ(sqlite3_exec(db, ddl.c_str(), NULL, NULL, NULL), SQLITE_OK);
    }
    static const char *UT_SQL_INSERT_DATA = "INSERT INTO salary(entryId, entryName, salary, class) VALUES(?,?,?,?);";
    sqlite3_stmt *insertStmt = NULL;
    EXPECT_EQ(sqlite3_prepare_v2(db, UT_SQL_INSERT_DATA, -1, &insertStmt, NULL), SQLITE_OK);
    for (int i = 0; i < TEST_PRESET_DATA_COUNT; i++) {
        // bind parameters, 1, 2, 3, 4 are sequence number of fields
        sqlite3_bind_int(insertStmt, 1, i + 1);
        sqlite3_bind_text(insertStmt, 2, "salary-entry-name", -1, SQLITE_STATIC);
        sqlite3_bind_double(insertStmt, 3, TEST_DATA_REAL + i);
        sqlite3_bind_int(insertStmt, 4, i + 1);
        EXPECT_EQ(sqlite3_step(insertStmt), SQLITE_DONE);
        sqlite3_reset(insertStmt);
    }
    sqlite3_finalize(insertStmt);
    sqlite3_close(db);
}

/**
 * @tc.name: CksumTest001
 * @tc.desc: Test to enable cksum before backup
 * @tc.type: FUNC
 */
HWTEST_F(SQLiteCksumTest, CksumTest001, TestSize.Level0)
{
    /**
     * @tc.steps: step1. Create a new db as main, disable cksum
     * @tc.expected: step1. Execute successfully
     */
    std::string dbPath = TEST_DIR "/test001.db";
    UtPresetDb(dbPath, false);
    sqlite3 *db = nullptr;
    EXPECT_EQ(sqlite3_open(dbPath.c_str(), &db),SQLITE_OK);

    /**
     * @tc.steps: step2. Create a new db as slave, enable cksum
     * @tc.expected: step2. Execute successfully
     */
    std::string slaveDbPath = TEST_DIR "/test001_slave.db";
    sqlite3 *slaveDb = nullptr;
    std::string dbFileUri = "file:";
    dbFileUri += slaveDbPath;
    dbFileUri += "?vfs=cksmvfs";
    EXPECT_EQ(sqlite3_open(dbFileUri.c_str(), &slaveDb),SQLITE_OK);
    EXPECT_EQ(sqlite3_exec(slaveDb, "PRAGMA checksum_persist_enable=ON;", NULL, NULL, NULL), SQLITE_OK);
    EXPECT_EQ(sqlite3_exec(slaveDb, "PRAGMA meta_double_write=enabled;", NULL, NULL, NULL), SQLITE_OK);
    EXPECT_EQ(sqlite3_exec(slaveDb, "PRAGMA journal_mode=WAL;", NULL, NULL, NULL), SQLITE_OK);
    UtBackupDatabase(db, slaveDb);
    sqlite3_close(slaveDb);
    /**
     * @tc.steps: step3. Open slave db, make an integrity check
     * @tc.expected: step3. Execute successfully
     */
    EXPECT_EQ(sqlite3_open(dbFileUri.c_str(), &slaveDb),SQLITE_OK);
    EXPECT_EQ(sqlite3_exec(slaveDb, "PRAGMA integrity_check;", UtSqliteExecCallback, (void *)"PRAGMA", nullptr),
        SQLITE_OK);
    sqlite3_close(slaveDb);
}

}  // namespace Test
