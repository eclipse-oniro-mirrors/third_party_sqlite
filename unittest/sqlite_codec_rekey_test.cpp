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
#include <fcntl.h>
#include <sys/types.h>

#include <cstdio>
#include <fstream>
#include <string>
#include <unistd.h>
#include <vector>
#include <sys/mman.h>

#include "sqlite3sym.h"

using namespace testing::ext;
using namespace std;

#define TEST_DIR "./sqlitetest"
#define TEST_DB TEST_DIR "/test.db"

#define REKEY_RENAME_SUFFIX "-rekey-rename"
#define REKEY_EXPORT_SUFFIX "-rekey-export"
#define REKEY_LOCK_SUFFIX "-rekey-lock"

#define MULPROC_STEP_1 0
#define MULPROC_STEP_2 1
#define MULPROC_STEP_3 2

namespace Test {
static void UtSqliteLogPrint(const void *data, int err, const char *msg)
{
    std::cout << "LibSQLiteRekeyTest SQLite xLog err:" << err << ", msg:" << msg << std::endl;
}

class LibSQLiteRekeyTest : public testing::Test {
public:
    static void SetUpTestCase(void);
    static void TearDownTestCase(void);
    void SetUp();
    void TearDown();
};

void LibSQLiteRekeyTest::SetUpTestCase(void)
{
    sqlite3_config(SQLITE_CONFIG_LOG, &UtSqliteLogPrint, NULL);
    // permission 0770
    mkdir(TEST_DIR, 0770);
}

void LibSQLiteRekeyTest::TearDownTestCase(void)
{
    sqlite3_config(SQLITE_CONFIG_LOG, NULL, NULL);
}

void LibSQLiteRekeyTest::SetUp(void)
{
    system("rm -rf " TEST_DB "*");
}

void LibSQLiteRekeyTest::TearDown(void)
{
}

static void CreateTable(sqlite3* db, std::string tableName, std::string indexName)
{
    // Enable WAL (Write-Ahead Logging) mode
    ASSERT_EQ(sqlite3_exec(db, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr), SQLITE_OK)
        << "Failed to enable WAL mode: " << sqlite3_errmsg(db);
    
    const std::string sql = "CREATE TABLE " + tableName +
        "(id INTEGER PRIMARY KEY, "
        "name TEXT NOT NULL, "
        "users TEXT UNIQUE);";
    
    ASSERT_EQ(sqlite3_exec(db, sql.c_str(), nullptr, nullptr, nullptr), SQLITE_OK)
        << tableName << " table creation failed: " << sqlite3_errmsg(db);

    // Create indexes
    const std::string sqlIdx = "CREATE INDEX " + indexName + " ON " + tableName + "(users);";
    
    ASSERT_EQ(sqlite3_exec(db, sqlIdx.c_str(), nullptr, nullptr, nullptr), SQLITE_OK)
        << "Index creation failed: " << sqlite3_errmsg(db);
}

// Helper function to insert records
static void InsertRecords(sqlite3* db, const std::string& table, int count)
{
    const std::string sql = "INSERT INTO " + table + " VALUES (?,?,?);";
    sqlite3_stmt* stmt = nullptr;
    // Prepare statement
    ASSERT_EQ(sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr), SQLITE_OK)
        << "Prepare failed for " << table << ": " << sqlite3_errmsg(db);
    // Insert test data
    for (int i = 0; i < count; ++i) {
        // Bind parameters based on table structure
        std::string name = "User_" + std::to_string(i);
        std::string users = "user" + std::to_string(i) + "@example.com";
        
        sqlite3_bind_int(stmt, 1, i);
        sqlite3_bind_text(stmt, 2, name.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 3, users.c_str(), -1, SQLITE_STATIC);
        // Execute insertion
        ASSERT_EQ(sqlite3_step(stmt), SQLITE_DONE)
            << "Insertion failed in " << table << " at index " << i << ": " << sqlite3_errmsg(db);
        
        // Reset statement for next use
        ASSERT_EQ(sqlite3_reset(stmt), SQLITE_OK)
            << "Reset failed in " << table << " at index " << i;
    }
    // Finalize statement
    ASSERT_EQ(sqlite3_finalize(stmt), SQLITE_OK)
        << "Finalization failed for " << table;
}

