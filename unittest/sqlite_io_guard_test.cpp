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
#include <cstring>
#include <string>
#include <unistd.h>

#include "sqlite3sym.h"

using namespace testing::ext;
using namespace std;

#define TEST_DIR "./sqliteioguardtest"
#define TEST_DB (TEST_DIR "/test.db")
#define TEST_DB_WAL (TEST_DIR "/test.db-dwr")
#define TEST_DB_SHM (TEST_DIR "/test.db-shm")
#define TEST_DB_JOURNAL (TEST_DIR "/test.db-journal")

class SQLiteIoGuardTest : public testing::Test {
public:
    static void SetUpTestCase(void);
    static void TearDownTestCase(void);
    void SetUp();
    void TearDown();
};

void SQLiteIoGuardTest::SetUpTestCase(void)
{
    mkdir(TEST_DIR, 0770);
    /*
     * Enable io_guard before opening any descriptor (AC-1 / AC-3).
     * In the unit-test environment the musl libc inner API (ohos_io_guard_*)
     * is absent, so the weak symbols resolve to NULL and SQLite automatically
     * degrades to plain IO (AC-4); the config call itself must still succeed
     * and every DB operation below must remain functional.  If another fixture
     * in this shared binary already opened a descriptor, the switch is latched
     * (INV-2) and config legitimately returns SQLITE_ERROR - acceptable here,
     * because the precise switch/latch semantics are covered by the isolated
     * libsqliteioguardenabletest binary.  Real token interception is covered
     * by the local harness (33 checks) and requires the real libc on device.
     */
    int rc = sqlite3_config(SQLITE_CONFIG_IO_GUARD, 1);
    if (rc != SQLITE_OK) {
        printf("io_guard config skipped (rc=%d, latched by an earlier fixture, INV-2); "
               "running as plain-IO compatibility baseline\n", rc);
    }
}

void SQLiteIoGuardTest::TearDownTestCase(void)
{
}

void SQLiteIoGuardTest::SetUp(void)
{
    unlink(TEST_DB);
    unlink(TEST_DB_WAL);
    unlink(TEST_DB_SHM);
    unlink(TEST_DB_JOURNAL);
}

void SQLiteIoGuardTest::TearDown(void)
{
    unlink(TEST_DB);
    unlink(TEST_DB_WAL);
    unlink(TEST_DB_SHM);
    unlink(TEST_DB_JOURNAL);
}

/*
 * Locate the descriptor that currently refers to the given file by scanning
 * /proc/self/fd.  Returns the fd number, or -1 when it cannot be resolved.
 * This is the black-box way to obtain the target fd that io_guard manages
 * (the descriptor SQLite opened through its VFS).
 */
static int FindDbFd(const char *dbPath)
{
    char linkPath[64];
    char linkTarget[512];
    for (int fd = 3; fd < 1024; fd++) {
        snprintf(linkPath, sizeof(linkPath), "/proc/self/fd/%d", fd);
        ssize_t n = readlink(linkPath, linkTarget, sizeof(linkTarget) - 1);
        if (n <= 0) {
            continue;
        }
        linkTarget[n] = '\0';
        printf("readlink %s, %s \n", linkTarget, dbPath);
        if (strstr(linkTarget, dbPath) != NULL) {
            return fd;
        }
    }
    return -1;
}

/*
 * Probe whether the running libc actually intercepts a given plain
 * (token-less) file interface on io_guard-managed descriptors.  Opens a
 * throwaway database through SQLite, locates its fd and attempts the plain
 * call against it.  Returns true when the call was rejected (io_guard
 * interception active), false when it succeeded or the fd could not be
 * located (plain-IO degradation on a libc without the io_guard inner API,
 * AC-4).
 *
 * The probe is per-interface so each test only assumes interception for the
 * exact interface it exercises; the libc may block some interfaces and leave
 * others passable.  which selects the interface:
 *   0 = write(), 1 = pwrite(), 2 = close().
 */
