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

#include <cstdio>
#include <string>
#include <unistd.h>

#include "common.h"
#include "sqlite3sym.h"

using namespace testing::ext;
using namespace UnitTest::SQLiteTest;

#define TEST_DIR "./sqlitebinlogtest"
#define TEST_DB (TEST_DIR "/test.db")
#define TEST_BACKUP_DB (TEST_DIR "/test_bak.db")
#define TEST_DATA_COUNT 1000
#define TEST_DATA_REAL 16.1

namespace BinlogTest {
static void UtSqliteLogPrint(const void *data, int err, const char *msg)
{
    std::cout << "SqliteBinlogTest SQLite xLog err:" << err << ", msg:" << msg << std::endl;
}

static void UtPresetDb(const char *dbFile)
{
    /**
     * @tc.steps: step1. Prepare compressed db
     * @tc.expected: step1. Execute successfully
     */
    sqlite3 *db = NULL;
    std::string dbFileUri = "file:";
    dbFileUri += dbFile;
    EXPECT_EQ(sqlite3_open(dbFileUri.c_str(), &db), SQLITE_OK);
    /**
     * @tc.steps: step2. Create table and fill it, make db size big enough
     * @tc.expected: step2. Execute successfully
     */
    static const char *UT_DDL_CREATE_TABLE = "CREATE TABLE salary("
        "entryId INTEGER PRIMARY KEY,"
        "entryName Text,"
        "salary REAL,"
        "class INTEGER,"
        "extra BLOB);";
    EXPECT_EQ(sqlite3_exec(db, UT_DDL_CREATE_TABLE, NULL, NULL, NULL), SQLITE_OK);
    sqlite3_close(db);
}

static void UtEnableBinlog(sqlite3 *db)
{
    Sqlite3BinlogConfig cfg = {
        .mode = Sqlite3BinlogMode::ROW,
        .fullCallbackThreshold = 2,
        .maxFileSize = 1024 * 1024 * 4,
        .xErrorCallback = nullptr,
        .xLogFullCallback = nullptr,
        .callbackCtx = nullptr,
    };
    EXPECT_EQ(sqlite3_db_config(db, SQLITE_DBCONFIG_ENABLE_BINLOG, &cfg), SQLITE_OK);
}

static int UtQueryResult(void *data, int argc, char **argv, char **azColName)
{
    int count = *static_cast<int *>(data);
    *static_cast<int *>(data) = count + 1;
    // 2 means 2 fields, entryId, entryName
    EXPECT_EQ(argc, 2);
    return SQLITE_OK;
}

static int UtGetRecordCount(sqlite3 *db)
{
    int count = 0;
    EXPECT_EQ(sqlite3_exec(db, "SELECT entryId, entryName FROM salary;", UtQueryResult, &count, nullptr), SQLITE_OK);
    return count;
}

class SqliteBinlogTest : public testing::Test {
public:
    static void SetUpTestCase(void);
    static void TearDownTestCase(void);
    void SetUp();
    void TearDown();
};

void SqliteBinlogTest::SetUpTestCase(void)
{
    Common::RemoveDir(TEST_DIR);
    Common::MakeDir(TEST_DIR);
}

void SqliteBinlogTest::TearDownTestCase(void)
{
}

void SqliteBinlogTest::SetUp(void)
{
    std::string command = "rm -rf ";
    command += TEST_DIR "/*";
    system(command.c_str());
    sqlite3_config(SQLITE_CONFIG_LOG, &UtSqliteLogPrint, NULL);
    UtPresetDb(TEST_DB);
    UtPresetDb(TEST_BACKUP_DB);
}

void SqliteBinlogTest::TearDown(void)
{
    sqlite3_config(SQLITE_CONFIG_LOG, NULL, NULL);
}

/**
 * @tc.name: BinlogReplayTest001
 * @tc.desc: Test replay sql with test value that has single quote
 * @tc.type: FUNC
 */
HWTEST_F(SqliteBinlogTest, BinlogReplayTest001, TestSize.Level0)
{
    /**
     * @tc.steps: step1. open db and set binlog
     * @tc.expected: step1. ok
     */
    sqlite3 *db = NULL;
    EXPECT_EQ(sqlite3_open_v2(TEST_DB, &db,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr), SQLITE_OK);
    ASSERT_NE(db, nullptr);
    UtEnableBinlog(db);
    /**
     * @tc.steps: step2. Insert records with single quotes
     * @tc.expected: step2. Execute successfully
     */
    static const char *UT_SQL_INSERT_DATA =
        "INSERT INTO salary(entryId, entryName, salary, class) VALUES(?,?,?,?);";
    sqlite3_stmt *insertStmt = NULL;
    EXPECT_EQ(sqlite3_prepare_v2(db, UT_SQL_INSERT_DATA, -1, &insertStmt, NULL), SQLITE_OK);
    for (int i = 0; i < TEST_DATA_COUNT; i++) {
        // bind parameters, 1, 2, 3, 4 are sequence number of fields
        sqlite3_bind_int(insertStmt, 1, i + 1);
        sqlite3_bind_text(insertStmt, 2, "'salary-entry-name'", -1, SQLITE_STATIC);
        sqlite3_bind_double(insertStmt, 3, TEST_DATA_REAL + i);
        sqlite3_bind_int(insertStmt, 4, i + 1);
        EXPECT_EQ(sqlite3_step(insertStmt), SQLITE_DONE);
        sqlite3_reset(insertStmt);
    }
    sqlite3_finalize(insertStmt);
    /**
     * @tc.steps: step3. binlog replay to write into backup db
     * @tc.expected: step3. Return SQLITE_OK
     */
    sqlite3 *backupDb = NULL;
    EXPECT_EQ(sqlite3_open_v2(TEST_BACKUP_DB, &backupDb,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr), SQLITE_OK);
    ASSERT_NE(backupDb, nullptr);
    EXPECT_EQ(sqlite3_replay_binlog(db, backupDb), SQLITE_OK);
    /**
     * @tc.steps: step4. check db count
     * @tc.expected: step4. both db have TEST_DATA_COUNT
     */
    EXPECT_EQ(UtGetRecordCount(db), TEST_DATA_COUNT);
    EXPECT_EQ(UtGetRecordCount(backupDb), TEST_DATA_COUNT);
    sqlite3_close_v2(db);
    sqlite3_close_v2(backupDb);
}

/**
 * @tc.name: BinlogReplayTest002
 * @tc.desc: Test replay sql with zero blob at the end of the record
 * @tc.type: FUNC
 */
HWTEST_F(SqliteBinlogTest, BinlogReplayTest002, TestSize.Level1)
{
    /**
     * @tc.steps: step1. open db and set binlog
     * @tc.expected: step1. ok
     */
    sqlite3 *db = NULL;
    EXPECT_EQ(sqlite3_open_v2(TEST_DB, &db,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr), SQLITE_OK);
    ASSERT_NE(db, nullptr);
    UtEnableBinlog(db);
    /**
     * @tc.steps: step2. Insert records with zeroblob
     * @tc.expected: step2. Execute successfully
     */
    static const char *UT_SQL_INSERT_DATA =
        "INSERT INTO salary(entryId, entryName, salary, class, extra) VALUES(?,?,?,?,?);";
    sqlite3_stmt *insertStmt = NULL;
    EXPECT_EQ(sqlite3_prepare_v2(db, UT_SQL_INSERT_DATA, -1, &insertStmt, NULL), SQLITE_OK);
    for (int i = 0; i < TEST_DATA_COUNT; i++) {
        // bind parameters, 1, 2, 3, 4, 5 are sequence number of fields
        sqlite3_bind_int(insertStmt, 1, i + 1);
        sqlite3_bind_text(insertStmt, 2, "'salary-entry-name'", -1, SQLITE_STATIC);
        sqlite3_bind_double(insertStmt, 3, TEST_DATA_REAL + i);
        sqlite3_bind_int(insertStmt, 4, i + 1);
        sqlite3_bind_zeroblob(insertStmt, 5, i + 1);
        EXPECT_EQ(sqlite3_step(insertStmt), SQLITE_DONE);
        sqlite3_reset(insertStmt);
    }
    sqlite3_finalize(insertStmt);
    /**
     * @tc.steps: step3. binlog replay to write into backup db
     * @tc.expected: step3. Return SQLITE_OK
     */
    sqlite3 *backupDb = NULL;
    EXPECT_EQ(sqlite3_open_v2(TEST_BACKUP_DB, &backupDb,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr), SQLITE_OK);
    ASSERT_NE(backupDb, nullptr);
    EXPECT_EQ(sqlite3_replay_binlog(db, backupDb), SQLITE_OK);
    /**
     * @tc.steps: step4. check db count
     * @tc.expected: step4. both db have TEST_DATA_COUNT
     */
    EXPECT_EQ(UtGetRecordCount(db), TEST_DATA_COUNT);
    EXPECT_EQ(UtGetRecordCount(backupDb), TEST_DATA_COUNT);
    sqlite3_close_v2(db);
    sqlite3_close_v2(backupDb);
}

/**
 * @tc.name: BinlogInterfaceTest001
 * @tc.desc: Test replay sql with invalid db pointer
 * @tc.type: FUNC
 */
HWTEST_F(SqliteBinlogTest, BinlogInterfaceTest001, TestSize.Level0)
{
    /**
     * @tc.steps: step1. open db and set binlog
     * @tc.expected: step1. ok
     */
    sqlite3 *db = NULL;
    EXPECT_EQ(sqlite3_open_v2(TEST_DB, &db,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr), SQLITE_OK);
    ASSERT_NE(db, nullptr);
    UtEnableBinlog(db);
    /**
     * @tc.steps: step2. Insert records
     * @tc.expected: step2. Execute successfully
     */
    static const char *UT_SQL_INSERT_DATA =
        "INSERT INTO salary(entryId, entryName, salary, class) VALUES(?,?,?,?);";
    sqlite3_stmt *insertStmt = NULL;
    EXPECT_EQ(sqlite3_prepare_v2(db, UT_SQL_INSERT_DATA, -1, &insertStmt, NULL), SQLITE_OK);
    for (int i = 0; i < TEST_DATA_COUNT; i++) {
        // bind parameters, 1, 2, 3, 4 are sequence number of fields
        sqlite3_bind_int(insertStmt, 1, i + 1);
        sqlite3_bind_text(insertStmt, 2, "'salary-entry-name'", -1, SQLITE_STATIC);
        sqlite3_bind_double(insertStmt, 3, TEST_DATA_REAL + i);
        sqlite3_bind_int(insertStmt, 4, i + 1);
        EXPECT_EQ(sqlite3_step(insertStmt), SQLITE_DONE);
        sqlite3_reset(insertStmt);
    }
    sqlite3_finalize(insertStmt);
    /**
     * @tc.steps: step3. binlog replay to write to a closed db
     * @tc.expected: step3. Return SQLITE_MISUSE
     */
    sqlite3 *backupDb = NULL;
    EXPECT_EQ(sqlite3_open_v2(TEST_BACKUP_DB, &backupDb,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr), SQLITE_OK);
    ASSERT_NE(backupDb, nullptr);
    sqlite3_close_v2(backupDb);
    EXPECT_EQ(sqlite3_replay_binlog(db, backupDb), SQLITE_MISUSE);
    /**
     * @tc.steps: step4. binlog replay to read from a closed db
     * @tc.expected: step4. Return SQLITE_MISUSE
     */
    EXPECT_EQ(sqlite3_open_v2(TEST_BACKUP_DB, &backupDb,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr), SQLITE_OK);
    ASSERT_NE(backupDb, nullptr);
    sqlite3_close_v2(db);
    EXPECT_EQ(sqlite3_replay_binlog(db, backupDb), SQLITE_MISUSE);
    sqlite3_close_v2(db);
    sqlite3_close_v2(backupDb);
}

/**
 * @tc.name: BinlogInterfaceTest002
 * @tc.desc: Test clean with invalid db pointer
 * @tc.type: FUNC
 */
HWTEST_F(SqliteBinlogTest, BinlogInterfaceTest002, TestSize.Level0)
{
    /**
     * @tc.steps: step1. open db and set binlog
     * @tc.expected: step1. ok
     */
    sqlite3 *db = NULL;
    EXPECT_EQ(sqlite3_open_v2(TEST_DB, &db,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr), SQLITE_OK);
    ASSERT_NE(db, nullptr);
    UtEnableBinlog(db);
    /**
     * @tc.steps: step2. cal binlog clean with closed db pointer
     * @tc.expected: step2. Return SQLITE_MISUSE
     */
    sqlite3_close_v2(db);
    EXPECT_EQ(sqlite3_clean_binlog(db, BinlogFileCleanModeE::BINLOG_FILE_CLEAN_READ_MODE), SQLITE_MISUSE);
}
}  // namespace BinlogTest