// Helper to retrieve index information
static int QueryIndexInfo(sqlite3* db, const std::string &indexName)
{
    sqlite3_stmt* stmt = nullptr;
    std::string sql = "PRAGMA index_info(" + indexName + ");";
    
    std::cout << "\nIndex Information for: " << indexName << "\n";
    std::cout << "-----------------------------------\n";

    int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::cout << "Index info prepare failed: " << sqlite3_errmsg(db) << std::endl;
        return rc;
    }
    int cols = sqlite3_column_count(stmt);
    std::cout << "Columns: " << cols << "\n";

    // Print header
    for (int i = 0; i < cols; ++i) {
        std::cout << sqlite3_column_name(stmt, i) << " ";
    }
    int queryCnt = 0;
    // Fetch rows
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        queryCnt++;
    }
    sqlite3_finalize(stmt);
    return (queryCnt == 1) ? SQLITE_OK : SQLITE_ERROR;
}

// Helper function to get record count
static int GetRecordCount(sqlite3* db, std::string table, int &count)
{
    const std::string sqlCount = "SELECT COUNT(*) FROM " + table + ";";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, sqlCount.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::cout << "Count preparation failed for " << table << std::endl;
        return rc;
    }
        
    rc = sqlite3_step(stmt);
    if (rc != SQLITE_OK && rc != SQLITE_ROW && rc != SQLITE_DONE) {
        std::cout << "Count execution failed for " << table << std::endl;
        return rc;
    }
    count = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    return SQLITE_OK;
}

static void PrepareDataForDb(sqlite3* db)
{
    CreateTable(db, "customers", "customers_idx");
    CreateTable(db, "orders", "orders_idx");
    InsertRecords(db, "customers", 50);
    InsertRecords(db, "orders", 100);
}

static void QueryData(sqlite3* db)
{
    // Verify record counts
    int count = 0;
    EXPECT_EQ(GetRecordCount(db, "customers", count), SQLITE_OK) << "Incorrect get customer count";
    EXPECT_EQ(count, 50) << "Incorrect customer count";
    EXPECT_EQ(GetRecordCount(db, "orders", count), SQLITE_OK) << "Incorrect get order count";
    EXPECT_EQ(count, 100) << "Incorrect customer count";
    // Query index information
    EXPECT_EQ(QueryIndexInfo(db, "customers_idx"), SQLITE_OK);
    EXPECT_EQ(QueryIndexInfo(db, "orders_idx"), SQLITE_OK);
}

static void EncryptDbConfig(sqlite3* db, CodecConfig *config)
{
    // key size 32
    ASSERT_EQ(sqlite3_key(db, config->pKey, config->nKey), SQLITE_OK);
    const std::string sqlKey = "PRAGMA codec_cipher='" + std::string((const char *)config->pCipher) +
        "';PRAGMA codec_hmac_algo='" + std::string((const char *)config->pHmacAlgo) + "';PRAGMA codec_kdf_algo='" +
        std::string((const char *)config->pKdfAlgo) + "';PRAGMA codec_page_size='" +
        std::to_string(config->pageSize) + "';PRAGMA codec_kdf_iter='" + std::to_string(config->kdfIter) + "';";
    ASSERT_EQ(sqlite3_exec(db, sqlKey.c_str(), nullptr, nullptr, nullptr), SQLITE_OK)
        << "execute " << sqlKey << " failed: " << sqlite3_errmsg(db);
}

/**
 * @tc.name: LibSQLiteRekeyTest001
 * @tc.desc: Test rekey only with changed key
 * @tc.type: FUNC
 */
HWTEST_F(LibSQLiteRekeyTest, LibSQLiteRekeyTest001, TestSize.Level0)
{
    sqlite3* db;
    /**
     * @tc.steps: step1. Open database and create table
     * @tc.expected: step1. Return SQLITE_OK
     */
    ASSERT_EQ(sqlite3_open(TEST_DB, &db), SQLITE_OK);
    CodecConfig config = {
        "aes-256-gcm", "SHA1", "KDF_SHA1", "01234567890123456789012345678901", 32, 5000, 1024
    };
    EncryptDbConfig(db, &config);
    PrepareDataForDb(db);
    sqlite3_close(db);
    /**
     * @tc.steps: step2. Rekey with new passwd
     * @tc.expected: step2. Return SQLITE_OK
     */
    CodecRekeyConfig rekeyCfg = {
        TEST_DB,
        { "aes-256-gcm", "SHA1", "KDF_SHA1", "01234567890123456789012345678901", 32, 5000, 1024 },
        { "aes-256-gcm", "SHA1", "KDF_SHA1", "11234567890123456789012345678901", 32, 5000, 1024 }
    };
    ASSERT_EQ(sqlite3_rekey_v3(&rekeyCfg), SQLITE_OK);
    /**
     * @tc.steps: step3. Decrypt with invalid passwd
     * @tc.expected: step3. Return SQLITE_NOTADB
     */
    ASSERT_EQ(sqlite3_open(TEST_DB, &db), SQLITE_OK);
    config = {
        "aes-256-gcm", "SHA1", "KDF_SHA1", "01234567890123456789012345678901", 32, 5000, 1024
    };
    EncryptDbConfig(db, &config);
    int count = 0;
    EXPECT_EQ(GetRecordCount(db, "customers", count), SQLITE_NOTADB) << "Incorrect query";
    /**
     * @tc.steps: step3. Decrypt with valid passwd
     * @tc.expected: step3. Return SQLITE_OK
     */
    config = {
        "aes-256-gcm", "SHA1", "KDF_SHA1", "11234567890123456789012345678901", 32, 5000, 1024
    };
    EncryptDbConfig(db, &config);
    QueryData(db);
    sqlite3_close(db);
}

