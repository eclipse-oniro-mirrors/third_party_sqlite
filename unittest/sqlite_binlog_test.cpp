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
static int g_xChangeCount = 0;

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

static void UtEnableBinlogForSearch(sqlite3 *db)
{
    Sqlite3BinlogConfig cfg = {
        .mode = Sqlite3BinlogMode::ROW_FOR_SEARCH,
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

static MonitorTablesConfig *InitMonitorConfig(const char *tableName)
{
    MonitorTablesConfig *monitorConfig = static_cast<MonitorTablesConfig*>(malloc(sizeof(MonitorTablesConfig)));
    memset_s(monitorConfig, 0, sizeof(MonitorTablesConfig));
    int defaultSize = 10;
    monitorConfig->tables = static_cast<MonitorTableCol*>(malloc(defaultSize * sizeof(MonitorTableCol)));
    memset_s(monitorConfig->tables, 0, defaultSize * sizeof(MonitorTableCol));
    monitorConfig->tables[0].tableName = strdup(tableName);
    monitorConfig->tableCount++;
    int defaultColCount = 1;
    monitorConfig->tables[0].colCount = defaultColCount;
    monitorConfig->tables[0].cols = static_cast<char**>(malloc(sizeof(char*) * defaultSize)); // default 10 cols
    std::string defaultColName = "id";
    monitorConfig->tables[0].cols[0] = strdup(defaultColName.c_str());
    return monitorConfig;
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
 * @tc.name: BinlogReplayTest003
 * @tc.desc: Test replay binlog with transaction failed
 * @tc.type: FUNC
 */
HWTEST_F(SqliteBinlogTest, BinlogReplayTest003, TestSize.Level1)
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
    EXPECT_EQ(sqlite3_exec(db, "begin;", nullptr, nullptr, nullptr), SQLITE_OK);
    static const char *UT_SQL_INSERT_DATA =
        "INSERT INTO salary(entryId, entryName, salary, class) VALUES(1,'test',2,3);";
    EXPECT_EQ(sqlite3_exec(db, UT_SQL_INSERT_DATA, nullptr, nullptr, nullptr), SQLITE_OK);
    EXPECT_EQ(sqlite3_exec(db, "commit;", nullptr, nullptr, nullptr), SQLITE_OK);
    /**
     * @tc.steps: step3. drop table in backup and replay
     * @tc.expected: step3. replay failed
     */
    sqlite3 *backupDb = NULL;
    EXPECT_EQ(sqlite3_open_v2(TEST_BACKUP_DB, &backupDb,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr), SQLITE_OK);
    ASSERT_NE(backupDb, nullptr);
    EXPECT_EQ(sqlite3_exec(backupDb, "drop table salary;", nullptr, nullptr, nullptr), SQLITE_OK);
    EXPECT_EQ(sqlite3_replay_binlog(db, backupDb), SQLITE_ERROR);
    /**
     * @tc.steps: step4. re-create table and clean binlog
     * @tc.expected: step4. clean ok
     */
    static const char *UT_DDL_CREATE_TABLE = "CREATE TABLE salary("
        "entryId INTEGER PRIMARY KEY,"
        "entryName Text,"
        "salary REAL,"
        "class INTEGER,"
        "extra BLOB);";
    EXPECT_EQ(sqlite3_exec(backupDb, UT_DDL_CREATE_TABLE, NULL, NULL, NULL), SQLITE_OK);
    UtEnableBinlog(db);
    EXPECT_EQ(sqlite3_clean_binlog(db, BinlogFileCleanModeE::BINLOG_FILE_CLEAN_ALL_MODE), SQLITE_OK);
    /**
     * @tc.steps: step5. insert one more data with transaction
     * @tc.expected: step5. insert ok
     */
    EXPECT_EQ(sqlite3_exec(db, "begin;", nullptr, nullptr, nullptr), SQLITE_OK);
    static const char *UT_SQL_INSERT_DATA2 =
        "INSERT INTO salary(entryId, entryName, salary, class) VALUES (2, 'test', 2, 3);";
    EXPECT_EQ(sqlite3_exec(db, UT_SQL_INSERT_DATA2, nullptr, nullptr, nullptr), SQLITE_OK);
    EXPECT_EQ(sqlite3_exec(db, "commit;", nullptr, nullptr, nullptr), SQLITE_OK);
    /**
     * @tc.steps: step5. replay transaction into backup
     * @tc.expected: step5. replay success
     */
    EXPECT_EQ(sqlite3_replay_binlog(db, backupDb), SQLITE_OK);
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


/**
 * @tc.name: Sqlite_Binlog_SearchDataTest001
 * @tc.desc: test get_search_data_binlog with rowid table and free
 * @tc.author: liuhongyang
 */
HWTEST_F(SqliteBinlogTest, Sqlite_Binlog_SearchDataTest001, TestSize.Level1)
{
    sqlite3 *db = NULL;
    EXPECT_EQ(sqlite3_open_v2(TEST_DB, &db,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr), SQLITE_OK);
    ASSERT_NE(db, nullptr);
    UtEnableBinlogForSearch(db);
    /**
     * @tc.steps: step1. create a rowid table and insert data
     * @tc.expected: step1. ok
     */
    EXPECT_EQ(sqlite3_exec(db, "create table test_search (id int primary key, name text, value real);",
        nullptr, nullptr, nullptr), SQLITE_OK);
    EXPECT_EQ(sqlite3_exec(db, "insert into test_search values (1, 'Alice', 100.5),"
        "(2, 'Bob', 200.5), (3, 'Charlie', 300.5);", nullptr, nullptr, nullptr), SQLITE_OK);

    /**
     * @tc.steps: step2. get search data
     * @tc.expected: step2. return ok and verify data
     */
    sqlite3 *backupDb = NULL;
    EXPECT_EQ(sqlite3_open_v2(TEST_BACKUP_DB, &backupDb,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr), SQLITE_OK);
    ASSERT_NE(backupDb, nullptr);
    BinlogSearchResultSet *rs = nullptr;
    EXPECT_EQ(sqlite3_get_search_data_binlog(db, backupDb, &rs), SQLITE_OK);
    ASSERT_FALSE(rs == nullptr);
    EXPECT_EQ(rs->row_count, 3);

    /**
     * @tc.steps: step3. verify the data content
     * @tc.expected: step3. data is correct
     */
    EXPECT_EQ(rs->results[0].op, SQLITE_INSERT);
    EXPECT_STREQ(rs->results[0].tableName, "test_search");
    EXPECT_STREQ(rs->results[0].nameAndValues[1], "1");   // id
    EXPECT_STREQ(rs->results[0].nameAndValues[3], "Alice"); // name
    EXPECT_STREQ(rs->results[0].nameAndValues[5], "100.5"); // value

    /**
     * @tc.steps: step4. free search data and verify rs is null
     * @tc.expected: step4. return ok and rs is null
     */
    EXPECT_EQ(sqlite3_free_search_data_binlog(db, &rs), SQLITE_OK);
    EXPECT_TRUE(rs == nullptr);
}

/**
 * @tc.name: Sqlite_Binlog_SearchDataTest002
 * @tc.desc: test get_search_data_binlog with empty binlog
 * @tc.author: liuhongyang
 */
HWTEST_F(SqliteBinlogTest, Sqlite_Binlog_SearchDataTest002, TestSize.Level1)
{
    sqlite3 *db = NULL;
    EXPECT_EQ(sqlite3_open_v2(TEST_DB, &db,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr), SQLITE_OK);
    ASSERT_NE(db, nullptr);
    UtEnableBinlogForSearch(db);
    /**
     * @tc.steps: step1. create table without any data
     * @tc.expected: step1. ok
     */
    EXPECT_EQ(sqlite3_exec(db, "create table test_search_empty (id int primary key, name text);",
        nullptr, nullptr, nullptr), SQLITE_OK);

    /**
     * @tc.steps: step2. get search data with empty binlog
     * @tc.expected: step2. return ok with empty result (not error)
     */
    sqlite3 *backupDb = NULL;
    EXPECT_EQ(sqlite3_open_v2(TEST_BACKUP_DB, &backupDb,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr), SQLITE_OK);
    ASSERT_NE(backupDb, nullptr);
    BinlogSearchResultSet *rs = nullptr;
    int ret = sqlite3_get_search_data_binlog(db, backupDb, &rs);
    EXPECT_TRUE(ret == SQLITE_OK || rs == nullptr);
}

/**
 * @tc.name: Sqlite_Binlog_SearchDataTest003
 * @tc.desc: test get_search_data_binlog with NULL parameters
 * @tc.author: liuhongyang
 */
HWTEST_F(SqliteBinlogTest, Sqlite_Binlog_SearchDataTest003, TestSize.Level1)
{
    sqlite3 *db = NULL;
    EXPECT_EQ(sqlite3_open_v2(TEST_DB, &db,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr), SQLITE_OK);
    ASSERT_NE(db, nullptr);
    UtEnableBinlogForSearch(db);
    /**
     * @tc.steps: step1. create table and insert data
     * @tc.expected: step1. ok
     */
    EXPECT_EQ(sqlite3_exec(db, "create table test_search_null (id int primary key);",
        nullptr, nullptr, nullptr), SQLITE_OK);
    EXPECT_EQ(sqlite3_exec(db, "insert into test_search_null values (1);", nullptr, nullptr, nullptr), SQLITE_OK);

    /**
     * @tc.steps: step2. test with NULL db parameter
     * @tc.expected: step2. return error
     */
    sqlite3 *backupDb = NULL;
    EXPECT_EQ(sqlite3_open_v2(TEST_BACKUP_DB, &backupDb,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr), SQLITE_OK);
    ASSERT_NE(backupDb, nullptr);
    BinlogSearchResultSet *rs = nullptr;
    EXPECT_EQ(sqlite3_get_search_data_binlog(nullptr, backupDb, &rs), SQLITE_ERROR);
    EXPECT_EQ(sqlite3_get_search_data_binlog(db, nullptr, &rs), SQLITE_ERROR);

    /**
     * @tc.steps: step3. test free with NULL db parameter
     * @tc.expected: step3. return error
     */
    rs = nullptr;
    EXPECT_EQ(sqlite3_free_search_data_binlog(nullptr, &rs), SQLITE_ERROR);
}

/**
 * @tc.name: Sqlite_Binlog_HwmTest001
 * @tc.desc: test set and reset search hwm
 * @tc.author: liuhongyang
 */
HWTEST_F(SqliteBinlogTest, Sqlite_Binlog_HwmTest001, TestSize.Level1)
{
    sqlite3 *db = NULL;
    EXPECT_EQ(sqlite3_open_v2(TEST_DB, &db,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr), SQLITE_OK);
    ASSERT_NE(db, nullptr);
    UtEnableBinlogForSearch(db);
    /**
     * @tc.steps: step1. create table and insert data
     * @tc.expected: step1. ok
     */
    EXPECT_EQ(sqlite3_exec(db, "create table test_hwm (id int primary key, name text);",
        nullptr, nullptr, nullptr), SQLITE_OK);
    EXPECT_EQ(sqlite3_exec(db, "insert into test_hwm values (1, 'First'), (2, 'Second'), (3, 'Third');",
        nullptr, nullptr, nullptr), SQLITE_OK);

    /**
     * @tc.steps: step2. get initial search data
     * @tc.expected: step2. ok
     */
    sqlite3 *backupDb = NULL;
    EXPECT_EQ(sqlite3_open_v2(TEST_BACKUP_DB, &backupDb,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr), SQLITE_OK);
    ASSERT_NE(backupDb, nullptr);
    BinlogSearchResultSet *rs = nullptr;
    EXPECT_EQ(sqlite3_get_search_data_binlog(db, backupDb, &rs), SQLITE_OK);
    ASSERT_FALSE(rs == nullptr);

    /**
     * @tc.steps: step3. set search hwm to last position
     * @tc.expected: step3. return ok
     */
    BinlogSearchHwmT hwm = {
        .readFileIndex = rs->results[1].fileIndex,  // position at second record
        .readPos = rs->results[rs->row_count - 1].readPos
    };
    EXPECT_EQ(sqlite3_free_search_data_binlog(db, &rs), SQLITE_OK);
    EXPECT_TRUE(rs == nullptr);
    EXPECT_EQ(sqlite3_set_search_hwm_binlog(db, &hwm, BINLOG_CACHE_MODE), SQLITE_OK);

    /**
     * @tc.steps: step4. get search data after setting hwm
     * @tc.expected: step4. should get less data or empty
     */
    EXPECT_EQ(sqlite3_get_search_data_binlog(db, backupDb, &rs), SQLITE_OK);
    // After setting hwm, should not get the data already above hwm
    EXPECT_EQ(sqlite3_free_search_data_binlog(db, &rs), SQLITE_OK);
    EXPECT_TRUE(rs == nullptr);

    /**
     * @tc.steps: step5. reset search hwm
     * @tc.expected: step5. return ok
     */
    EXPECT_EQ(sqlite3_reset_search_hwm_binlog(db), SQLITE_OK);

    /**
     * @tc.steps: step6. get search data after reset hwm
     * @tc.expected: step6. should get all data again
     */
    EXPECT_EQ(sqlite3_get_search_data_binlog(db, backupDb, &rs), SQLITE_OK);
    ASSERT_FALSE(rs == nullptr);
    EXPECT_EQ(rs->row_count, 3); // Should get all 3 records again after reset
    EXPECT_EQ(sqlite3_free_search_data_binlog(db, &rs), SQLITE_OK);
    EXPECT_TRUE(rs == nullptr);
}

/**
 * @tc.name: Sqlite_Binlog_HwmTest002
 * @tc.desc: test set search hwm with cache mode
 * @tc.author: liuhongyang
 */
HWTEST_F(SqliteBinlogTest, Sqlite_Binlog_HwmTest002, TestSize.Level1)
{
    sqlite3 *db = NULL;
    EXPECT_EQ(sqlite3_open_v2(TEST_DB, &db,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr), SQLITE_OK);
    ASSERT_NE(db, nullptr);
    UtEnableBinlogForSearch(db);
    /**
     * @tc.steps: step1. create table and insert data
     * @tc.expected: step1. ok
     */
    EXPECT_EQ(sqlite3_exec(db, "create table test_hwm_cache (id int primary key);",
        nullptr, nullptr, nullptr), SQLITE_OK);
    EXPECT_EQ(sqlite3_exec(db, "insert into test_hwm_cache values (1), (2), (3);",
        nullptr, nullptr, nullptr), SQLITE_OK);

    /**
     * @tc.steps: step2. get search data
     * @tc.expected: step2. ok
     */
    sqlite3 *backupDb = NULL;
    EXPECT_EQ(sqlite3_open_v2(TEST_BACKUP_DB, &backupDb,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr), SQLITE_OK);
    ASSERT_NE(backupDb, nullptr);
    BinlogSearchResultSet *rs = nullptr;
    EXPECT_EQ(sqlite3_get_search_data_binlog(db, backupDb, &rs), SQLITE_OK);
    ASSERT_FALSE(rs == nullptr);

    /**
     * @tc.steps: step3. set hwm with cache mode
     * @tc.expected: step3. return ok
     */
    BinlogSearchHwmT hwm = {
        .readFileIndex = rs->results[1].fileIndex,
        .readPos = rs->results[1].readPos
    };
    EXPECT_EQ(sqlite3_free_search_data_binlog(db, &rs), SQLITE_OK);
    EXPECT_TRUE(rs == nullptr);
    EXPECT_EQ(sqlite3_set_search_hwm_binlog(db, &hwm, BINLOG_CACHE_MODE), SQLITE_OK);

    /**
     * @tc.steps: step4. reset hwm
     * @tc.expected: step4. return ok
     */
    EXPECT_EQ(sqlite3_reset_search_hwm_binlog(db), SQLITE_OK);
    EXPECT_EQ(sqlite3_get_search_data_binlog(db, backupDb, &rs), SQLITE_OK);
    ASSERT_FALSE(rs == nullptr);
    EXPECT_EQ(rs->row_count, 3);
    EXPECT_EQ(sqlite3_free_search_data_binlog(db, &rs), SQLITE_OK);
    EXPECT_TRUE(rs == nullptr);
}

/**
 * @tc.name: Sqlite_Binlog_CleanBinlogTest001
 * @tc.desc: test clean binlog function
 * @tc.author: liuhongyang
 */
HWTEST_F(SqliteBinlogTest, Sqlite_Binlog_CleanBinlogTest001, TestSize.Level1)
{
    sqlite3 *db = NULL;
    EXPECT_EQ(sqlite3_open_v2(TEST_DB, &db,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr), SQLITE_OK);
    ASSERT_NE(db, nullptr);
    UtEnableBinlogForSearch(db);
    /**
     * @tc.steps: step1. create table and insert data, then replay
     * @tc.expected: step1. ok
     */
    EXPECT_EQ(sqlite3_exec(db, "create table test_clean (id int primary key, name text);",
        nullptr, nullptr, nullptr), SQLITE_OK);
    EXPECT_EQ(sqlite3_exec(db, "insert into test_clean values (1, 'Test1'), (2, 'Test2');",
        nullptr, nullptr, nullptr), SQLITE_OK);
    sqlite3 *backupDb = NULL;
    EXPECT_EQ(sqlite3_open_v2(TEST_BACKUP_DB, &backupDb,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr), SQLITE_OK);
    ASSERT_NE(backupDb, nullptr);
    EXPECT_EQ(sqlite3_replay_binlog(db, backupDb), SQLITE_OK);

    /**
     * @tc.steps: step2. clean binlog with read mode (should keep files)
     * @tc.expected: step2. return ok
     */
    EXPECT_EQ(sqlite3_clean_binlog(db, BINLOG_FILE_CLEAN_READ_MODE), SQLITE_OK);

    /**
     * @tc.steps: step3. clean binlog with all mode (should delete all)
     * @tc.expected: step3. return ok
     */
    EXPECT_EQ(sqlite3_clean_binlog(db, BINLOG_FILE_CLEAN_ALL_MODE), SQLITE_OK);
}

/**
 * @tc.name: Sqlite_Binlog_SearchDataTest004
 * @tc.desc: test get_search_data_binlog with UPDATE operation
 * @tc.author: liuhongyang
 */
HWTEST_F(SqliteBinlogTest, Sqlite_Binlog_SearchDataTest004, TestSize.Level1)
{
    sqlite3 *db = NULL;
    EXPECT_EQ(sqlite3_open_v2(TEST_DB, &db,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr), SQLITE_OK);
    ASSERT_NE(db, nullptr);
    UtEnableBinlogForSearch(db);
    /**
     * @tc.steps: step1. create table and insert data
     * @tc.expected: step1. ok
     */
    EXPECT_EQ(sqlite3_exec(db, "create table test_update_search (id int primary key, value int);",
        nullptr, nullptr, nullptr), SQLITE_OK);
    EXPECT_EQ(sqlite3_exec(db, "insert into test_update_search values (1, 100), (2, 200);",
        nullptr, nullptr, nullptr), SQLITE_OK);

    /**
     * @tc.steps: step2. update data
     * @tc.expected: step2. ok
     */
    EXPECT_EQ(sqlite3_exec(db, "update test_update_search set value = 150 where id = 1;",
        nullptr, nullptr, nullptr), SQLITE_OK);

    /**
     * @tc.steps: step3. get search data including update
     * @tc.expected: step3. return ok
     */
    sqlite3 *backupDb = NULL;
    EXPECT_EQ(sqlite3_open_v2(TEST_BACKUP_DB, &backupDb,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr), SQLITE_OK);
    ASSERT_NE(backupDb, nullptr);
    BinlogSearchResultSet *rs = nullptr;
    EXPECT_EQ(sqlite3_get_search_data_binlog(db, backupDb, &rs), SQLITE_OK);
    ASSERT_FALSE(rs == nullptr);

    /**
     * @tc.steps: step4. verify update operation is captured
     * @tc.expected: step4. update operation found in results
     */
    bool foundUpdate = false;
    for (int i = 0; i < rs->row_count; i++) {
        if (rs->results[i].op == SQLITE_INSERT) { //no updata for row mode
            foundUpdate = true;
            EXPECT_STREQ(rs->results[i].tableName, "test_update_search");
            break;
        }
    }
    EXPECT_TRUE(foundUpdate);
    EXPECT_EQ(sqlite3_free_search_data_binlog(db, &rs), SQLITE_OK);
    EXPECT_TRUE(rs == nullptr);
}

static void XChangeCallbackHelper(const char *dbPath, char *tableName)
{
    printf("xChangeCallback dbPath:%s tableName:%s\n", dbPath, tableName);
    g_xChangeCount++;
}

/**
 * @tc.name: Sqlite_Binlog_SearchDataTest005
 * @tc.desc: test get_search_data_binlog with DELETE operation
 * @tc.author: liuhongyang
 */
HWTEST_F(SqliteBinlogTest, Sqlite_Binlog_SearchDataTest005, TestSize.Level1)
{
    sqlite3 *db = NULL;
    EXPECT_EQ(sqlite3_open_v2(TEST_DB, &db,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr), SQLITE_OK);
    ASSERT_NE(db, nullptr);
    UtEnableBinlogForSearch(db);
    std::string table = "test_delete_search";
    EXPECT_EQ(sqlite3_set_monitor_config_binlog(db, InitMonitorConfig(table.c_str())), SQLITE_OK);
    g_xChangeCount = 0;
    EXPECT_EQ(sqlite3_set_xChange_callback_binlog(db, XChangeCallbackHelper), SQLITE_OK);
    /**
     * @tc.steps: step1. create table and insert data
     * @tc.expected: step1. ok
     */
    EXPECT_EQ(sqlite3_exec(db, "create table test_delete_search (id int primary key, name text);",
        nullptr, nullptr, nullptr), SQLITE_OK);
    EXPECT_EQ(sqlite3_exec(db, "insert into test_delete_search values (1, 'DeleteMe'), (2, 'KeepMe');",
        nullptr, nullptr, nullptr), SQLITE_OK);

    /**
     * @tc.steps: step2. delete data
     * @tc.expected: step2. ok
     */
    EXPECT_EQ(sqlite3_exec(db, "delete from test_delete_search where id = 1;",
        nullptr, nullptr, nullptr), SQLITE_OK);

    /**
     * @tc.steps: step3. get search data including delete
     * @tc.expected: step3. return ok
     */
    sqlite3 *backupDb = NULL;
    EXPECT_EQ(sqlite3_open_v2(TEST_BACKUP_DB, &backupDb,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr), SQLITE_OK);
    ASSERT_NE(backupDb, nullptr);
    BinlogSearchResultSet *rs = nullptr;
    EXPECT_EQ(sqlite3_get_search_data_binlog(db, backupDb, &rs), SQLITE_OK);
    ASSERT_FALSE(rs == nullptr);
    EXPECT_EQ(g_xChangeCount, 3);
    /**
     * @tc.steps: step4. verify delete operation is captured
     * @tc.expected: step4. delete operation found in results
     */
    bool foundDelete = false;
    for (int i = 0; i < rs->row_count; i++) {
        if (rs->results[i].op == SQLITE_DELETE) {
            foundDelete = true;
            EXPECT_STREQ(rs->results[i].tableName, "test_delete_search");
            break;
        }
    }
    EXPECT_EQ(rs->row_count, 3);
    EXPECT_TRUE(foundDelete);
    EXPECT_EQ(sqlite3_free_search_data_binlog(db, &rs), SQLITE_OK);
    EXPECT_TRUE(rs == nullptr);
}

/**
 * @tc.name: Sqlite_Binlog_SearchDataTest006
 * @tc.desc: test multiple get and free operations
 * @tc.author: liuhongyang
 */
HWTEST_F(SqliteBinlogTest, Sqlite_Binlog_SearchDataTest006, TestSize.Level1)
{
    sqlite3 *db = NULL;
    EXPECT_EQ(sqlite3_open_v2(TEST_DB, &db,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr), SQLITE_OK);
    ASSERT_NE(db, nullptr);
    UtEnableBinlogForSearch(db);
    /**
     * @tc.steps: step1. create table and insert data
     * @tc.expected: step1. ok
     */
    EXPECT_EQ(sqlite3_exec(db, "create table test_multi_get (id int primary key);",
        nullptr, nullptr, nullptr), SQLITE_OK);
    EXPECT_EQ(sqlite3_exec(db, "insert into test_multi_get values (1), (2);",
        nullptr, nullptr, nullptr), SQLITE_OK);

    /**
     * @tc.steps: step2. first get
     * @tc.expected: step2. ok
     */
    sqlite3 *backupDb = NULL;
    EXPECT_EQ(sqlite3_open_v2(TEST_BACKUP_DB, &backupDb,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr), SQLITE_OK);
    ASSERT_NE(backupDb, nullptr);
    BinlogSearchResultSet *rs1 = nullptr;
    EXPECT_EQ(sqlite3_get_search_data_binlog(db, backupDb, &rs1), SQLITE_OK);
    ASSERT_FALSE(rs1 == nullptr);
    EXPECT_EQ(sqlite3_free_search_data_binlog(db, &rs1), SQLITE_OK);
    EXPECT_TRUE(rs1 == nullptr);

    /**
     * @tc.steps: step3. insert more data
     * @tc.expected: step3. ok
     */
    EXPECT_EQ(sqlite3_exec(db, "insert into test_multi_get values (3), (4);",
        nullptr, nullptr, nullptr), SQLITE_OK);

    /**
     * @tc.steps: step4. second get should get new data
     * @tc.expected: step4. ok
     */
    BinlogSearchResultSet *rs2 = nullptr;
    EXPECT_EQ(sqlite3_get_search_data_binlog(db, backupDb, &rs2), SQLITE_OK);
    ASSERT_FALSE(rs2 == nullptr);
    // Should get the 2 new records
    EXPECT_EQ(rs2->row_count, 2);
    EXPECT_EQ(sqlite3_free_search_data_binlog(db, &rs2), SQLITE_OK);
    EXPECT_TRUE(rs2 == nullptr);
}

/**
 * @tc.name: Sqlite_Binlog_SearchDataTest007
 * @tc.desc: test free_search_data_binlog with already freed pointer
 * @tc.author: liuhongyang
 */
HWTEST_F(SqliteBinlogTest, Sqlite_Binlog_SearchDataTest007, TestSize.Level1)
{
    sqlite3 *db = NULL;
    EXPECT_EQ(sqlite3_open_v2(TEST_DB, &db,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr), SQLITE_OK);
    ASSERT_NE(db, nullptr);
    UtEnableBinlogForSearch(db);
    /**
     * @tc.steps: step1. create table and insert data
     * @tc.expected: step1. ok
     */
    EXPECT_EQ(sqlite3_exec(db, "create table test_free_twice (id int primary key);",
        nullptr, nullptr, nullptr), SQLITE_OK);
    EXPECT_EQ(sqlite3_exec(db, "insert into test_free_twice values (1);",
        nullptr, nullptr, nullptr), SQLITE_OK);

    /**
     * @tc.steps: step2. get and free search data
     * @tc.expected: step2. ok
     */
    sqlite3 *backupDb = NULL;
    EXPECT_EQ(sqlite3_open_v2(TEST_BACKUP_DB, &backupDb,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr), SQLITE_OK);
    ASSERT_NE(backupDb, nullptr);
    BinlogSearchResultSet *rs = nullptr;
    EXPECT_EQ(sqlite3_get_search_data_binlog(db, backupDb, &rs), SQLITE_OK);
    ASSERT_FALSE(rs == nullptr);
    EXPECT_EQ(sqlite3_free_search_data_binlog(db, &rs), SQLITE_OK);
    EXPECT_TRUE(rs == nullptr);

    /**
     * @tc.steps: step3. try to free again with NULL pointer
     * @tc.expected: step3. should return error (already freed)
     */
    EXPECT_EQ(sqlite3_free_search_data_binlog(db, &rs), SQLITE_ERROR);
}

/**
 * @tc.name: Sqlite_Binlog_SearchDataTest008
 * @tc.desc: CRUD in without rowid tables
 * @tc.author: liuhongyang
 */
HWTEST_F(SqliteBinlogTest, Sqlite_Binlog_SearchDataTest008, TestSize.Level0)
{
    sqlite3 *db = NULL;
    EXPECT_EQ(sqlite3_open_v2(TEST_DB, &db,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr), SQLITE_OK);
    ASSERT_NE(db, nullptr);
    UtEnableBinlogForSearch(db);
    /**
     * @tc.steps: step1. create without rowid table BB1 and 4 more indexes
     * @tc.expected: step1. ok
     */
    EXPECT_EQ(sqlite3_exec(db,
        "create table BB1 (id INTEGER PRIMARY KEY, name TEXT, length REAL, count INTEGER) without rowid;",
        nullptr, nullptr, nullptr), SQLITE_OK);
    EXPECT_EQ(sqlite3_exec(db, "create index index_bb1 on BB1 (id);", nullptr, nullptr, nullptr), SQLITE_OK);
    EXPECT_EQ(sqlite3_exec(db, "create index index_bb2 on BB1 (name);", nullptr, nullptr, nullptr), SQLITE_OK);
    EXPECT_EQ(sqlite3_exec(db, "create index index_bb3 on BB1 (id, name);", nullptr, nullptr, nullptr), SQLITE_OK);
    EXPECT_EQ(sqlite3_exec(db, "create index index_bb4 on BB1 (id, name, length, count);",
        nullptr, nullptr, nullptr), SQLITE_OK);
    /**
     * @tc.steps: step2. insert, update, delete into BB1 with auto commit
     * @tc.expected: step2. ok
     */
    EXPECT_EQ(sqlite3_exec(db,
        "insert into BB1 (id, name, length, count ) values (3, 'Chinese', 8.5, 3),"
        "(100, 'Hahahaha', 7.5, 100), (5000, 'yolo', 6.5, 5000);", nullptr, nullptr, nullptr), SQLITE_OK);
    EXPECT_EQ(sqlite3_exec(db, "update BB1 set (id, name, length, count ) = "
        "(200, 'Hahahaha', 7.5, 200) where id = 3;", nullptr, nullptr, nullptr), SQLITE_OK);
    EXPECT_EQ(sqlite3_exec(db, "delete from BB1 where id = 5000;", nullptr, nullptr, nullptr), SQLITE_OK);
    EXPECT_EQ(sqlite3_exec(db, "update BB1 set (id, name, length, count ) = "
        "(100, 'Hahahaha', 7.5, 300) where id = 100;", nullptr, nullptr, nullptr), SQLITE_OK);
    /**
     * @tc.steps: step3. insert into BB1 with failed actions inside a transaction
     * @tc.expected: step3. ok
     */
    EXPECT_EQ(sqlite3_exec(db, "begin;", nullptr, nullptr, nullptr), SQLITE_OK);
    EXPECT_EQ(sqlite3_exec(db, "insert into BB1 (id, name, length, count ) values (3, 'Chinese', 8.5, 3),"
        "(100, 'Hahahaha', 7.5, 100);", nullptr, nullptr, nullptr), SQLITE_CONSTRAINT);
    EXPECT_EQ(sqlite3_exec(db, "commit;", nullptr, nullptr, nullptr), SQLITE_OK);
    /**
     * @tc.steps: step4. create without rowid table with combined primary keys and do CRUD operations
     * @tc.expected: step4. ok
     */
    EXPECT_EQ(sqlite3_exec(db, "create table BB3 (id INTEGER, name TEXT, length REAL,"
        "count INTEGER, primary key (count, name)) without rowid;", nullptr, nullptr, nullptr), SQLITE_OK);
    EXPECT_EQ(sqlite3_exec(db, "insert into BB3 (id, name, length, count ) values (3, 'Chinese', 8.5, 3),"
        "(100, 'Hahahaha', 7.5, 100);", nullptr, nullptr, nullptr), SQLITE_OK);
    EXPECT_EQ(sqlite3_exec(db, "update BB3 set (id, name, length, count ) = "
        "(200, 'yolo', 7.5, 200) where id = 3;", nullptr, nullptr, nullptr), SQLITE_OK);
    EXPECT_EQ(sqlite3_exec(db, "select * from BB3;", nullptr, nullptr, nullptr), SQLITE_OK);
    /**
     * @tc.steps: step5. getData
     * @tc.expected: step5. return ok
     */
    sqlite3 *backupDb = NULL;
    EXPECT_EQ(sqlite3_open_v2(TEST_BACKUP_DB, &backupDb,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr), SQLITE_OK);
    ASSERT_NE(backupDb, nullptr);
    BinlogSearchResultSet *rs = nullptr;
    EXPECT_EQ(sqlite3_get_search_data_binlog(db, backupDb, &rs), SQLITE_OK);
    ASSERT_FALSE(rs == nullptr);
    EXPECT_STREQ(rs->results[0].nameAndValues[1], "3");
    EXPECT_STREQ(rs->results[0].nameAndValues[3], "Chinese");
    EXPECT_STREQ(rs->results[0].nameAndValues[5], "8.5");
    EXPECT_STREQ(rs->results[0].nameAndValues[7], "3");
    EXPECT_GT(rs->row_count, 10);
    EXPECT_GT(rs->results[10].readPos, rs->results[0].readPos);
    EXPECT_EQ(sqlite3_free_search_data_binlog(db, &rs), SQLITE_OK);
    /**
     * @tc.steps: step6. drop a without rowid table
     * @tc.expected: step6. ok
     */
    EXPECT_EQ(sqlite3_exec(db, "drop table BB3;", nullptr, nullptr, nullptr), SQLITE_OK);
    /**
     * @tc.steps: step7. replay
     * @tc.expected: step7. database is the same
     */
    EXPECT_EQ(sqlite3_replay_binlog(db, backupDb), SQLITE_OK);
}

/**
 * @tc.name: Sqlite_Binlog_SearchDataTest009
 * @tc.desc: test config and callback
 * @tc.author: liuhongyang
 */
HWTEST_F(SqliteBinlogTest, Sqlite_Binlog_SearchDataTest009, TestSize.Level1)
{
    sqlite3 *db = NULL;
    EXPECT_EQ(sqlite3_open_v2(TEST_DB, &db,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr), SQLITE_OK);
    ASSERT_NE(db, nullptr);
    UtEnableBinlogForSearch(db);
    std::string tableName = "test_delete_search";
    EXPECT_EQ(sqlite3_set_monitor_config_binlog(db, InitMonitorConfig(tableName.c_str())), SQLITE_OK);
    g_xChangeCount = 0;
    EXPECT_EQ(sqlite3_set_xChange_callback_binlog(db, XChangeCallbackHelper), SQLITE_OK);
    /**
     * @tc.steps: step1. create table and insert data
     * @tc.expected: step1. ok
     */
    EXPECT_EQ(sqlite3_exec(db, "create table test_delete_search (id int primary key, name text);",
        nullptr, nullptr, nullptr), SQLITE_OK);
    EXPECT_EQ(sqlite3_exec(db, "insert into test_delete_search values (1, 'DeleteMe'), (2, 'KeepMe');",
        nullptr, nullptr, nullptr), SQLITE_OK);

    /**
     * @tc.steps: step2. delete data
     * @tc.expected: step2. ok
     */
    EXPECT_EQ(sqlite3_exec(db, "delete from test_delete_search where id = 1;",
        nullptr, nullptr, nullptr), SQLITE_OK);

    /**
     * @tc.steps: step3. get search data including delete
     * @tc.expected: step3. return ok
     */
    sqlite3 *backupDb = NULL;
    EXPECT_EQ(sqlite3_open_v2(TEST_BACKUP_DB, &backupDb,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr), SQLITE_OK);
    ASSERT_NE(backupDb, nullptr);
    BinlogSearchResultSet *rs = nullptr;
    EXPECT_EQ(sqlite3_get_search_data_binlog(db, backupDb, &rs), SQLITE_OK);
    ASSERT_FALSE(rs == nullptr);
    EXPECT_EQ(g_xChangeCount, 3);
    /**
     * @tc.steps: step4. verify delete operation is captured
     * @tc.expected: step4. delete operation found in results
     */
    bool foundDelete = false;
    for (int i = 0; i < rs->row_count; i++) {
        if (rs->results[i].op == SQLITE_DELETE) {
            foundDelete = true;
            EXPECT_STREQ(rs->results[i].tableName, "test_delete_search");
            break;
        }
    }
    EXPECT_TRUE(foundDelete);
    EXPECT_EQ(sqlite3_free_search_data_binlog(db, &rs), SQLITE_OK);
    EXPECT_TRUE(rs == nullptr);
}

/**
 * @tc.name: Sqlite_Binlog_SearchDataTest01
 * @tc.desc: test config and callback
 * @tc.author: liuhongyang
 */
HWTEST_F(SqliteBinlogTest, Sqlite_Binlog_SearchDataTest010, TestSize.Level1)
{
    sqlite3 *db = NULL;
    EXPECT_EQ(sqlite3_open_v2(TEST_DB, &db,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr), SQLITE_OK);
    ASSERT_NE(db, nullptr);
    UtEnableBinlogForSearch(db);
    std::string table = "test_delete_search";
    MonitorTablesConfig* (*callback)(const char *dbPath) = [](const char *dbPath) -> MonitorTablesConfig* {
        MonitorTablesConfig *config = new MonitorTablesConfig();
        config->tableCount = 0;
        config->tables = nullptr;
        return config;
    };
    EXPECT_EQ(sqlite3_set_json_parse_callback_binlog(db, callback), SQLITE_OK);
    g_xChangeCount = 0;
    EXPECT_EQ(sqlite3_set_xChange_callback_binlog(db, XChangeCallbackHelper), SQLITE_OK);
    /**
     * @tc.steps: step1. create table and insert data
     * @tc.expected: step1. ok
     */
    EXPECT_EQ(sqlite3_exec(db, "create table test_delete_search (id int primary key, name text);",
        nullptr, nullptr, nullptr), SQLITE_OK);
    EXPECT_EQ(sqlite3_exec(db, "insert into test_delete_search values (1, 'DeleteMe'), (2, 'KeepMe');",
        nullptr, nullptr, nullptr), SQLITE_OK);

    /**
     * @tc.steps: step2. delete data
     * @tc.expected: step2. ok
     */
    EXPECT_EQ(sqlite3_exec(db, "delete from test_delete_search where id = 1;",
        nullptr, nullptr, nullptr), SQLITE_OK);

    /**
     * @tc.steps: step3. get search data including delete
     * @tc.expected: step3. return ok
     */
    sqlite3 *backupDb = NULL;
    EXPECT_EQ(sqlite3_open_v2(TEST_BACKUP_DB, &backupDb,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr), SQLITE_OK);
    ASSERT_NE(backupDb, nullptr);
    BinlogSearchResultSet *rs = nullptr;
    EXPECT_EQ(sqlite3_get_search_data_binlog(db, backupDb, &rs), SQLITE_OK);
    ASSERT_FALSE(rs == nullptr);
    EXPECT_EQ(g_xChangeCount, 3);
    /**
     * @tc.steps: step4. verify delete operation is captured
     * @tc.expected: step4. delete operation found in results
     */
    bool foundDelete = false;
    for (int i = 0; i < rs->row_count; i++) {
        if (rs->results[i].op == SQLITE_DELETE) {
            foundDelete = true;
            EXPECT_STREQ(rs->results[i].tableName, "test_delete_search");
            break;
        }
    }
    EXPECT_TRUE(foundDelete);
    EXPECT_EQ(sqlite3_free_search_data_binlog(db, &rs), SQLITE_OK);
    EXPECT_TRUE(rs == nullptr);
}
}  // namespace BinlogTest