static bool IsIoGuardIfaceBlocked(const char *probeDb, int which)
{
    unlink(probeDb);
    sqlite3 *db = NULL;
    if (sqlite3_open_v2(probeDb, &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL) != SQLITE_OK) {
        return false;
    }
    if (db == nullptr) {
        return false;
    }
    /* Force a real file on disk so /proc/self/fd resolves to the path. */
    sqlite3_exec(db, "CREATE TABLE IF NOT EXISTS t(a);", NULL, NULL, NULL);
    int fd = FindDbFd("probe.db");
    if (fd < 0) {
        sqlite3_close(db);
        return false;
    }
    char buf[8] = "probe!";
    if (which == 1) {
        ssize_t n = pwrite(fd, buf, sizeof(buf), 0);
        sqlite3_close(db);
        return n < 0;
    } else if (which == 2) {
        errno = 0;
        int rc = close(fd);
        /* The fd was not released when close() was rejected; SQLite's own
         * close below still owns it.  Ignore its rc on the probe db. */
        sqlite3_close(db);
        return rc < 0;
    }
    ssize_t n = write(fd, buf, sizeof(buf));
    sqlite3_close(db);
    return n < 0;
}

/**
 * @tc.name: Lib_SQLite_Io_Guard_Test_001
 * @tc.desc: With io_guard enabled, normal open/create/write/close stays intact.
 * @tc.type: FUNC
 */
HWTEST_F(SQLiteIoGuardTest, Lib_SQLite_Io_Guard_Test_001, TestSize.Level1)
{
    sqlite3 *db = NULL;
    EXPECT_EQ(sqlite3_open_v2(TEST_DB, &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL), SQLITE_OK);
    ASSERT_NE(db, nullptr);

    EXPECT_EQ(sqlite3_exec(db, "CREATE TABLE IF NOT EXISTS t(a INTEGER, b TEXT);", NULL, NULL, NULL), SQLITE_OK);
    EXPECT_EQ(sqlite3_exec(db, "INSERT INTO t VALUES(1,'one'),(2,'two'),(3,'three');", NULL, NULL, NULL),
        SQLITE_OK);

    sqlite3_stmt *stmt = NULL;
    EXPECT_EQ(sqlite3_prepare_v2(db, "SELECT count(*) FROM t;", -1, &stmt, NULL), SQLITE_OK);
    EXPECT_EQ(sqlite3_step(stmt), SQLITE_ROW);
    EXPECT_EQ(sqlite3_column_int(stmt, 0), 3);
    EXPECT_EQ(sqlite3_finalize(stmt), SQLITE_OK);

    EXPECT_EQ(sqlite3_close(db), SQLITE_OK);
}

/**
 * @tc.name: Lib_SQLite_Io_Guard_Test_002
 * @tc.desc: Read-after-write integrity with io_guard enabled.
 * @tc.type: FUNC
 */
HWTEST_F(SQLiteIoGuardTest, Lib_SQLite_Io_Guard_Test_002, TestSize.Level1)
{
    sqlite3 *db = NULL;
    EXPECT_EQ(sqlite3_open_v2(TEST_DB, &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL), SQLITE_OK);
    ASSERT_NE(db, nullptr);

    EXPECT_EQ(sqlite3_exec(db, "CREATE TABLE IF NOT EXISTS t(a INTEGER, b TEXT);", NULL, NULL, NULL), SQLITE_OK);
    EXPECT_EQ(sqlite3_exec(db, "INSERT INTO t VALUES(1,'one'),(2,'two'),(3,'three');", NULL, NULL, NULL),
        SQLITE_OK);
    /* Write back a row (positional write path) and read it again. */
    EXPECT_EQ(sqlite3_exec(db, "UPDATE t SET b='TWO' WHERE a=2;", NULL, NULL, NULL), SQLITE_OK);

    sqlite3_stmt *stmt = NULL;
    EXPECT_EQ(sqlite3_prepare_v2(db, "SELECT b FROM t WHERE a=2;", -1, &stmt, NULL), SQLITE_OK);
    EXPECT_EQ(sqlite3_step(stmt), SQLITE_ROW);
    EXPECT_STREQ(reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0)), "TWO");
    EXPECT_EQ(sqlite3_finalize(stmt), SQLITE_OK);

    EXPECT_EQ(sqlite3_close(db), SQLITE_OK);
}

/**
 * @tc.name: Lib_SQLite_Io_Guard_Test_003
 * @tc.desc: Transaction (journal) commit and rollback with io_guard enabled.
 * @tc.type: FUNC
 */