/**
 * @tc.name: LibSQLiteRekeyTest002
 * @tc.desc: Test rekey with changed codec param
 * @tc.type: FUNC
 */
HWTEST_F(LibSQLiteRekeyTest, LibSQLiteRekeyTest002, TestSize.Level0)
{
    sqlite3* db;
    /**
     * @tc.steps: step1. Open database and create table
     * @tc.expected: step1. Return SQLITE_OK
     */
    ASSERT_EQ(sqlite3_open(TEST_DB, &db), SQLITE_OK);
    CodecConfig config = {
        "aes-256-gcm", "SHA1", "KDF_SHA1", "01234567890123456789012345678901", 32, 5000, 1024
    };
    EncryptDbConfig(db, &config);
    PrepareDataForDb(db);
    sqlite3_close(db);
    /**
     * @tc.steps: step2. Rekey with new passwd and codec param
     * @tc.expected: step2. Return SQLITE_OK
     */
    CodecRekeyConfig rekeyCfg = {
        TEST_DB,
        { "aes-256-gcm", "SHA1", "KDF_SHA1", "01234567890123456789012345678901", 32, 5000, 1024 },
        { "aes-256-gcm", "SHA256", "KDF_SHA256", "11234567890123456789012345678901", 32, 5000, 1024 }
    };
    ASSERT_EQ(sqlite3_rekey_v3(&rekeyCfg), SQLITE_OK);
    /**
     * @tc.steps: step3. Decrypt with valid passwd
     * @tc.expected: step3. Return SQLITE_OK
     */
    ASSERT_EQ(sqlite3_open(TEST_DB, &db), SQLITE_OK);
    config = {
        "aes-256-gcm", "SHA256", "KDF_SHA256", "11234567890123456789012345678901", 32, 5000, 1024
    };
    EncryptDbConfig(db, &config);
    QueryData(db);
    sqlite3_close(db);
}

/**
 * @tc.name: LibSQLiteRekeyTest003
 * @tc.desc: Test rekey with changed codec param and pagesz
 * @tc.type: FUNC
 */
HWTEST_F(LibSQLiteRekeyTest, LibSQLiteRekeyTest003, TestSize.Level0)
{
    sqlite3* db;
    /**
     * @tc.steps: step1. Open database and create table
     * @tc.expected: step1. Return SQLITE_OK
     */
    ASSERT_EQ(sqlite3_open(TEST_DB, &db), SQLITE_OK);
    CodecConfig config = {
        "aes-256-gcm", "SHA1", "KDF_SHA1", "01234567890123456789012345678901", 32, 5000, 1024
    };
    EncryptDbConfig(db, &config);
    PrepareDataForDb(db);
    sqlite3_close(db);
    /**
     * @tc.steps: step2. Rekey with new passwd and codec param
     * @tc.expected: step2. Return SQLITE_OK
     */
    CodecRekeyConfig rekeyCfg = {
        TEST_DB,
        { "aes-256-gcm", "SHA1", "KDF_SHA1", "01234567890123456789012345678901", 32, 5000, 1024 },
        { "aes-256-gcm", "SHA256", "KDF_SHA256", "11234567890123456789012345678901", 32, 5000, 4096 }
    };
    ASSERT_EQ(sqlite3_rekey_v3(&rekeyCfg), SQLITE_OK);
    /**
     * @tc.steps: step3. assert rekey relevant file is deleted
     * @tc.expected: step3. Return SQLITE_OK
     */
    ASSERT_EQ(access(TEST_DB REKEY_LOCK_SUFFIX, F_OK), -1)
     << "File unexpectedly exists: " << TEST_DB REKEY_LOCK_SUFFIX;
    ASSERT_EQ(access(TEST_DB REKEY_EXPORT_SUFFIX, F_OK), -1)
     << "File unexpectedly exists: " << TEST_DB REKEY_EXPORT_SUFFIX;
    ASSERT_EQ(access(TEST_DB REKEY_RENAME_SUFFIX, F_OK), -1)
     << "File unexpectedly exists: " << TEST_DB REKEY_RENAME_SUFFIX;
    /**
     * @tc.steps: step4. Decrypt with valid passwd
     * @tc.expected: step4. Return SQLITE_OK
     */
    ASSERT_EQ(sqlite3_open(TEST_DB, &db), SQLITE_OK);
    config = {
        "aes-256-gcm", "SHA256", "KDF_SHA256", "11234567890123456789012345678901", 32, 5000, 4096
    };
    EncryptDbConfig(db, &config);
    QueryData(db);
    sqlite3_close(db);
}

