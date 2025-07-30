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
#include <fstream>
#include <string>
#include <unistd.h>
#include <vector>

#include "sqlite3sym.h"

using namespace testing::ext;

#define TEST_DIR "./sqlitetest"
#define TEST_DB (TEST_DIR "/test.db")
#define TEST_DATA_COUNT 1000
#define TEST_DATA_REAL 16.1

static void UtSqliteLogPrint(const void *data, int err, const char *msg)
{
    std::cout << "LibSQLiteTest SQLite xLog err:" << err << ", msg:" << msg << std::endl;
}

static void UtPresetDb(const char *dbFile)
{
    /**
     * @tc.steps: step1. Prepare db used to simulate corrupted
     * @tc.expected: step1. Execute successfully
     */
    sqlite3 *db = NULL;
    std::string dbFileUri = "file:";
    dbFileUri += dbFile;
    dbFileUri += "?vfs=cksmvfs";
    EXPECT_EQ(sqlite3_open(dbFileUri.c_str(), &db), SQLITE_OK);
    /**
     * @tc.steps: step1. Enable cksumvfs using PRAGMA checksum_persist_enable,
     * @tc.expected: step1. Execute successfully
     */
    static const char *UT_PRAGMA_CKSUM_ENABLE = "PRAGMA checksum_persist_enable=ON";
    EXPECT_EQ(sqlite3_exec(db, UT_PRAGMA_CKSUM_ENABLE, NULL, NULL, NULL), SQLITE_OK);
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
    static const char *UT_SQL_INSERT_DATA = "INSERT INTO salary(entryId, entryName, salary, class) VALUES(?,?,?,?);";
    sqlite3_stmt *insertStmt = NULL;
    EXPECT_EQ(sqlite3_prepare_v2(db, UT_SQL_INSERT_DATA, -1, &insertStmt, NULL), SQLITE_OK);
    for (int i = 0; i < TEST_DATA_COUNT; i++) {
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

class LibSQLiteTest : public testing::Test {
public:
    static void SetUpTestCase(void);
    static void TearDownTestCase(void);
    void SetUp();
    void TearDown();
};

void LibSQLiteTest::SetUpTestCase(void)
{
    // permission 0770
    mkdir(TEST_DIR, 0770);
}

void LibSQLiteTest::TearDownTestCase(void)
{
}

void LibSQLiteTest::SetUp(void)
{
    unlink(TEST_DB);
    sqlite3_config(SQLITE_CONFIG_LOG, &UtSqliteLogPrint, NULL);
    EXPECT_EQ(sqlite3_register_cksumvfs(NULL), SQLITE_OK);
    UtPresetDb(TEST_DB);
}

void LibSQLiteTest::TearDown(void)
{
    EXPECT_EQ(sqlite3_unregister_cksumvfs(), SQLITE_OK);
    sqlite3_config(SQLITE_CONFIG_LOG, NULL, NULL);
}

static void UtCorruptDb(const std::string &dbFile, size_t offset, size_t len, unsigned char ch)
{
    std::fstream f;
    f.open(dbFile.c_str());
    f.seekp(offset, std::ios::beg);
    std::vector<char> buf(len, ch);
    f.write(buf.data(), len);
    f.close();
}

static int UtSqliteExecCallback(void *data, int argc, char **argv, char **azColName)
{
    std::cout << (const char *)data << " result:" << std::endl;
    for (int i = 0; i < argc; i++) {
        std::cout << azColName[i] << " = " << (argv[i] ? argv[i] : "NULL") << std::endl;
    }
    std::cout << std::endl;
    return 0;
}

/**
 * @tc.name: Lib_SQLite_Test_001
 * @tc.desc: Test to enable cksumvfs.
 * @tc.type: FUNC
 */
HWTEST_F(LibSQLiteTest, Lib_SQLite_Test_001, TestSize.Level0)
{
    /**
     * @tc.steps: step1. Corrupt db file, reopen and make a quick check
     * @tc.expected: step1. Return SQLITE_IOERR, as meta table info corrupted
     */
    sqlite3 *db = NULL;
    UtCorruptDb(TEST_DB, 3 * 4096 + 1000, 1, 0xFF); // 3 * 4096 + 1000 is the target page's position in file
    std::string dbFileUri = "file:";
    dbFileUri += TEST_DB;
    dbFileUri += "?vfs=cksmvfs";
    EXPECT_EQ(sqlite3_open(dbFileUri.c_str(), &db), SQLITE_OK);
    static const char *UT_SQL_SELECT_TABLE = "SELECT COUNT(*) FROM salary WHERE entryId=3;";
    EXPECT_EQ(sqlite3_exec(db, UT_SQL_SELECT_TABLE, NULL, NULL, NULL), SQLITE_OK);
    /**
     * @tc.steps: step2. Disable checksum_verification, re-quary the table
     * @tc.expected: step2. Execute successfully
     */
    static const char *UT_PRAGMA_DISABLE_CKSUM = "PRAGMA checksum_verification=OFF;";
    EXPECT_EQ(sqlite3_exec(db, UT_PRAGMA_DISABLE_CKSUM, UtSqliteExecCallback, (void *)"PRAGMA", NULL), SQLITE_OK);
    EXPECT_EQ(sqlite3_exec(db, UT_SQL_SELECT_TABLE, NULL, NULL, NULL), SQLITE_OK);
    /**
     * @tc.steps: step2. Disable checksum_verification, re-quary the table
     * @tc.expected: step2. Return SQLITE_OK, as page already in the cache
     */
    static const char *UT_PRAGMA_ENABLE_CKSUM = "PRAGMA checksum_verification=ON;";
    EXPECT_EQ(sqlite3_exec(db, UT_PRAGMA_ENABLE_CKSUM, UtSqliteExecCallback, (void *)"PRAGMA", NULL), SQLITE_OK);
    EXPECT_EQ(sqlite3_exec(db, UT_SQL_SELECT_TABLE, NULL, NULL, NULL), SQLITE_OK);
    sqlite3_close(db);
}

/**
 * @tc.name: Lib_SQLite_Test_002
 * @tc.desc: Test to enable and disable checksum_verification.
 * @tc.type: FUNC
 */
HWTEST_F(LibSQLiteTest, Lib_SQLite_Test_002, TestSize.Level0)
{
    /**
     * @tc.steps: step1. Corrupt db file, reopen and make a quick check
     * @tc.expected: step1. Return SQLITE_IOERR, as meta table info corrupted
     */
    sqlite3 *db = NULL;
    UtCorruptDb(TEST_DB, 3 * 4096 + 1000, 1, 0xFF); // 3 * 4096 + 1000 is the target page's position in file
    std::string dbFileUri = "file:";
    dbFileUri += TEST_DB;
    dbFileUri += "?vfs=cksmvfs";
    EXPECT_EQ(sqlite3_open(dbFileUri.c_str(), &db), SQLITE_OK);
    static const char *UT_SQL_SELECT_TABLE_1 = "SELECT COUNT(*) FROM salary WHERE entryId=3;";
    EXPECT_EQ(sqlite3_exec(db, UT_SQL_SELECT_TABLE_1, NULL, NULL, NULL), SQLITE_OK);
    /**
     * @tc.steps: step2. Disable checksum_verification, re-check the table
     * @tc.expected: step2. Execute successfully
     */
    static const char *UT_PRAGMA_DISABLE_CKSUM = "PRAGMA checksum_verification=OFF;";
    EXPECT_EQ(sqlite3_exec(db, UT_PRAGMA_DISABLE_CKSUM, UtSqliteExecCallback, (void *)"PRAGMA", NULL), SQLITE_OK);
    EXPECT_EQ(sqlite3_exec(db, UT_SQL_SELECT_TABLE_1, NULL, NULL, NULL), SQLITE_OK);
    /**
     * @tc.steps: step3. Enable checksum_verification again, re-quary the user table
     * @tc.expected: step3. Return SQLITE_OK, as page already in the cache
     */
    UtCorruptDb(TEST_DB, 7 * 4096 + 1000, 1, 0xFF); // 7 * 4096 + 1000 is the target page's position in file
    static const char *UT_SQL_SELECT_TABLE_2 = "SELECT COUNT(*) FROM salary WHERE entryId=500;";
    static const char *UT_PRAGMA_ENABLE_CKSUM = "PRAGMA checksum_verification=ON;";
    EXPECT_EQ(sqlite3_exec(db, UT_PRAGMA_ENABLE_CKSUM, UtSqliteExecCallback, (void *)"PRAGMA", NULL), SQLITE_OK);
    EXPECT_EQ(sqlite3_exec(db, UT_SQL_SELECT_TABLE_2, NULL, NULL, NULL), SQLITE_OK);
    sqlite3_close(db);
}