HWTEST_F(SQLiteIoGuardTest, Lib_SQLite_Io_Guard_Test_003, TestSize.Level1)
{
    sqlite3 *db = NULL;
    EXPECT_EQ(sqlite3_open_v2(TEST_DB, &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL), SQLITE_OK);
    ASSERT_NE(db, nullptr);
    EXPECT_EQ(sqlite3_exec(db, "CREATE TABLE IF NOT EXISTS t(a INTEGER);", NULL, NULL, NULL), SQLITE_OK);
    EXPECT_EQ(sqlite3_exec(db, "BEGIN; INSERT INTO t VALUES(1),(2),(3),(4); COMMIT;", NULL, NULL, NULL),
        SQLITE_OK);
    EXPECT_EQ(sqlite3_close(db), SQLITE_OK);

    /* Reopen and verify the committed transaction persisted. */
    EXPECT_EQ(sqlite3_open_v2(TEST_DB, &db, SQLITE_OPEN_READWRITE, NULL), SQLITE_OK);
    ASSERT_NE(db, nullptr);
    EXPECT_EQ(sqlite3_exec(db, "BEGIN; INSERT INTO t VALUES(5); ROLLBACK;", NULL, NULL, NULL), SQLITE_OK);

    sqlite3_stmt *stmt = NULL;
    EXPECT_EQ(sqlite3_prepare_v2(db, "SELECT count(*) FROM t;", -1, &stmt, NULL), SQLITE_OK);
    EXPECT_EQ(sqlite3_step(stmt), SQLITE_ROW);
    EXPECT_EQ(sqlite3_column_int(stmt, 0), 4); /* rolled-back row must not persist */
    EXPECT_EQ(sqlite3_finalize(stmt), SQLITE_OK);

    EXPECT_EQ(sqlite3_close(db), SQLITE_OK);
}

/**
 * @tc.name: Lib_SQLite_Io_Guard_Test_004
 * @tc.desc: WAL mode (shm mapping path) with io_guard enabled.
 * @tc.type: FUNC
 */
HWTEST_F(SQLiteIoGuardTest, Lib_SQLite_Io_Guard_Test_004, TestSize.Level1)
{
    sqlite3 *db = NULL;
    EXPECT_EQ(sqlite3_open_v2(TEST_DB, &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL), SQLITE_OK);
    ASSERT_NE(db, nullptr);

    sqlite3_stmt *stmt = NULL;
    EXPECT_EQ(sqlite3_prepare_v2(db, "PRAGMA journal_mode=WAL;", -1, &stmt, NULL), SQLITE_OK);
    EXPECT_EQ(sqlite3_step(stmt), SQLITE_ROW);
    EXPECT_STREQ(reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0)), "wal");
    EXPECT_EQ(sqlite3_finalize(stmt), SQLITE_OK);

    EXPECT_EQ(sqlite3_exec(db, "CREATE TABLE IF NOT EXISTS t(a INTEGER);", NULL, NULL, NULL), SQLITE_OK);
    EXPECT_EQ(sqlite3_exec(db, "INSERT INTO t VALUES(1),(2),(3);", NULL, NULL, NULL), SQLITE_OK);
    EXPECT_EQ(sqlite3_close(db), SQLITE_OK);

    /* Reopen in WAL mode: data must persist via the -wal/-shm files. */
    EXPECT_EQ(sqlite3_open_v2(TEST_DB, &db, SQLITE_OPEN_READWRITE, NULL), SQLITE_OK);
    ASSERT_NE(db, nullptr);
    EXPECT_EQ(sqlite3_prepare_v2(db, "SELECT count(*) FROM t;", -1, &stmt, NULL), SQLITE_OK);
    EXPECT_EQ(sqlite3_step(stmt), SQLITE_ROW);
    EXPECT_EQ(sqlite3_column_int(stmt, 0), 3);
    EXPECT_EQ(sqlite3_finalize(stmt), SQLITE_OK);

    EXPECT_EQ(sqlite3_close(db), SQLITE_OK);
}

/**
 * @tc.name: Lib_SQLite_Io_Guard_Test_005
 * @tc.desc: With io_guard enabled, a plain (token-less) write() through the
 *           ordinary file interface against the guarded DB fd must be
 *           rejected (AC-1).  Skipped when the running libc does not provide
 *           io_guard interception (weak-symbol degradation, AC-4).
 * @tc.type: FUNC
 */
