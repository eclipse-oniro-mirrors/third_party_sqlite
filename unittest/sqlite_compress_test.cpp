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
    static bool IsSupportPageCompress(void);
    static void UtPresetDb(const std::string &dbFile, const std::string &vfsOption);
    static int UtQueryPresetDbResult(void *data, int argc, char **argv, char **azColName);
    static void UtCheckPresetDb(const std::string &dbPath, const std::string &vfsOption);

    static sqlite3 *db_;
    static int resCnt_;
    static const std::string &UT_DDL_CREATE_DEMO;
    static const std::string &UT_DML_INSERT_DEMO;
    static const std::string &UT_SQL_SELECT_META;
    static const std::string &UT_DDL_CREATE_PHONE;
    static const std::string &UT_PHONE_DESC;
};

sqlite3 *SQLiteCompressTest::db_ = nullptr;
int SQLiteCompressTest::resCnt_ = 0;
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

bool SQLiteCompressTest::IsSupportPageCompress(void)
{
#ifdef SQLITE_SUPPORT_PAGE_COMPRESS_TEST
    return true;
#else
    return false;
#endif
}

void SQLiteCompressTest::UtPresetDb(const std::string &dbFile, const std::string &vfsOption)
{
    sqlite3 *db = NULL;
    EXPECT_EQ(sqlite3_open_v2(dbFile.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
        (vfsOption.empty()? nullptr : vfsOption.c_str())), SQLITE_OK);
    if (vfsOption == "cksmvfs") {
        EXPECT_EQ(sqlite3_exec(db, "PRAGMA checksum_persist_enable=ON;", NULL, NULL, NULL), SQLITE_OK);
    }
    EXPECT_EQ(sqlite3_exec(db, "PRAGMA meta_double_write=enabled;", NULL, NULL, NULL), SQLITE_OK);
    EXPECT_EQ(sqlite3_exec(db, "PRAGMA journal_mode=WAL;", NULL, NULL, NULL), SQLITE_OK);
    static const char *UT_DDL_CREATE_TABLE = "CREATE TABLE salary"
        "(entryId INTEGER PRIMARY KEY, entryName Text, salary REAL, class INTEGER);";
    EXPECT_EQ(sqlite3_exec(db, UT_DDL_CREATE_TABLE, NULL, NULL, NULL), SQLITE_OK);
    std::string tmp = "CREATE TABLE test";
    for (size_t i = 0; i < 10; i++) {  // 10 means the count of tables
        std::string ddl = "CREATE TABLE test";
        ddl += std::to_string(i);
        ddl += "(entryId INTEGER PRIMARY KEY, entryName Text, salary REAL, class INTEGER);";
        EXPECT_EQ(sqlite3_exec(db, ddl.c_str(), NULL, NULL, NULL), SQLITE_OK);
    }
    static const char *UT_SQL_INSERT_DATA = "INSERT INTO salary(entryId, entryName, salary, class) VALUES(?,?,?,?);";
    sqlite3_stmt *insertStmt = NULL;
    EXPECT_EQ(sqlite3_prepare_v2(db, UT_SQL_INSERT_DATA, -1, &insertStmt, NULL), SQLITE_OK);
    for (int i = 0; i < 1000; i++) {  // 1000 means the total number of insert data
        sqlite3_bind_int(insertStmt, 1, i + 1);  // 1 is the seq number of 1st field
        sqlite3_bind_text(insertStmt, 2, "salary-entry-name", -1, SQLITE_STATIC);  // 2 is the seq number of 2nd field
        sqlite3_bind_double(insertStmt, 3, i);  // 3 is the seq number of 3rd field
        sqlite3_bind_int(insertStmt, 4, i + 1);  // 4 is the seq number of 4th field
        EXPECT_EQ(sqlite3_step(insertStmt), SQLITE_DONE);
        sqlite3_reset(insertStmt);
    }
    sqlite3_finalize(insertStmt);
    sqlite3_close(db);
}

