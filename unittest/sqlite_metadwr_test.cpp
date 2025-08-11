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
using namespace std;

#define TEST_DIR "./sqlitetest"
#define TEST_DB (TEST_DIR "/test.db")
#define TEST_DWR (TEST_DIR "/test.db-dwr")

static void UtSqliteLogPrint(const void *data, int err, const char *msg)
{
    std::cout << "LibSQLiteMetaDwrTest SQLite xLog err:" << err << ", msg:" << msg << std::endl;
}

class LibSQLiteMetaDwrTest : public testing::Test {
public:
    static void SetUpTestCase(void);
    static void TearDownTestCase(void);
    void SetUp();
    void TearDown();
};

void LibSQLiteMetaDwrTest::SetUpTestCase(void)
{
    // permission 0770
    mkdir(TEST_DIR, 0770);
}

void LibSQLiteMetaDwrTest::TearDownTestCase(void)
{
}

void LibSQLiteMetaDwrTest::SetUp(void)
{
    unlink(TEST_DB);
    unlink(TEST_DWR);
    sqlite3_config(SQLITE_CONFIG_LOG, &UtSqliteLogPrint, NULL);
}

void LibSQLiteMetaDwrTest::TearDown(void)
{
    sqlite3_config(SQLITE_CONFIG_LOG, NULL, NULL);
}

/**
 * @tc.name: Lib_SQLite_Meta_Dwr_Test_001
 * @tc.desc: Test write meta dwr pages out of range.
 * @tc.type: FUNC
 */
HWTEST_F(LibSQLiteMetaDwrTest, Lib_SQLite_Meta_Dwr_Test_001, TestSize.Level2)
{
    /**
     * @tc.steps: step1. Open database
     * @tc.expected: step1. Return SQLITE_OK
     */
    sqlite3 *db = NULL;
    EXPECT_EQ(sqlite3_open(TEST_DB, &db), SQLITE_OK);
    /**
     * @tc.steps: step2. Enable meta dwr
     * @tc.expected: step2. Execute successfully
     */
    EXPECT_EQ(sqlite3_exec(db, "pragma meta_double_write=enabled", NULL, NULL, NULL), SQLITE_OK);
    /**
     * @tc.steps: step3. Create tables to reach max meta dwr pages
     * @tc.expected: step3. Execute successfully
     */
    std::string tableNamePrefix("TestTableInfoForMetaDwr");
    // table tail 4000
    tableNamePrefix += std::string(4000, 'a');
    // meta dwr pages correspond to 240 table info
    for (int i = 0; i < 240; i++) {
        std::string cteateTable = std::string("CREATE TABLE ") + tableNamePrefix + to_string(i) +
            "(id INTEGER PRIMARY KEY, name TEXT);";
        EXPECT_EQ(sqlite3_exec(db, cteateTable.c_str(), NULL, NULL, NULL), SQLITE_OK);
    }
    sqlite3_close(db);
    /**
     * @tc.steps: step4. Expect meta dwr file limit size
     * @tc.expected: step4. Execute successfully
     */
    struct stat statbuf;
    EXPECT_EQ(stat(TEST_DWR, &statbuf), 0);
    // max 1001 pages and page size 4096
    EXPECT_EQ(statbuf.st_size, 1001 * 4096);
}