/**
 * @tc.name: LibSQLiteRekeyTest004
 * @tc.desc: Test rekey to unencrypt db
 * @tc.type: FUNC
 */
HWTEST_F(LibSQLiteRekeyTest, LibSQLiteRekeyTest004, TestSize.Level0)
{
    sqlite3* db;
    /**
     * @tc.steps: step1. Open database and create table
     * @tc.expected: step1. Return SQLITE_OK
     */
    ASSERT_EQ(sqlite3_open(TEST_DB, &db), SQLITE_OK);
    CodecConfig config = {
        "aes-256-gcm", "SHA1", "KDF_SHA1", "01234567890123456789012345678901", 32, 5000, 1024
    };
    EncryptDbConfig(db, &config);
    PrepareDataForDb(db);
    sqlite3_close(db);
    /**
     * @tc.steps: step2. Rekey to unencrypt db and pagesize no changed
     * @tc.expected: step2. Return SQLITE_OK
     */
    CodecRekeyConfig rekeyCfg = {
        TEST_DB,
        { "aes-256-gcm", "SHA1", "KDF_SHA1", "01234567890123456789012345678901", 32, 5000, 1024 },
        { NULL, NULL, NULL, NULL, 0, 0, 1024 }
    };
    ASSERT_EQ(sqlite3_rekey_v3(&rekeyCfg), SQLITE_OK);
    /**
     * @tc.steps: step3. query unencrypt db
     * @tc.expected: step3. Return SQLITE_OK
     */
    ASSERT_EQ(sqlite3_open(TEST_DB, &db), SQLITE_OK);
    QueryData(db);
    sqlite3_close(db);
}

/**
 * @tc.name: LibSQLiteRekeyTest005
 * @tc.desc: Test rekey to unencrypt db and pagesz changed
 * @tc.type: FUNC
 */
HWTEST_F(LibSQLiteRekeyTest, LibSQLiteRekeyTest005, TestSize.Level0)
{
    sqlite3* db;
    /**
     * @tc.steps: step1. Open database and create table
     * @tc.expected: step1. Return SQLITE_OK
     */
    ASSERT_EQ(sqlite3_open(TEST_DB, &db), SQLITE_OK);
    CodecConfig config = {
        "aes-256-gcm", "SHA1", "KDF_SHA1", "01234567890123456789012345678901", 32, 5000, 1024
    };
    EncryptDbConfig(db, &config);
    PrepareDataForDb(db);
    sqlite3_close(db);
    /**
     * @tc.steps: step2. Rekey to unencrypt db and pagesize changed
     * @tc.expected: step2. Return SQLITE_OK
     */
    CodecRekeyConfig rekeyCfg = {
        TEST_DB,
        { "aes-256-gcm", "SHA1", "KDF_SHA1", "01234567890123456789012345678901", 32, 5000, 1024 },
        { NULL, NULL, NULL, NULL, 0, 0, 4096 }
    };
    ASSERT_EQ(sqlite3_rekey_v3(&rekeyCfg), SQLITE_OK);
    /**
     * @tc.steps: step3. query unencrypt db
     * @tc.expected: step3. Return SQLITE_OK
     */
    ASSERT_EQ(sqlite3_open(TEST_DB, &db), SQLITE_OK);
    QueryData(db);
    sqlite3_close(db);
}

/**
 * @tc.name: LibSQLiteRekeyTest006
 * @tc.desc: Test rekey to unencrypt db and pagesz no changed
 * @tc.type: FUNC
 */