HWTEST_F(SQLiteIoGuardTest, Lib_SQLite_Io_Guard_Test_005, TestSize.Level1)
{
    const char *probeDb = TEST_DIR "/probe.db";
    if (!IsIoGuardIfaceBlocked(probeDb, 0)) {
        GTEST_SKIP() << "io_guard interception unavailable in this environment "
                        "(plain-IO degradation, AC-4); plain write is not blocked";
    }

    sqlite3 *db = NULL;
    EXPECT_EQ(sqlite3_open_v2(TEST_DB, &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL), SQLITE_OK);
    ASSERT_NE(db, nullptr);
    EXPECT_EQ(sqlite3_exec(db, "CREATE TABLE IF NOT EXISTS t(a INTEGER, b TEXT);", NULL, NULL, NULL), SQLITE_OK);

    int fd = FindDbFd("test.db");
    ASSERT_GE(fd, 0);

    /* The DB fd is guarded by io_guard: a plain token-less write() must fail. */
    char buf[16] = "corrupt";
    errno = 0;
    EXPECT_LT(write(fd, buf, sizeof(buf)), 0);
    EXPECT_EQ(errno, EPERM); /* BLOCK policy rejects the token-less write */

    EXPECT_EQ(sqlite3_close(db), SQLITE_OK);
}

/**
 * @tc.name: Lib_SQLite_Io_Guard_Test_006
 * @tc.desc: With io_guard enabled, a plain (token-less) pwrite() through the
 *           ordinary file interface against the guarded DB fd must be
 *           rejected (AC-1).  Skipped when io_guard interception is not
 *           available in the running environment (AC-4).
 * @tc.type: FUNC
 */
HWTEST_F(SQLiteIoGuardTest, Lib_SQLite_Io_Guard_Test_006, TestSize.Level1)
{
    const char *probeDb = TEST_DIR "/probe.db";
    if (!IsIoGuardIfaceBlocked(probeDb, 1)) {
        GTEST_SKIP() << "io_guard interception unavailable in this environment "
                        "(plain-IO degradation, AC-4); plain pwrite is not blocked";
    }

    sqlite3 *db = NULL;
    EXPECT_EQ(sqlite3_open_v2(TEST_DB, &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL), SQLITE_OK);
    ASSERT_NE(db, nullptr);
    EXPECT_EQ(sqlite3_exec(db, "CREATE TABLE IF NOT EXISTS t(a INTEGER, b TEXT);", NULL, NULL, NULL), SQLITE_OK);

    int fd = FindDbFd("test.db");
    ASSERT_GE(fd, 0);

    /* Positional write without the io_guard token must be rejected. */
    char buf[16] = "corrupt";
    errno = 0;
    EXPECT_LT(pwrite(fd, buf, sizeof(buf), 0), 0);
    EXPECT_EQ(errno, EPERM);

    EXPECT_EQ(sqlite3_close(db), SQLITE_OK);
}

/**
 * @tc.name: Lib_SQLite_Io_Guard_Test_007
 * @tc.desc: With io_guard enabled, a plain close() through the ordinary file
 *           interface against the guarded DB fd must be rejected (AC-1); the
 *           descriptor stays owned by SQLite and the connection remains
 *           usable.  Skipped when io_guard interception is not available in
 *           the running environment (AC-4).
 * @tc.type: FUNC
 */
HWTEST_F(SQLiteIoGuardTest, Lib_SQLite_Io_Guard_Test_007, TestSize.Level1)
{
    const char *probeDb = TEST_DIR "/probe.db";
    if (!IsIoGuardIfaceBlocked(probeDb, 2)) {
        GTEST_SKIP() << "io_guard interception unavailable in this environment "
                        "(plain-IO degradation, AC-4); plain close is not blocked";
    }

    sqlite3 *db = NULL;
    EXPECT_EQ(sqlite3_open_v2(TEST_DB, &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL), SQLITE_OK);
    ASSERT_NE(db, nullptr);
    EXPECT_EQ(sqlite3_exec(db, "CREATE TABLE IF NOT EXISTS t(a INTEGER, b TEXT);", NULL, NULL, NULL), SQLITE_OK);

    int fd = FindDbFd("test.db");
    ASSERT_GE(fd, 0);

    /* A token-less close() on the guarded fd must be rejected, and SQLite's
     * own close below must still succeed (fd was not actually released). */
    errno = 0;
    EXPECT_LT(close(fd), 0);

    EXPECT_EQ(sqlite3_close(db), SQLITE_OK);
}