int SQLiteCompressTest::UtQueryPresetDbResult(void *data, int argc, char **argv, char **azColName)
{
    int type = *static_cast<int *>(data);
    EXPECT_EQ(argc, 1);
    if (type==0) { // Check tables
        std::string tableName = argv[0] ? argv[0] : nullptr;
        std::string expectTables[] = { "salary", "test0", "test1", "test2", "test3", "test4",
            "test5", "test6", "test7", "test8", "test9" };
        bool isExist = false;
        for (size_t i = 0; i < 11; i++) {  // 11 is the number of array:expectTables
            if (expectTables[i] == tableName) {
                isExist = true;
                break;
            }
        }
        EXPECT_TRUE(isExist) << ", tableName:" << tableName;
        return SQLITE_OK;
    }

    resCnt_++;
    int entryId = atoi(argv[0]);
    EXPECT_TRUE(entryId > 0 && entryId < 1001) << ", entryId:" << entryId;  // 1001 is the max id of entryId
    return SQLITE_OK;
}

void SQLiteCompressTest::UtCheckPresetDb(const std::string &dbFile, const std::string &vfsOption)
{
    sqlite3 *db = NULL;
    EXPECT_EQ(sqlite3_open_v2(dbFile.c_str(), &db, SQLITE_OPEN_READONLY,
        (vfsOption.empty()? nullptr : vfsOption.c_str())), SQLITE_OK);
    static const char *UT_SELECT_ALL_TABLES = "SELECT tbl_name FROM sqlite_master;";
    int type = 0;
    EXPECT_EQ(sqlite3_exec(db, UT_SELECT_ALL_TABLES, &UtQueryPresetDbResult, &type, nullptr), SQLITE_OK);
    type = 1;  // 1 means query table: salary
    resCnt_ = 0;
    static const char *UT_SELECT_ALL_FROM_SALARY = "SELECT entryId FROM salary;";
    EXPECT_EQ(sqlite3_exec(db, UT_SELECT_ALL_FROM_SALARY, &UtQueryPresetDbResult, &type, nullptr), SQLITE_OK);
    EXPECT_EQ(resCnt_, 1000);
    sqlite3_close(db);
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
    EXPECT_EQ(sqlite3_open(TEST_DB, &db_), SQLITE_OK);
    EXPECT_EQ(sqlite3_exec(db_, UT_DDL_CREATE_PHONE.c_str(), nullptr, nullptr, nullptr), SQLITE_OK);
    for (int i = 0; i < TEST_PRESET_TABLE_COUNT; i++) {
        std::string ddl = "CREATE TABLE IF NOT EXISTS test";
        ddl += std::to_string(i + 1);
        ddl += "(id INTEGER PRIMARY KEY, field1 INTEGER, field2 REAL, field3 TEXT, field4 BLOB, field5 TEXT);";
        EXPECT_EQ(sqlite3_exec(db_, ddl.c_str(), nullptr, nullptr, nullptr), SQLITE_OK);
    }
    std::vector<std::string> phoneList = {"Huawei", "Samsung", "Apple", "Xiaomi", "Oppo", "Vivo", "Realme"};
    for (int i = 0; i < TEST_PRESET_DATA_COUNT; i++) {
        std::string dml = "INSERT INTO phone(id, name, brand, price, type, desc) VALUES(";
        dml += std::to_string(i + 1) + ",'";
        dml += std::to_string(i + 1) + " Martin''s Phone','";
        dml += std::to_string(i + 1) + phoneList[i%phoneList.size()] + "',";
        dml += std::to_string(i + 1.0) + ",";
        dml += std::to_string(i + 1) + ",'" + UT_PHONE_DESC + UT_PHONE_DESC + UT_PHONE_DESC + UT_PHONE_DESC + "');";
        EXPECT_EQ(sqlite3_exec(db_, dml.c_str(), nullptr, nullptr, nullptr), SQLITE_OK) << sqlite3_errmsg(db_);
    }
    sqlite3_close(db_);
    EXPECT_EQ(sqlite3_open(TEST_DB, &db_), SQLITE_OK);
}

void SQLiteCompressTest::TearDown(void)
{
    sqlite3_close(db_);
    db_ = nullptr;
    sqlite3_config(SQLITE_CONFIG_LOG, NULL, NULL);
}

/**
 * @tc.name: CompressTest001
 * @tc.desc: Test to enable page compression on brand new db.
 * @tc.type: FUNC
 */