HWTEST_F(LibSQLiteRekeyTest, LibSQLiteRekeyTest006, TestSize.Level0)
{
    sqlite3* db;
    /**
     * @tc.steps: step1. Open database and create table
     * @tc.expected: step1. Return SQLITE_OK
     */
    ASSERT_EQ(sqlite3_open(TEST_DB, &db), SQLITE_OK);
    PrepareDataForDb(db);
    sqlite3_close(db);
    /**
     * @tc.steps: step2. Rekey to unencrypt db and pagesize no changed
     * @tc.expected: step2. Return SQLITE_OK
     */
    CodecRekeyConfig rekeyCfg = {
        TEST_DB,
        { NULL, NULL, NULL, NULL, 0, 0, 4096 },
        { "aes-256-gcm", "SHA1", "KDF_SHA1", "01234567890123456789012345678901", 32, 5000, 1024 },
    };
    ASSERT_EQ(sqlite3_rekey_v3(&rekeyCfg), SQLITE_OK);
    /**
     * @tc.steps: step3. query unencrypt db
     * @tc.expected: step3. Return SQLITE_OK
     */
    ASSERT_EQ(sqlite3_open(TEST_DB, &db), SQLITE_OK);
    CodecConfig config = {
        "aes-256-gcm", "SHA1", "KDF_SHA1", "01234567890123456789012345678901", 32, 5000, 1024
    };
    EncryptDbConfig(db, &config);
    QueryData(db);
    sqlite3_close(db);
}

/**
 * @tc.name: LibSQLiteRekeyTest007
 * @tc.desc: Test rekey to unencrypt db and pagesz changed
 * @tc.type: FUNC
 */
HWTEST_F(LibSQLiteRekeyTest, LibSQLiteRekeyTest007, TestSize.Level0)
{
    sqlite3* db;
    /**
     * @tc.steps: step1. Open database and create table
     * @tc.expected: step1. Return SQLITE_OK
     */
    ASSERT_EQ(sqlite3_open(TEST_DB, &db), SQLITE_OK);
    PrepareDataForDb(db);
    sqlite3_close(db);
    /**
     * @tc.steps: step2. Rekey to unencrypt db and pagesize changed
     * @tc.expected: step2. Return SQLITE_OK
     */
    CodecRekeyConfig rekeyCfg = {
        TEST_DB,
        { NULL, NULL, NULL, NULL, 0, 0, 4096 },
        { "aes-256-gcm", "SHA1", "KDF_SHA1", "01234567890123456789012345678901", 32, 5000, 4096 },
    };
    ASSERT_EQ(sqlite3_rekey_v3(&rekeyCfg), SQLITE_OK);
    /**
     * @tc.steps: step3. query unencrypt db
     * @tc.expected: step3. Return SQLITE_OK
     */
    ASSERT_EQ(sqlite3_open(TEST_DB, &db), SQLITE_OK);
    CodecConfig config = {
        "aes-256-gcm", "SHA1", "KDF_SHA1", "01234567890123456789012345678901", 32, 5000, 4096
    };
    EncryptDbConfig(db, &config);
    QueryData(db);
    sqlite3_close(db);
}

/**
 * @tc.name: LibSQLiteRekeyTest008
 * @tc.desc: Test rekey return busy when other connection is open
 * @tc.type: FUNC
 */
HWTEST_F(LibSQLiteRekeyTest, LibSQLiteRekeyTest008, TestSize.Level0)
{
    sqlite3* db;
    /**
     * @tc.steps: step1. Open database and create table
     * @tc.expected: step1. Return SQLITE_OK
     */
    ASSERT_EQ(sqlite3_open(TEST_DB, &db), SQLITE_OK);
    PrepareDataForDb(db);

    /**
     * @tc.steps: step2. Rekey to unencrypt db and pagesize changed
     * @tc.expected: step2. Return SQLITE_OK
     */
    CodecRekeyConfig rekeyCfg = {
        TEST_DB,
        { NULL, NULL, NULL, NULL, 0, 0, 4096 },
        { "aes-256-gcm", "SHA1", "KDF_SHA1", "01234567890123456789012345678901", 32, 5000, 4096 },
    };
    ASSERT_EQ(sqlite3_rekey_v3(&rekeyCfg), SQLITE_BUSY);
    sqlite3_close(db);
}

/**
 * @tc.name: LibSQLiteRekeyTest009
 * @tc.desc: Test encrypted db rekey return busy when other connection is open
 * @tc.type: FUNC
 */
