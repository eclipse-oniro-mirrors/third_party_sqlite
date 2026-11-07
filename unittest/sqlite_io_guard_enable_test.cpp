/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
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
#include <unistd.h>

#include "sqlite3sym.h"

using namespace testing::ext;
using namespace std;

#define TEST_DIR "./sqlitetest"
#define TEST_DB (TEST_DIR "/test.db")
#define TEST_DB_WAL (TEST_DIR "/test.db-dwr")
#define TEST_DB_SHM (TEST_DIR "/test.db-shm")

/*
 * This binary intentionally does NOT enable io_guard in SetUpTestCase: it
 * verifies the process-level switch semantics (AC-3) - boolean validation
 * (INV-3) and latch-once-open (INV-2) - starting from the default-disabled
 * state.  It is built as a separate executable so that its config calls are
 * not disturbed by the shared process-level latch state of libsqlittest.
 */
class SQLiteIoGuardEnableTest : public testing::Test {
public:
    static void SetUpTestCase(void);
    static void TearDownTestCase(void);
    void SetUp();
    void TearDown();
};

void SQLiteIoGuardEnableTest::SetUpTestCase(void)
{
    mkdir(TEST_DIR, 0770);
}

void SQLiteIoGuardEnableTest::TearDownTestCase(void)
{
}

void SQLiteIoGuardEnableTest::SetUp(void)
{
    unlink(TEST_DB);
    unlink(TEST_DB_WAL);
    unlink(TEST_DB_SHM);
}

void SQLiteIoGuardEnableTest::TearDown(void)
{
    unlink(TEST_DB);
    unlink(TEST_DB_WAL);
    unlink(TEST_DB_SHM);
}

/**
 * @tc.name: Lib_SQLite_Io_Guard_Enable_Test_001
 * @tc.desc: Illegal boolean (2/-1/100) is rejected and the switch state is unchanged.
 * @tc.type: FUNC
 */
HWTEST_F(SQLiteIoGuardEnableTest, Lib_SQLite_Io_Guard_Enable_Test_001, TestSize.Level1)
{
    /* 2: reject before latching; the switch must stay disabled afterwards. */
    EXPECT_EQ(sqlite3_config(SQLITE_CONFIG_IO_GUARD, 2), SQLITE_ERROR);
    EXPECT_EQ(sqlite3_config(SQLITE_CONFIG_IO_GUARD, -1), SQLITE_ERROR);
    EXPECT_EQ(sqlite3_config(SQLITE_CONFIG_IO_GUARD, 100), SQLITE_ERROR);

    /* After a legal disable, the switch is still usable (not latched). */
    EXPECT_EQ(sqlite3_config(SQLITE_CONFIG_IO_GUARD, 0), SQLITE_OK);
    EXPECT_EQ(sqlite3_config(SQLITE_CONFIG_IO_GUARD, 0), SQLITE_OK);
}

/**
 * @tc.name: Lib_SQLite_Io_Guard_Enable_Test_002
 * @tc.desc: Legal 0/1 toggles succeed; enabling and disabling round-trip.
 * @tc.type: FUNC
 */
HWTEST_F(SQLiteIoGuardEnableTest, Lib_SQLite_Io_Guard_Enable_Test_002, TestSize.Level1)
{
    EXPECT_EQ(sqlite3_config(SQLITE_CONFIG_IO_GUARD, 1), SQLITE_OK);
    EXPECT_EQ(sqlite3_config(SQLITE_CONFIG_IO_GUARD, 0), SQLITE_OK);
    EXPECT_EQ(sqlite3_config(SQLITE_CONFIG_IO_GUARD, 1), SQLITE_OK);
    EXPECT_EQ(sqlite3_config(SQLITE_CONFIG_IO_GUARD, 0), SQLITE_OK);
}

/**
 * @tc.name: Lib_SQLite_Io_Guard_Enable_Test_003
 * @tc.desc: After any descriptor is opened, config is latched and rejected.
 * @tc.type: FUNC
 */
HWTEST_F(SQLiteIoGuardEnableTest, Lib_SQLite_Io_Guard_Enable_Test_003, TestSize.Level1)
{
    EXPECT_EQ(sqlite3_config(SQLITE_CONFIG_IO_GUARD, 1), SQLITE_OK);

    sqlite3 *db = NULL;
    EXPECT_EQ(sqlite3_open_v2(TEST_DB, &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL), SQLITE_OK);
    ASSERT_NE(db, nullptr);
    EXPECT_EQ(sqlite3_exec(db, "CREATE TABLE IF NOT EXISTS t(a INTEGER);", NULL, NULL, NULL), SQLITE_OK);
    EXPECT_EQ(sqlite3_close(db), SQLITE_OK);

    /* Latched: any further config (enable or disable) is rejected. */
    EXPECT_EQ(sqlite3_config(SQLITE_CONFIG_IO_GUARD, 0), SQLITE_ERROR);
    EXPECT_EQ(sqlite3_config(SQLITE_CONFIG_IO_GUARD, 1), SQLITE_ERROR);
    EXPECT_EQ(sqlite3_config(SQLITE_CONFIG_IO_GUARD, 2), SQLITE_ERROR);
}

/**
 * @tc.name: Lib_SQLite_Io_Guard_Enable_Test_004
 * @tc.desc: The switch is sticky (INV-2): once any descriptor has been opened in
 *           this process (with or without an explicit config first), every later
 *           config call is rejected, including a fresh enable attempt.
 * @tc.type: FUNC
 */
HWTEST_F(SQLiteIoGuardEnableTest, Lib_SQLite_Io_Guard_Enable_Test_004, TestSize.Level1)
{
    /* The process-level latch is shared across this binary: any earlier open
     * (e.g. in this fixture's own open below, or in a prior fixture/test) sets
     * it, after which config must be rejected no matter the requested value. */
    sqlite3 *db = NULL;
    EXPECT_EQ(sqlite3_open_v2(TEST_DB, &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL), SQLITE_OK);
    ASSERT_NE(db, nullptr);
    EXPECT_EQ(sqlite3_exec(db, "CREATE TABLE IF NOT EXISTS t(a INTEGER);", NULL, NULL, NULL), SQLITE_OK);
    EXPECT_EQ(sqlite3_close(db), SQLITE_OK);

    /* Once latched, enabling (or disabling) afterwards is rejected. */
    EXPECT_EQ(sqlite3_config(SQLITE_CONFIG_IO_GUARD, 1), SQLITE_ERROR);
    EXPECT_EQ(sqlite3_config(SQLITE_CONFIG_IO_GUARD, 0), SQLITE_ERROR);
}