HWTEST_F(SQLiteCompressTest, CompressTest001, TestSize.Level0)
{
    if (!IsSupportPageCompress()) {
        GTEST_SKIP() << "Current testcase is not compatible";
    }
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
    if (!IsSupportPageCompress()) {
        GTEST_SKIP() << "Current testcase is not compatible";
    }
    /**
     * @tc.steps: step1. Compress db using Backup function:sqlite3_backup_step
     * @tc.expected: step1. Execute successfully
     */
    std::string dbPath1 = TEST_DIR "/compresstest0030.db";
    sqlite3 *compDb = nullptr;
    EXPECT_EQ(sqlite3_open_v2(dbPath1.c_str(), &compDb, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, "compressvfs"),
        SQLITE_OK);
    UtBackupDatabase(db_, compDb);
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
    if (!IsSupportPageCompress()) {
        GTEST_SKIP() << "Current testcase is not compatible";
    }
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
    if (!IsSupportPageCompress()) {
        GTEST_SKIP() << "Current testcase is not compatible";
    }
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
    if (!IsSupportPageCompress()) {
        GTEST_SKIP() << "Current testcase is not compatible";
    }
    /**
     * @tc.steps: step1. Create a brand new db while page compression enabled with sqlite3_open_v2
     * @tc.expected: step1. Execute successfully
     */
    std::string dbPath1 = TEST_DIR "/compresstest0060.db";
    sqlite3 *compDb = nullptr;
    EXPECT_EQ(sqlite3_open_v2(dbPath1.c_str(), &compDb, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, "compressvfs"),
        SQLITE_OK);
    EXPECT_EQ(sqlite3_exec(compDb, "PRAGMA meta_double_write=enabled;", nullptr, nullptr, nullptr), SQLITE_OK);
    EXPECT_EQ(sqlite3_exec(compDb, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr), SQLITE_OK);
    EXPECT_EQ(sqlite3_exec(compDb, UT_DDL_CREATE_DEMO.c_str(), nullptr, nullptr, nullptr), SQLITE_OK);
    EXPECT_EQ(sqlite3_exec(compDb, UT_DML_INSERT_DEMO.c_str(), nullptr, nullptr, nullptr), SQLITE_OK);
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

/**
 * @tc.name: CompressTest007
 * @tc.desc: Test to create brand new db
 * @tc.type: FUNC
 */
HWTEST_F(SQLiteCompressTest, CompressTest007, TestSize.Level0)
{
    if (!IsSupportPageCompress()) {
        GTEST_SKIP() << "Current testcase is not compatible";
    }
    /**
     * @tc.steps: step1. Create a brand new db while page compression enabled through sqlite3_open_v2
     * @tc.expected: step1. Execute successfully
     */
    std::string dbPath1 = TEST_DIR "/compresstest007.db";
    sqlite3 *compDb = nullptr;
    EXPECT_EQ(sqlite3_open_v2(dbPath1.c_str(), &compDb, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, "compressvfs"),
        SQLITE_OK);
    EXPECT_EQ(sqlite3_exec(compDb, "PRAGMA meta_double_write=enabled;", nullptr, nullptr, nullptr), SQLITE_OK);
    EXPECT_EQ(sqlite3_exec(compDb, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr), SQLITE_OK);
    EXPECT_EQ(sqlite3_exec(compDb, UT_DDL_CREATE_DEMO.c_str(), nullptr, nullptr, nullptr), SQLITE_OK);
    EXPECT_EQ(sqlite3_exec(compDb, UT_DML_INSERT_DEMO.c_str(), nullptr, nullptr, nullptr), SQLITE_OK);
    int persistMode = 1;
    EXPECT_EQ(sqlite3_file_control(compDb, "main", SQLITE_FCNTL_PERSIST_WAL, &persistMode), SQLITE_OK);
    sqlite3_close_v2(compDb);
    /**
     * @tc.steps: step5. Check result, files should exist
     * @tc.expected: step4. Execute successfully
     */
    std::string wal = dbPath1 + "-walcompress";
    std::string shm = dbPath1 + "-shmcompress";
    std::string dwr = dbPath1 + "-dwr";
    std::string lock = dbPath1 + "-lockcompress";
    EXPECT_TRUE(Common::IsFileExist(wal.c_str()));
    EXPECT_TRUE(Common::IsFileExist(shm.c_str()));
    EXPECT_TRUE(Common::IsFileExist(dwr.c_str()));
    EXPECT_TRUE(Common::IsFileExist(lock.c_str()));
}

/**
 * @tc.name: CompressTest008
 * @tc.desc: Test to create brand new db disabled compression
 * @tc.type: FUNC
 */
HWTEST_F(SQLiteCompressTest, CompressTest008, TestSize.Level0)
{
    /**
     * @tc.steps: step1. Create a brand new db while page compression disabled
     * @tc.expected: step1. Execute successfully
     */
    std::string dbPath1 = TEST_DIR "/test008.db";
    sqlite3 *db = nullptr;
    EXPECT_EQ(sqlite3_open_v2(dbPath1.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr),
        SQLITE_OK);
    EXPECT_EQ(sqlite3_exec(db, "PRAGMA meta_double_write=enabled;", nullptr, nullptr, nullptr), SQLITE_OK);
    EXPECT_EQ(sqlite3_exec(db, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr), SQLITE_OK);
    EXPECT_EQ(sqlite3_exec(db, UT_DDL_CREATE_DEMO.c_str(), nullptr, nullptr, nullptr), SQLITE_OK);
    EXPECT_EQ(sqlite3_exec(db, UT_DML_INSERT_DEMO.c_str(), nullptr, nullptr, nullptr), SQLITE_OK);
    int persistMode = 1;
    EXPECT_EQ(sqlite3_file_control(db, "main", SQLITE_FCNTL_PERSIST_WAL, &persistMode), SQLITE_OK);
    sqlite3_close_v2(db);
    /**
     * @tc.steps: step5. Check result, files should exist except lock file
     * @tc.expected: step4. Execute successfully
     */
    std::string wal = dbPath1 + "-wal";
    std::string shm = dbPath1 + "-shm";
    std::string dwr = dbPath1 + "-dwr";
    std::string lock = dbPath1 + "-lockcompress";
    EXPECT_TRUE(Common::IsFileExist(wal.c_str()));
    EXPECT_TRUE(Common::IsFileExist(shm.c_str()));
    EXPECT_TRUE(Common::IsFileExist(dwr.c_str()));
    EXPECT_FALSE(Common::IsFileExist(lock.c_str()));
}

/**
 * @tc.name: CompressTest009
 * @tc.desc: Test to open an non-cmopress db through vfs option:compress
 * @tc.type: FUNC
 */
HWTEST_F(SQLiteCompressTest, CompressTest009, TestSize.Level0)
{
    if (!IsSupportPageCompress()) {
        GTEST_SKIP() << "Current testcase is not compatible";
    }
    /**
     * @tc.steps: step1. Open the exist db through vfs option:compressvfs using SQLITE_OPEN_READONLY
     * @tc.expected: step1. Execute successfully
     */
    sqlite3 *db1 = nullptr;
    EXPECT_EQ(sqlite3_open_v2(TEST_DB, &db1, SQLITE_OPEN_READONLY, "compressvfs"), SQLITE_WARNING);
    // lock file not exist, return not a compress db
    EXPECT_EQ(sqlite3_extended_errcode(db1), SQLITE_WARNING_NOTCOMPRESSDB);
    sqlite3_close_v2(db1);
}

/**
 * @tc.name: CompressTest010
 * @tc.desc: Test to insert data while transaction executing
 * @tc.type: FUNC
 */
HWTEST_F(SQLiteCompressTest, CompressTest010, TestSize.Level0)
{
    if (!IsSupportPageCompress()) {
        GTEST_SKIP() << "Current testcase is not compatible";
    }
    /**
     * @tc.steps: step1. Create a brand new db while page compression enabled
     * @tc.expected: step1. Execute successfully
     */
    std::string dbPath = TEST_DIR "/compresstest010.db";
    UtPresetDb(dbPath, "compressvfs");
    /**
     * @tc.steps: step2. Open the db used to begin transaction, delete 5 entries
     * @tc.expected: step2. Execute successfully
     */
    sqlite3 *db1 = nullptr;
    EXPECT_EQ(sqlite3_open_v2(dbPath.c_str(), &db1, SQLITE_OPEN_READWRITE, "compressvfs"), SQLITE_OK);
    EXPECT_EQ(sqlite3_exec(db1, "BEGIN;", nullptr, nullptr, nullptr), SQLITE_OK);
    // delete data from 996 ~ 1000
    EXPECT_EQ(sqlite3_exec(db1, "DELETE FROM salary WHERE entryId > 995;", nullptr, nullptr, nullptr), SQLITE_OK);
    /**
     * @tc.steps: step3. Open another db used to insert data, before commit transaction
     * @tc.expected: step3. Execute successfully
     */
    sqlite3 *db2 = nullptr;
    EXPECT_EQ(sqlite3_open_v2(dbPath.c_str(), &db2, SQLITE_OPEN_READWRITE, "compressvfs"), SQLITE_OK);
    static const char *UT_DDL_CREATE_TABLE = "CREATE TABLE salary123"
        "(entryId INTEGER PRIMARY KEY, entryName Text, salary REAL, class INTEGER);";
    EXPECT_EQ(sqlite3_exec(db2, UT_DDL_CREATE_TABLE, NULL, NULL, NULL), SQLITE_BUSY);
    sqlite3_close_v2(db2);
    EXPECT_EQ(sqlite3_exec(db1, "COMMIT;", nullptr, nullptr, nullptr), SQLITE_OK);
    sqlite3_close_v2(db1);
}

/**
 * @tc.name: CompressTest011
 * @tc.desc: Test to check lock file name
 * @tc.type: FUNC
 */
HWTEST_F(SQLiteCompressTest, CompressTest011, TestSize.Level0)
{
    if (!IsSupportPageCompress()) {
        GTEST_SKIP() << "Current testcase is not compatible";
    }
    /**
     * @tc.steps: step1. Get lock file name from non-compress db connection
     * @tc.expected: step1. Execute successfully
     */
    const char *dbName = sqlite3_db_filename(db_, "main");
    const char *walName = sqlite3_filename_wal(dbName);
    const char *lockName = walName + strlen(walName) + 1;
    std::string expectLockFilenameSuffix = "/sqlitecompresstest/test.db";
    std::string lockPathStr = lockName;
    EXPECT_TRUE(lockPathStr.find(expectLockFilenameSuffix) != std::string::npos);
    /**
     * @tc.steps: step2. Create a brand new db while page compression enabled, check lock filename
     * @tc.expected: step2. Execute successfully
     */
    std::string dbPath = TEST_DIR "/compresstest011.db";
    UtPresetDb(dbPath, "compressvfs");
    sqlite3 *db1 = nullptr;
    EXPECT_EQ(sqlite3_open_v2(dbPath.c_str(), &db1, SQLITE_OPEN_READWRITE, "compressvfs"), SQLITE_OK);
    EXPECT_EQ(sqlite3_exec(db1, UT_DDL_CREATE_PHONE.c_str(), nullptr, nullptr, nullptr), SQLITE_OK);
    const char *dbName1 = sqlite3_db_filename(db1, "main");
    const char *walName1 = sqlite3_filename_wal(dbName1);
    const char *lockName1 = walName1 + strlen(walName1) + 1;
    std::string expectLockFilenameSuffix1 = "/sqlitecompresstest/compresstest011.db-lockcompress";
    lockPathStr = lockName1;
    EXPECT_TRUE(lockPathStr.find(expectLockFilenameSuffix1) != std::string::npos);
    sqlite3_close_v2(db1);
}

/**
 * @tc.name: CompressTest012
 * @tc.desc: Test to check lock file name
 * @tc.type: FUNC
 */
HWTEST_F(SQLiteCompressTest, CompressTest012, TestSize.Level0)
{
    if (!IsSupportPageCompress()) {
        GTEST_SKIP() << "Current testcase is not compatible";
    }
    /**
     * @tc.steps: step1. Create brand new db as main db, and a compress db as slave
     * @tc.expected: step1. Execute successfully
     */
    std::string dbPath = TEST_DIR "/test011.db";
    UtPresetDb(dbPath, "");
    sqlite3 *db = nullptr;
    EXPECT_EQ(sqlite3_open_v2(dbPath.c_str(), &db, SQLITE_OPEN_READWRITE, nullptr), SQLITE_OK);
    std::string slavePath = TEST_DIR "/test011_slave.db";
    sqlite3 *slaveDb = nullptr;
    EXPECT_EQ(sqlite3_open_v2(slavePath.c_str(), &slaveDb, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, "compressvfs"),
        SQLITE_OK);
    EXPECT_EQ(sqlite3_exec(slaveDb, "PRAGMA meta_double_write=enabled;", nullptr, nullptr, nullptr), SQLITE_OK);
    EXPECT_EQ(sqlite3_exec(slaveDb, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr), SQLITE_OK);
    EXPECT_EQ(sqlite3_exec(slaveDb, UT_DDL_CREATE_DEMO.c_str(), nullptr, nullptr, nullptr), SQLITE_OK);
    EXPECT_EQ(sqlite3_exec(slaveDb, UT_DML_INSERT_DEMO.c_str(), nullptr, nullptr, nullptr), SQLITE_OK);
    int persistMode = 1;
    EXPECT_EQ(sqlite3_file_control(slaveDb, "main", SQLITE_FCNTL_PERSIST_WAL, &persistMode), SQLITE_OK);
    UtBackupDatabase(db, slaveDb);
    /**
     * @tc.steps: step2. Insert serveral data into slave db, then backup again
     * @tc.expected: step2. Execute successfully
     */
    static const char *UT_INSERT_DATA = "INSERT INTO salary(entryId, entryName, salary, class) VALUES(?,?,?,?);";
    sqlite3_stmt *insertStmt = NULL;
    EXPECT_EQ(sqlite3_prepare_v2(slaveDb, UT_INSERT_DATA, -1, &insertStmt, NULL), SQLITE_OK);
    for (int i = 0; i < 100; i++) {  // 100 means the total number of insert data
        sqlite3_bind_int(insertStmt, 1, i + 20000);  // 20000 is the begining of entryId to insert
        sqlite3_bind_text(insertStmt, 2, "salary-entry-name", -1, SQLITE_STATIC);  // 2 is the seq number of 2nd field
        sqlite3_bind_double(insertStmt, 3, i);  // 3 is the seq number of 3rd field
        sqlite3_bind_int(insertStmt, 4, i + 1);  // 4 is the seq number of 4th field
        EXPECT_EQ(sqlite3_step(insertStmt), SQLITE_DONE);
        sqlite3_reset(insertStmt);
    }
    sqlite3_finalize(insertStmt);
    // Backup again, all data insert above should be covered
    UtBackupDatabase(db, slaveDb);
    sqlite3_close_v2(slaveDb);
    sqlite3_close_v2(db);
    UtCheckPresetDb(slavePath, "compressvfs");
}

/**
 * @tc.name: CompressTest013
 * @tc.desc: Test to open a file was corrupted
 * @tc.type: FUNC
 */
HWTEST_F(SQLiteCompressTest, CompressTest013, TestSize.Level0)
{
    if (!IsSupportPageCompress()) {
        GTEST_SKIP() << "Current testcase is not compatible";
    }
    /**
     * @tc.steps: step1. Create brand new db as slave db using compress
     * @tc.expected: step1. Execute successfully
     */
    std::string slavePath = TEST_DIR "/test013_slave.db";
    sqlite3 *slaveDb = nullptr;
    EXPECT_EQ(sqlite3_open_v2(slavePath.c_str(), &slaveDb, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, "compressvfs"),
        SQLITE_OK);
    EXPECT_EQ(sqlite3_exec(slaveDb, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr), SQLITE_OK);
    EXPECT_EQ(sqlite3_exec(slaveDb, UT_DDL_CREATE_DEMO.c_str(), nullptr, nullptr, nullptr), SQLITE_OK);
    EXPECT_EQ(sqlite3_exec(slaveDb, UT_DML_INSERT_DEMO.c_str(), nullptr, nullptr, nullptr), SQLITE_OK);
    sqlite3_close_v2(slaveDb);
    slaveDb = nullptr;
    /**
     * @tc.steps: step2. Corrupted db file, make it unrecognized db file
     * @tc.expected: step1. Execute successfully
     */
    Common::DestroyDbFile(slavePath, 0, "testcase013");
    EXPECT_EQ(sqlite3_open_v2(slavePath.c_str(), &slaveDb, SQLITE_OPEN_READWRITE, "compressvfs"), SQLITE_NOTADB);
}

/**
 * @tc.name: CompressTest014
 * @tc.desc: Test to open a file is corrupted
 * @tc.type: FUNC
 */
HWTEST_F(SQLiteCompressTest, CompressTest014, TestSize.Level0)
{
    if (!IsSupportPageCompress()) {
        GTEST_SKIP() << "Current testcase is not compatible";
    }
    /**
     * @tc.steps: step1. Create brand new db as slave db using compress
     * @tc.expected: step1. Execute successfully
     */
    std::string slavePath = TEST_DIR "/test013_slave.db";
    sqlite3 *slaveDb = nullptr;
    EXPECT_EQ(sqlite3_open_v2(slavePath.c_str(), &slaveDb, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, "compressvfs"),
        SQLITE_OK);
    EXPECT_EQ(sqlite3_exec(slaveDb, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr), SQLITE_OK);
    EXPECT_EQ(sqlite3_exec(slaveDb, UT_DDL_CREATE_DEMO.c_str(), nullptr, nullptr, nullptr), SQLITE_OK);
    for (int i = 0; i < 1000; i++) {  // 1000 means the total number of insert data
        std::string insert = "INSERT INTO demo(id, name) VALUES(";
        insert += std::to_string(1000 + i);  // 1000 means the start id of insert data
        insert += ", '";
        insert += std::string(100, 'a' + (i % ('z' - 'a'))) + "Call";  // 100 means the length of field's value
        insert += "');";
        EXPECT_EQ(sqlite3_exec(slaveDb, insert.c_str(), nullptr, nullptr, nullptr), SQLITE_OK);
    }
    sqlite3_close_v2(slaveDb);
    slaveDb = nullptr;
    /**
     * @tc.steps: step2. Corrupted db file
     * @tc.expected: step1. Execute successfully
     */
    Common::DestroyDbFile(slavePath, 5 * 4096, "testcase013");  // 5 * 4096 means the 6th page, belongs to vfs_pages
    EXPECT_EQ(sqlite3_open_v2(slavePath.c_str(), &slaveDb, SQLITE_OPEN_READWRITE, "compressvfs"), SQLITE_OK);
    EXPECT_EQ(sqlite3_exec(slaveDb, "SELECT COUNT(name) FROM demo;", nullptr, nullptr, nullptr), SQLITE_CORRUPT);
    sqlite3_close_v2(slaveDb);
}

/**
 * @tc.name: CompressTest015
 * @tc.desc: Test to operate empty compress database
 * @tc.type: FUNC
 */
HWTEST_F(SQLiteCompressTest, CompressTest015, TestSize.Level0)
{
    if (!IsSupportPageCompress()) {
        GTEST_SKIP() << "Current testcase is not compatible";
    }
    std::string slavePath = TEST_DIR "/test015_slave.db";
    sqlite3 *slaveDb = nullptr;
    EXPECT_EQ(sqlite3_open_v2(slavePath.c_str(), &slaveDb, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, "compressvfs"),
        SQLITE_OK);
    sqlite3_close_v2(slaveDb);
    EXPECT_EQ(sqlite3_open_v2(slavePath.c_str(), &slaveDb, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, "compressvfs"),
        SQLITE_OK);
    // Drop vfs_pages to simulate empty database
    sqlite3 *emptySlaveDb = nullptr;
    EXPECT_EQ(sqlite3_open_v2(slavePath.c_str(), &emptySlaveDb, SQLITE_OPEN_READWRITE, nullptr), SQLITE_OK);
    EXPECT_EQ(sqlite3_exec(emptySlaveDb, "DROP TABLE vfs_pages;", nullptr, nullptr, nullptr), SQLITE_OK);
    sqlite3_close_v2(emptySlaveDb);

    EXPECT_EQ(sqlite3_exec(slaveDb, "SELECT COUNT(*) FROM sqlite_master WHERE type='table';", nullptr, nullptr, nullptr),
        SQLITE_OK);
    sqlite3_close_v2(slaveDb);
}

}  // namespace Test
