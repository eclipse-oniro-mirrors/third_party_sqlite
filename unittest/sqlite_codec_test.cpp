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

#include "sqlite3sym.h"

using namespace testing::ext;
using namespace std;

#define TEST_DIR "./sqlitetest"
#define TEST_DB (TEST_DIR "/test.db")
#define CORRUPT_DATA "SQLITE_CORRUPT_PAGE_SIGNATURE"
namespace Test {
static void UtSqliteLogPrint(const void *data, int err, const char *msg)
{
    std::cout << "LibSQLiteCodecTest SQLite xLog err:" << err << ", msg:" << msg << std::endl;
}

class LibSQLiteCodecTest : public testing::Test {
public:
    static void SetUpTestCase(void);
    static void TearDownTestCase(void);
    void SetUp();
    void TearDown();
};

void LibSQLiteCodecTest::SetUpTestCase(void)
{
    sqlite3_config(SQLITE_CONFIG_LOG, &UtSqliteLogPrint, NULL);
    // permission 0770
    mkdir(TEST_DIR, 0770);
}

void LibSQLiteCodecTest::TearDownTestCase(void)
{
    sqlite3_config(SQLITE_CONFIG_LOG, NULL, NULL);
}

void LibSQLiteCodecTest::SetUp(void)
{
}

void LibSQLiteCodecTest::TearDown(void)
{
    unlink(TEST_DB);
}

static void SetEncryptDb(sqlite3* db)
{
    const char *key = "01234567890123456789012345678901";
    // key size 32
    ASSERT_EQ(sqlite3_key(db, key, 32), SQLITE_OK);
}

static void CodecCorruptTest(int offset)
{
    sqlite3* db;
    char* errMsg = nullptr;
    /**
     * @tc.steps: step1. Open database and create table
     * @tc.expected: step1. Return SQLITE_OK
     */
    ASSERT_EQ(sqlite3_open(TEST_DB, &db), SQLITE_OK);
    SetEncryptDb(db);
    const char* sql = "CREATE TABLE test_data(id INTEGER PRIMARY KEY AUTOINCREMENT, data TEXT);";
    ASSERT_EQ(sqlite3_exec(db, sql, nullptr, nullptr, &errMsg), SQLITE_OK)
        << "create db failed: " << (errMsg ? errMsg : "unknown error");
    /**
     * @tc.steps: step2.insert data
     * @tc.expected: step2. Return SQLITE_OK
     */
    sqlite3_stmt* stmt;
    const char* insertSQL = "INSERT INTO test_data (data) VALUES (?);";
    ASSERT_EQ(sqlite3_prepare_v2(db, insertSQL, -1, &stmt, nullptr), SQLITE_OK);
    int valueLength = 50;
    std::string valueStr(valueLength, 'a');
    // repeat 50 count
    for (int i = 0; i < 50; ++i) {
        ASSERT_EQ(sqlite3_bind_text(stmt, 1, valueStr.c_str(), -1, SQLITE_STATIC), SQLITE_OK);
        ASSERT_EQ(sqlite3_step(stmt), SQLITE_DONE);
        sqlite3_reset(stmt);
    }
    sqlite3_finalize(stmt);
    sqlite3_close(db);

    /**
     * @tc.steps: step3.modify page data
     * @tc.expected: step3. Return OK
     */
    int fd = open(TEST_DB, O_RDWR);
    ASSERT_GE(fd, 0) << "cann't open file ";

    lseek(fd, offset, SEEK_SET);
    ssize_t written = write(fd, CORRUPT_DATA, strlen(CORRUPT_DATA));
    ASSERT_EQ(written, static_cast<ssize_t>(strlen(CORRUPT_DATA)))
        << "write file failed: " << strerror(errno);
    close(fd);
}

/**
 * @tc.name: Lib_SQLite_CODEC_Test_001
 * @tc.desc: Test corrupt head page of encrypted db
 * @tc.type: FUNC
 */
HWTEST_F(LibSQLiteCodecTest, Lib_SQLite_CODEC_Test_001, TestSize.Level0)
{
    CodecCorruptTest(0);

    /**
     * @tc.steps: step4.query db
     * @tc.expected: step4. Return expRet
     */
    sqlite3* db = nullptr;
    ASSERT_EQ(sqlite3_open(TEST_DB, &db), SQLITE_OK);
    SetEncryptDb(db);
    const char* query = "SELECT count(*) FROM test_data;";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, query, -1, &stmt, nullptr);
    EXPECT_EQ(rc, SQLITE_NOTADB) << "expect " << SQLITE_NOTADB << " but return "
        << rc << " - " << sqlite3_errmsg(db);
    /**
     * @tc.steps: step5.clear resource
     * @tc.expected: step5. Return OK
     */
    if (stmt) sqlite3_finalize(stmt);
    sqlite3_close(db);
}

/**
 * @tc.name: Lib_SQLite_CODEC_Test_002
 * @tc.desc: Test corrupt other page of encrypted db
 * @tc.type: FUNC
 */
HWTEST_F(LibSQLiteCodecTest, Lib_SQLite_CODEC_Test_002, TestSize.Level0)
{
    // 4096 offset of db
    CodecCorruptTest(4096);

    /**
     * @tc.steps: step4.query db
     * @tc.expected: step4. Return expRet
     */
    sqlite3* db = nullptr;
    ASSERT_EQ(sqlite3_open(TEST_DB, &db), SQLITE_OK);
    SetEncryptDb(db);
    const char* query = "SELECT count(*) FROM test_data;";
    sqlite3_stmt* stmt = nullptr;
    ASSERT_EQ(sqlite3_prepare_v2(db, query, -1, &stmt, nullptr), SQLITE_OK);
    int rc = sqlite3_step(stmt);
    EXPECT_EQ(rc, SQLITE_CORRUPT) << "expect " << SQLITE_CORRUPT << " but return "
        << rc << " - " << sqlite3_errmsg(db);
    /**
     * @tc.steps: step5.clear resource
     * @tc.expected: step5. Return OK
     */
    if (stmt) sqlite3_finalize(stmt);
    sqlite3_close(db);
}

}  // namespace Test