HWTEST_F(LibSQLiteRekeyTest, LibSQLiteRekeyTest009, TestSize.Level0)
{
    sqlite3* db;
    /**
     * @tc.steps: step1. Open database and create table
     * @tc.expected: step1. Return SQLITE_OK
     */
    ASSERT_EQ(sqlite3_open(TEST_DB, &db), SQLITE_OK);
    CodecConfig config = {
        "aes-256-gcm", "SHA256", "KDF_SHA256", "01234567890123456789012345678901", 32, 5000, 4096
    };
    EncryptDbConfig(db, &config);
    PrepareDataForDb(db);

    /**
     * @tc.steps: step2. Rekey to unencrypt db and pagesize changed
     * @tc.expected: step2. Return SQLITE_OK
     */
    CodecRekeyConfig rekeyCfg = {
        TEST_DB,
        { "aes-256-gcm", "SHA256", "KDF_SHA256", "01234567890123456789012345678901", 32, 5000, 4096 },
        { "aes-256-gcm", "SHA1", "KDF_SHA1", "01234567890123456789012345678901", 32, 5000, 4096 },
    };
    ASSERT_EQ(sqlite3_rekey_v3(&rekeyCfg), SQLITE_BUSY);
    sqlite3_close(db);
}

/**
 * @tc.name: LibSQLiteRekeyTest010
 * @tc.desc: Test db rekey return busy when others process is open
 * @tc.type: FUNC
 */
