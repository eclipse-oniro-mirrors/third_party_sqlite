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

#define TEST_DIR "./sqlitecompresstest"
#define TEST_DB (TEST_DIR "/test.db")
#define TEST_PRESET_TABLE_COUNT 20
#define TEST_PRESET_DATA_COUNT 100
#define TEST_COMPRESS_META_TABLE (2)

namespace Test {
class SQLiteCompressTest : public testing::Test {
public:
    static void SetUpTestCase(void);
    static void TearDownTestCase(void);
    void SetUp();
    void TearDown();

    static void UtSqliteLogPrint(const void *data, int err, const char *msg);
    static void UtCheckDb(const std::string &dbPath, int tableCount = TEST_COMPRESS_META_TABLE);
    static void UtBackupDatabase(sqlite3 *srcDb, sqlite3 *destDb);
    static void UtBackupCompressDatabase(sqlite3 *srcDb, sqlite3 *destDb);

    static sqlite3 *db;
    static const std::string &UT_DDL_CREATE_DEMO;
    static const std::string &UT_DML_INSERT_DEMO;
    static const std::string &UT_SQL_SELECT_META;
    static const std::string &UT_DDL_CREATE_PHONE;
    static const std::string &UT_PHONE_DESC;
};

sqlite3 *SQLiteCompressTest::db = nullptr;
const std::string &SQLiteCompressTest::UT_DDL_CREATE_DEMO = "CREATE TABLE demo(id INTEGER PRIMARY KEY, name TEXT);";
const std::string &SQLiteCompressTest::UT_DML_INSERT_DEMO = "INSERT INTO demo(id, name) VALUES(110, 'Call 110!');";
const std::string &SQLiteCompressTest::UT_SQL_SELECT_META = "SELECT COUNT(1) FROM sqlite_master;";
const std::string &SQLiteCompressTest::UT_DDL_CREATE_PHONE =
    "CREATE TABLE IF NOT EXISTS phone(id INTEGER PRIMARY KEY, name TEXT, brand TEXT, price REAL, type int, desc TEXT);";
const std::string &SQLiteCompressTest::UT_PHONE_DESC = "The phone is easy to use and popular. 優點(繁体)系统(简体)稳定";

void SQLiteCompressTest::UtSqliteLogPrint(const void *data, int err, const char *msg)
{
    std::cout << "SQLiteCompressTest xLog err:" << err << ", msg:" << msg << std::endl;
}

void SQLiteCompressTest::UtCheckDb(const std::string &dbPath, int tableCount)
{
    sqlite3 *tmpDb = nullptr;
    EXPECT_EQ(sqlite3_open_v2(dbPath.c_str(), &tmpDb, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr),
        SQLITE_OK) << ", compress db:" << dbPath.c_str();
    sqlite3_stmt *sqlStmt = nullptr;
    EXPECT_EQ(sqlite3_prepare_v2(tmpDb, UT_SQL_SELECT_META.c_str(), -1, &sqlStmt, nullptr),
        SQLITE_OK) << ", compress db:" << dbPath.c_str();
    EXPECT_EQ(sqlite3_step(sqlStmt), SQLITE_ROW) << ", compress db:" << dbPath.c_str();
    int count = sqlite3_column_int(sqlStmt, 0);
    EXPECT_EQ(count, tableCount) << ", compress db:" << dbPath.c_str();
    sqlite3_finalize(sqlStmt);
    sqlite3_close_v2(tmpDb);
}

void SQLiteCompressTest::UtBackupDatabase(sqlite3 *srcDb, sqlite3 *destDb)
{
    sqlite3_backup *back = nullptr;
    back = sqlite3_backup_init(destDb, "main", srcDb, "main");
    EXPECT_TRUE(back != nullptr);
    EXPECT_EQ(sqlite3_backup_step(back, -1), SQLITE_DONE);
    sqlite3_backup_finish(back);
}

void SQLiteCompressTest::SetUpTestCase(void)
{
    Common::RemoveDir(TEST_DIR);
    Common::MakeDir(TEST_DIR);
    EXPECT_EQ(sqlite3_register_cksumvfs(NULL), SQLITE_OK);
}

void SQLiteCompressTest::TearDownTestCase(void)
{
    EXPECT_EQ(sqlite3_unregister_cksumvfs(), SQLITE_OK);
}

void SQLiteCompressTest::SetUp(void)
{
    std::string command = "rm -rf ";
    command += TEST_DIR "/*";
    system(command.c_str());
    sqlite3_config(SQLITE_CONFIG_LOG, &SQLiteCompressTest::UtSqliteLogPrint, NULL);
    EXPECT_EQ(sqlite3_open(TEST_DB, &db), SQLITE_OK);
    EXPECT_EQ(sqlite3_exec(db, UT_DDL_CREATE_PHONE.c_str(), nullptr, nullptr, nullptr), SQLITE_OK);
    for (int i = 0; i < TEST_PRESET_TABLE_COUNT; i++) {
        std::string ddl = "CREATE TABLE IF NOT EXISTS test";
        ddl += std::to_string(i + 1);
        ddl += "(id INTEGER PRIMARY KEY, field1 INTEGER, field2 REAL, field3 TEXT, field4 BLOB, field5 TEXT);";
        EXPECT_EQ(sqlite3_exec(db, ddl.c_str(), nullptr, nullptr, nullptr), SQLITE_OK);
    }
    std::vector<std::string> phoneList = {"Huawei", "Samsung", "Apple", "Xiaomi", "Oppo", "Vivo", "Realme"};
    for (int i = 0; i < TEST_PRESET_DATA_COUNT; i++) {
        std::string dml = "INSERT INTO phone(id, name, brand, price, type, desc) VALUES(";
        dml += std::to_string(i + 1) + ",'";
        dml += std::to_string(i + 1) + " Martin''s Phone','";
        dml += std::to_string(i + 1) + phoneList[i%phoneList.size()] + "',";
        dml += std::to_string(i + 1.0) + ",";
        dml += std::to_string(i + 1) + ",'" + UT_PHONE_DESC + UT_PHONE_DESC + UT_PHONE_DESC + UT_PHONE_DESC + "');";
        EXPECT_EQ(sqlite3_exec(db, dml.c_str(), nullptr, nullptr, nullptr), SQLITE_OK) << sqlite3_errmsg(db);
    }
    sqlite3_close(db);
    EXPECT_EQ(sqlite3_open(TEST_DB, &db), SQLITE_OK);
}

void SQLiteCompressTest::TearDown(void)
{
    sqlite3_close(db);
    db = nullptr;
    sqlite3_config(SQLITE_CONFIG_LOG, NULL, NULL);
}

/**
 * @tc.name: CompressTest001
 * @tc.desc: Test to enable page compression on brand new db.
 * @tc.type: FUNC
 */
HWTEST_F(SQLiteCompressTest, CompressTest001, TestSize.Level0)
{
    /**
     * @tc.steps: step1. Create a brand new db while page compression enabled with sqlite3_open_v2
     * @tc.expected: step1. Execute successfully
     */
    std::string dbPath1 = TEST_DIR "/compresstest0010.db";
    sqlite3 *compDb = nullptr;
    EXPECT_EQ(sqlite3_open_v2(dbPath1.c_str(), &compDb, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, "compressvfs"),
        SQLITE_OK);
    EXPECT_EQ(sqlite3_exec(compDb, UT_DDL_CREATE_DEMO.c_str(), nullptr, nullptr, nullptr), SQLITE_OK);
    EXPECT_EQ(sqlite3_exec(compDb, UT_DML_INSERT_DEMO.c_str(), nullptr, nullptr, nullptr), SQLITE_OK);
    sqlite3_close_v2(compDb);
    compDb = nullptr;
    UtCheckDb(dbPath1);
    /**
     * @tc.steps: step2. Create a brand new db while page compression enabled through uri
     * @tc.expected: step2. Execute successfully
     */
    std::string dbFileUri = "file:";
    dbFileUri += TEST_DIR "/compresstest0011.db";
    dbFileUri += "?vfs=compressvfs";
    EXPECT_EQ(sqlite3_open(dbFileUri.c_str(), &compDb), SQLITE_OK);
    EXPECT_EQ(sqlite3_exec(compDb, UT_DDL_CREATE_DEMO.c_str(), nullptr, nullptr, nullptr), SQLITE_OK);
    EXPECT_EQ(sqlite3_exec(compDb, UT_DML_INSERT_DEMO.c_str(), nullptr, nullptr, nullptr), SQLITE_OK);
    sqlite3_close(compDb);
    compDb = nullptr;
    UtCheckDb(TEST_DIR "/compresstest0011.db");
}

/**
 * @tc.name: CompressTest002
 * @tc.desc: Test to try enable page compression on none compress db
 * @tc.type: FUNC
 */
HWTEST_F(SQLiteCompressTest, CompressTest002, TestSize.Level0)
{
    sqlite3 *compDb = nullptr;
    EXPECT_EQ(sqlite3_open_v2(TEST_DB, &compDb, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, "compressvfs"),
        SQLITE_WARNING);
    EXPECT_EQ(sqlite3_extended_errcode(compDb), SQLITE_WARNING_NOTCOMPRESSDB);
    sqlite3_close_v2(compDb);

    EXPECT_EQ(sqlite3_open_v2(TEST_DB, &compDb, SQLITE_OPEN_READONLY, "compressvfs"), SQLITE_WARNING);
    EXPECT_EQ(sqlite3_extended_errcode(compDb), SQLITE_WARNING_NOTCOMPRESSDB);
    sqlite3_close_v2(compDb);

    std::string dbFileUri = "file:";
    dbFileUri += TEST_DB;
    dbFileUri += "?vfs=compressvfs";
    EXPECT_EQ(sqlite3_open(dbFileUri.c_str(), &compDb), SQLITE_WARNING);
    EXPECT_EQ(sqlite3_extended_errcode(compDb), SQLITE_WARNING_NOTCOMPRESSDB);
    sqlite3_close_v2(compDb);
}

/**
 * @tc.name: CompressTest003
 * @tc.desc: Test to backup db and enable page compression on dest db
 * @tc.type: FUNC
 */
HWTEST_F(SQLiteCompressTest, CompressTest003, TestSize.Level0)
{
    /**
     * @tc.steps: step1. Compress db using Backup function:sqlite3_backup_step
     * @tc.expected: step1. Execute successfully
     */
    std::string dbPath1 = TEST_DIR "/compresstest0030.db";
    sqlite3 *compDb = nullptr;
    EXPECT_EQ(sqlite3_open_v2(dbPath1.c_str(), &compDb, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, "compressvfs"),
        SQLITE_OK);
    UtBackupDatabase(db, compDb);
    sqlite3_close_v2(compDb);
    UtCheckDb(dbPath1);

    /**
     * @tc.steps: step2. Compress db using new Backup function:sqlite3_compressdb_backup
     * @tc.expected: step2. Execute successfully
     */
    EXPECT_EQ(sqlite3_open_v2(dbPath1.c_str(), &compDb, SQLITE_OPEN_READWRITE, nullptr), SQLITE_OK);
    std::string dbPath2 = TEST_DIR "/restore0031.db";
    EXPECT_EQ(sqlite3_compressdb_backup(compDb, dbPath2.c_str()), SQLITE_DONE);
    sqlite3_close_v2(compDb);
    UtCheckDb(dbPath2, TEST_PRESET_TABLE_COUNT + 1);
}

/**
 * @tc.name: CompressTest004
 * @tc.desc: Test to create new db and enable page compression
 * @tc.type: FUNC
 */
HWTEST_F(SQLiteCompressTest, CompressTest004, TestSize.Level0)
{
    /**
     * @tc.steps: step1. Create a brand new db while page compression enabled with sqlite3_open_v2
     * @tc.expected: step1. Execute successfully
     */
    std::string dbPath1 = TEST_DIR "/compresstest0040.db";
    sqlite3 *compDb = nullptr;
    EXPECT_EQ(sqlite3_open_v2(dbPath1.c_str(), &compDb, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, "compressvfs"),
        SQLITE_OK);
    EXPECT_EQ(sqlite3_exec(compDb, UT_DDL_CREATE_DEMO.c_str(), nullptr, nullptr, nullptr), SQLITE_OK);
    EXPECT_EQ(sqlite3_exec(compDb, UT_DML_INSERT_DEMO.c_str(), nullptr, nullptr, nullptr), SQLITE_OK);
    /**
     * @tc.steps: step2. Set meta double write enable through Pragma statement
     * @tc.expected: step2. Execute successfully
     */
    EXPECT_EQ(sqlite3_exec(compDb, "PRAGMA meta_double_write=enabled;", nullptr, nullptr, nullptr), SQLITE_OK);
    /**
     * @tc.steps: step3. Set journal mode as WAL through Pragma statement
     * @tc.expected: step3. Execute successfully
     */
    EXPECT_EQ(sqlite3_exec(compDb, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr), SQLITE_OK);
    EXPECT_EQ(sqlite3_exec(compDb, UT_DDL_CREATE_PHONE.c_str(), nullptr, nullptr, nullptr), SQLITE_OK);
    /**
     * @tc.steps: step4. Set WAL file persist
     * @tc.expected: step4. Execute successfully
     */
    int code = 1;
    EXPECT_EQ(sqlite3_file_control(compDb, "main", SQLITE_FCNTL_PERSIST_WAL, &code), SQLITE_OK);
    sqlite3_close_v2(compDb);
    /**
     * @tc.steps: step5. Check result, files should exist
     * @tc.expected: step4. Execute successfully
     */
    std::string wal = dbPath1 + "-walcompress";
    std::string shm = dbPath1 + "-shmcompress";
    std::string dwr = dbPath1 + "-dwr";
    EXPECT_TRUE(Common::IsFileExist(wal.c_str()));
    EXPECT_TRUE(Common::IsFileExist(shm.c_str()));
    EXPECT_TRUE(Common::IsFileExist(dwr.c_str()));
}

/**
 * @tc.name: CompressTest005
 * @tc.desc: Test to create new db and get db/wal/journal filename
 * @tc.type: FUNC
 */
HWTEST_F(SQLiteCompressTest, CompressTest005, TestSize.Level0)
{
    /**
     * @tc.steps: step1. Create a brand new db while page compression enabled with sqlite3_open_v2
     * @tc.expected: step1. Execute successfully
     */
    std::string dbPath1 = TEST_DIR "/compresstest0050.db";
    sqlite3 *compDb = nullptr;
    EXPECT_EQ(sqlite3_open_v2(dbPath1.c_str(), &compDb, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, "compressvfs"),
        SQLITE_OK);
    EXPECT_EQ(sqlite3_exec(compDb, UT_DDL_CREATE_DEMO.c_str(), nullptr, nullptr, nullptr), SQLITE_OK);
    EXPECT_EQ(sqlite3_exec(compDb, UT_DML_INSERT_DEMO.c_str(), nullptr, nullptr, nullptr), SQLITE_OK);
    EXPECT_EQ(sqlite3_exec(compDb, "PRAGMA meta_double_write=enabled;", nullptr, nullptr, nullptr), SQLITE_OK);
    EXPECT_EQ(sqlite3_exec(compDb, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr), SQLITE_OK);
    /**
     * @tc.steps: step2. Try to get db/wal/journal filename
     * @tc.expected: step2. Execute successfully
     */
    const char *db = sqlite3_db_filename(compDb, "main");
    const char *wal = sqlite3_filename_wal(db);
    const char *journal = sqlite3_filename_journal(db);
    std::string str = db;
    int n = str.find("/compresstest0050.db");
    EXPECT_TRUE(n != string::npos);
    EXPECT_TRUE(n==(str.size() - strlen("/compresstest0050.db")));
    str = wal;
    n = str.find("/compresstest0050.db-walcompress");
    EXPECT_TRUE(n != string::npos);
    EXPECT_TRUE(n==(str.size() - strlen("/compresstest0050.db-walcompress")));
    str = journal;
    n = str.find("/compresstest0050.db-journalcompress");
    EXPECT_TRUE(n != string::npos);
    EXPECT_TRUE(n==(str.size() - strlen("/compresstest0050.db-journalcompress")));
    sqlite3_close_v2(compDb);
}

/**
 * @tc.name: CompressTest006
 * @tc.desc: Test to create new db and open multi sqlite3 handler at the same time
 * @tc.type: FUNC
 */
HWTEST_F(SQLiteCompressTest, CompressTest006, TestSize.Level0)
{
    /**
     * @tc.steps: step1. Create a brand new db while page compression enabled with sqlite3_open_v2
     * @tc.expected: step1. Execute successfully
     */
    std::string dbPath1 = TEST_DIR "/compresstest0060.db";
    sqlite3 *compDb = nullptr;
    EXPECT_EQ(sqlite3_open_v2(dbPath1.c_str(), &compDb, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, "compressvfs"),
        SQLITE_OK);
    EXPECT_EQ(sqlite3_exec(compDb, UT_DDL_CREATE_DEMO.c_str(), nullptr, nullptr, nullptr), SQLITE_OK);
    EXPECT_EQ(sqlite3_exec(compDb, UT_DML_INSERT_DEMO.c_str(), nullptr, nullptr, nullptr), SQLITE_OK);
    EXPECT_EQ(sqlite3_exec(compDb, "PRAGMA meta_double_write=enabled;", nullptr, nullptr, nullptr), SQLITE_OK);
    EXPECT_EQ(sqlite3_exec(compDb, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr), SQLITE_OK);
    /**
     * @tc.steps: step2. Open multi sqlite3 which enable page compression at the same time
     * @tc.expected: step2. Execute successfully
     */
    sqlite3 *compDb1 = nullptr;
    sqlite3 *compDb2 = nullptr;
    EXPECT_EQ(sqlite3_open_v2(dbPath1.c_str(), &compDb1, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, "compressvfs"),
        SQLITE_OK);
    EXPECT_EQ(sqlite3_open_v2(dbPath1.c_str(), &compDb2, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, "compressvfs"),
        SQLITE_OK);
    sqlite3_close_v2(compDb2);
    sqlite3_close_v2(compDb1);
    sqlite3_close_v2(compDb);
}

}  // namespace Test