HWTEST_F(LibSQLiteRekeyTest, LibSQLiteRekeyTest010, TestSize.Level0)
{
    // 1. 创建匿名内存映射 (可读写/共享)
    size_t mapSize = sizeof(int);
    int *shared = (int *)mmap(nullptr, mapSize, PROT_READ | PROT_WRITE,
        MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ASSERT_NE(shared, MAP_FAILED);
    *shared = MULPROC_STEP_1;
    pid_t pid = fork();
    ASSERT_GE(pid, 0);
    // child
    if (pid == 0) {
        sqlite3* db;
        /**
        * @tc.steps: step1. Open database and create table
        * @tc.expected: step1. Return SQLITE_OK
        */
        ASSERT_EQ(sqlite3_open(TEST_DB, &db), SQLITE_OK);
        CodecConfig config = {
            "aes-256-gcm", "SHA256", "KDF_SHA256", "01234567890123456789012345678901", 32, 5000, 4096
        };
        EncryptDbConfig(db, &config);
        PrepareDataForDb(db);
        *shared = MULPROC_STEP_2;
        while (*shared != MULPROC_STEP_3) {
            usleep(20);
        }
        sqlite3_close(db);
        exit(0);
    }
    /**
    * @tc.steps: step2. Rekey to unencrypt db and pagesize no changed
    * @tc.expected: step2. Return SQLITE_OK
    */
    CodecRekeyConfig rekeyCfg = {
        TEST_DB,
        { "aes-256-gcm", "SHA256", "KDF_SHA256", "01234567890123456789012345678901", 32, 5000, 4096 },
        { "aes-256-gcm", "SHA1", "KDF_SHA1", "01234567890123456789012345678901", 32, 5000, 4096 },
    };
    while (*shared != MULPROC_STEP_2) {
        usleep(20);
    }
    ASSERT_EQ(sqlite3_rekey_v3(&rekeyCfg), SQLITE_BUSY);
    *shared = MULPROC_STEP_3;

    waitpid(pid, 0, 0);
    ASSERT_NE(munmap(shared, mapSize), -1);
}


/**
 * @tc.name: LibSQLiteRekeyTest011
 * @tc.desc: Test db rekey page changed return busy when others process is open
 * @tc.type: FUNC
 */
HWTEST_F(LibSQLiteRekeyTest, LibSQLiteRekeyTest011, TestSize.Level0)
{
    // 1. 创建匿名内存映射 (可读写/共享)
    size_t mapSize = sizeof(int);
    int *shared = (int *)mmap(nullptr, mapSize, PROT_READ | PROT_WRITE,
        MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ASSERT_NE(shared, MAP_FAILED);
    *shared = MULPROC_STEP_1;
    pid_t pid = fork();
    ASSERT_GE(pid, 0);
    // child
    if (pid == 0) {
        sqlite3* db;
        /**
        * @tc.steps: step1. Open database and create table
        * @tc.expected: step1. Return SQLITE_OK
        */
        ASSERT_EQ(sqlite3_open(TEST_DB, &db), SQLITE_OK);
        CodecConfig config = {
            "aes-256-gcm", "SHA256", "KDF_SHA256", "01234567890123456789012345678901", 32, 5000, 4096
        };
        EncryptDbConfig(db, &config);
        PrepareDataForDb(db);
        *shared = MULPROC_STEP_2;
        while (*shared != MULPROC_STEP_3) {
            usleep(20);
        }
        sqlite3_close(db);
        exit(0);
    }
    /**
    * @tc.steps: step2. Rekey to unencrypt db and pagesize changed
    * @tc.expected: step2. Return SQLITE_OK
    */
    CodecRekeyConfig rekeyCfg = {
        TEST_DB,
        { "aes-256-gcm", "SHA256", "KDF_SHA256", "01234567890123456789012345678901", 32, 5000, 4096 },
        { "aes-256-gcm", "SHA1", "KDF_SHA1", "01234567890123456789012345678901", 32, 5000, 1024 },
    };
    while (*shared != MULPROC_STEP_2) {
        usleep(20);
    }
    ASSERT_EQ(sqlite3_rekey_v3(&rekeyCfg), SQLITE_BUSY);
    *shared = MULPROC_STEP_3;

    waitpid(pid, 0, 0);
    ASSERT_NE(munmap(shared, mapSize), -1);
}

/**
 * @tc.name: LibSQLiteRekeyTest012
 * @tc.desc: Test rekey abort can recover when open
 * @tc.type: FUNC
 */
HWTEST_F(LibSQLiteRekeyTest, LibSQLiteRekeyTest012, TestSize.Level0)
{
    sqlite3* db;
    /**
     * @tc.steps: step1. Open database and create table
     * @tc.expected: step1. Return SQLITE_OK
     */
    ASSERT_EQ(sqlite3_open(TEST_DB, &db), SQLITE_OK);
    CodecConfig config = {
        "aes-256-gcm", "SHA256", "KDF_SHA256", "01234567890123456789012345678901", 32, 5000, 4096
    };
    EncryptDbConfig(db, &config);
    PrepareDataForDb(db);
    sqlite3_close(db);
    /**
     * @tc.steps: step2. simulate db is export
     * @tc.expected: step2. Return SQLITE_OK
     */
    system("touch " TEST_DB REKEY_LOCK_SUFFIX);
    system("touch " TEST_DB REKEY_EXPORT_SUFFIX);
    /**
     * @tc.steps: step3. open and query
     * @tc.expected: step3. Return SQLITE_OK
     */
    ASSERT_EQ(sqlite3_open(TEST_DB, &db), SQLITE_OK);
    config = {
        "aes-256-gcm", "SHA256", "KDF_SHA256", "01234567890123456789012345678901", 32, 5000, 4096
    };
    EncryptDbConfig(db, &config);
    QueryData(db);
    sqlite3_close(db);
    /**
     * @tc.steps: step4. assert rekey relevant file is deleted
     * @tc.expected: step4. Return SQLITE_OK
     */
    ASSERT_EQ(access(TEST_DB REKEY_LOCK_SUFFIX, F_OK), -1)
     << "File unexpectedly exists: " << TEST_DB REKEY_LOCK_SUFFIX;
    ASSERT_EQ(access(TEST_DB REKEY_EXPORT_SUFFIX, F_OK), -1)
     << "File unexpectedly exists: " << TEST_DB REKEY_EXPORT_SUFFIX;
}

/**
 * @tc.name: LibSQLiteRekeyTest013
 * @tc.desc: Test rekey abort can recover when open
 * @tc.type: FUNC
 */
HWTEST_F(LibSQLiteRekeyTest, LibSQLiteRekeyTest013, TestSize.Level0)
{
    sqlite3* db;
    /**
     * @tc.steps: step1. Open database and create table
     * @tc.expected: step1. Return SQLITE_OK
     */
    ASSERT_EQ(sqlite3_open(TEST_DB, &db), SQLITE_OK);
    CodecConfig config = {
        "aes-256-gcm", "SHA256", "KDF_SHA256", "01234567890123456789012345678901", 32, 5000, 4096
    };
    EncryptDbConfig(db, &config);
    PrepareDataForDb(db);
    sqlite3_close(db);
    /**
     * @tc.steps: step2. simulate db is export
     * @tc.expected: step2. Return SQLITE_OK
     */
    system("touch " TEST_DB REKEY_LOCK_SUFFIX);
    system("touch " TEST_DB REKEY_EXPORT_SUFFIX);
    system("mv " TEST_DB " " TEST_DB REKEY_RENAME_SUFFIX);
    /**
     * @tc.steps: step3. open and query
     * @tc.expected: step3. Return SQLITE_OK
     */
    ASSERT_EQ(sqlite3_open(TEST_DB, &db), SQLITE_OK);
    config = {
        "aes-256-gcm", "SHA256", "KDF_SHA256", "01234567890123456789012345678901", 32, 5000, 4096
    };
    EncryptDbConfig(db, &config);
    QueryData(db);
    sqlite3_close(db);
    /**
     * @tc.steps: step4. assert rekey relevant file is deleted
     * @tc.expected: step4. Return SQLITE_OK
     */
    ASSERT_EQ(access(TEST_DB REKEY_LOCK_SUFFIX, F_OK), -1)
     << "File unexpectedly exists: " << TEST_DB REKEY_LOCK_SUFFIX;
    ASSERT_EQ(access(TEST_DB REKEY_EXPORT_SUFFIX, F_OK), -1)
     << "File unexpectedly exists: " << TEST_DB REKEY_EXPORT_SUFFIX;
    ASSERT_EQ(access(TEST_DB REKEY_RENAME_SUFFIX, F_OK), -1)
     << "File unexpectedly exists: " << TEST_DB REKEY_RENAME_SUFFIX;
}

/**
 * @tc.name: LibSQLiteRekeyTest014
 * @tc.desc: Test rekey with invalid argument
 * @tc.type: FUNC
 */
HWTEST_F(LibSQLiteRekeyTest, LibSQLiteRekeyTest014, TestSize.Level0)
{
    sqlite3* db;
    /**
     * @tc.steps: step1. Open database and create table
     * @tc.expected: step1. Return SQLITE_OK
     */
    ASSERT_EQ(sqlite3_open(TEST_DB, &db), SQLITE_OK);
    CodecConfig config = {
        "aes-256-gcm", "SHA1", "KDF_SHA1", "01234567890123456789012345678901", 32, 5000, 1024
    };
    EncryptDbConfig(db, &config);
    PrepareDataForDb(db);
    sqlite3_close(db);
    /**
     * @tc.steps: step2. Rekey with invalid cipher aes-128-gcm
     * @tc.expected: step2. Return SQLITE_OK
     */
    CodecRekeyConfig rekeyCfg = {
        TEST_DB,
        { "aes-256-gcm", "SHA1", "KDF_SHA1", "01234567890123456789012345678901", 32, 5000, 1024 },
        { "aes-128-gcm", "SHA256", "KDF_SHA256", "11234567890123456789012345678901", 32, 5000, 4096 }
    };
    ASSERT_EQ(sqlite3_rekey_v3(&rekeyCfg), SQLITE_ERROR);
    /**
     * @tc.steps: step3. Rekey with invalid iter
     * @tc.expected: step3. Return SQLITE_OK
     */
    rekeyCfg = {
        TEST_DB,
        { "aes-256-gcm", "SHA1", "KDF_SHA1", "01234567890123456789012345678901", 32, 5000, 1024 },
        { "aes-256-gcm", "SHA256", "KDF_SHA256", "11234567890123456789012345678901", 32, 0, 4096 }
    };
    ASSERT_EQ(sqlite3_rekey_v3(&rekeyCfg), SQLITE_ERROR);
    /**
     * @tc.steps: step4. Rekey with invalid sha algo SHA192
     * @tc.expected: step4. Return SQLITE_OK
     */
    rekeyCfg = {
        TEST_DB,
        { "aes-256-gcm", "SHA1", "KDF_SHA1", "01234567890123456789012345678901", 32, 5000, 1024 },
        { "aes-256-gcm", "SHA192", "SHA192", "11234567890123456789012345678901", 32, 5000, 4096 }
    };
    ASSERT_EQ(sqlite3_rekey_v3(&rekeyCfg), SQLITE_ERROR);
    /**
     * @tc.steps: step5. Rekey with invalid cipher aes-128-gcm
     * @tc.expected: step5. Return SQLITE_OK
     */
    rekeyCfg = {
        TEST_DB,
        { "aes-128-gcm", "SHA1", "KDF_SHA1", "01234567890123456789012345678901", 32, 5000, 1024 },
        { "aes-256-gcm", "SHA256", "SHA256", "11234567890123456789012345678901", 32, 5000, 4096 }
    };
    ASSERT_EQ(sqlite3_rekey_v3(&rekeyCfg), SQLITE_ERROR);
}

}  // namespace Test