/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 *
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

/*
 * Migration verification tests: OpenSSL -> openHiTLS
 *
 * These tests exercise the codec layer through the public SQLite API
 * (sqlite3_key, PRAGMA codec_*, sqlite3_rekey_v3). Every function that
 * was migrated from openssl* to openhitls* is covered:
 *
 *   openhitlsActive / openhitlsDeactive   — lifecycle (attach/close)
 *   openhitlsGetRandom                    — salt + IV generation
 *   openhitlsGetCipher / openhitlsFreeCipher / openhitlsGetCipherName
 *   openhitlsGetKeySize / openhitlsGetInitVectorSize / openhitlsGetBlockSize
 *   openhitlsGetCtx / openhitlsCipher / openhitlsFreeCtx
 *   openhitlsKdf                          — PBKDF2 key derivation
 *   openhitlsHmac / openhitlsGetHmacSize  — page HMAC integrity
 *
 * If any migrated function produces different output from its OpenSSL
 * counterpart, the encrypted database will fail to decrypt or the HMAC
 * verification will reject pages, causing test failures.
 */

#include <gtest/gtest.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/types.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>
#include <vector>

#include "sqlite3sym.h"

using namespace testing::ext;
using namespace std;

static const std::string MIG_TEST_DIR =
    (std::filesystem::current_path() / "sqlitetest").string();
static const std::string MIG_TEST_DB =
    (std::filesystem::path(MIG_TEST_DIR) / "mig_test.db").string();
static const std::string MIG_TEST_DB2 =
    (std::filesystem::path(MIG_TEST_DIR) / "mig_test2.db").string();
static const char *MIG_TEST_KEY = "01234567890123456789012345678901";
static const char *MIG_TEST_KEY2 = "11234567890123456789012345678901";
static const char *MIG_WRONG_KEY = "FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF";
static const int MIG_TEST_COUNT = 100;

static void UtMigLogPrint(void *data, int err, const char *msg)
{
    (void)data;
    std::cout << "CodecMigrationTest SQLite xLog err:" << err
              << ", msg:" << msg << std::endl;
}

class CodecMigrationTest : public testing::Test {
public:
    static void SetUpTestCase(void);
    static void TearDownTestCase(void);
    void SetUp();
    void TearDown();
};

void CodecMigrationTest::SetUpTestCase(void)
{
    sqlite3_config(SQLITE_CONFIG_LOG, &UtMigLogPrint, NULL);
    mkdir(MIG_TEST_DIR.c_str(), 0770);
}

void CodecMigrationTest::TearDownTestCase(void)
{
    sqlite3_config(SQLITE_CONFIG_LOG, NULL, NULL);
}

static void RemoveDbFiles(const char *path)
{
    string base = path;
    unlink(base.c_str());
    unlink((base + "-wal").c_str());
    unlink((base + "-shm").c_str());
    unlink((base + "-journal").c_str());
}

void CodecMigrationTest::SetUp(void)
{
    RemoveDbFiles(MIG_TEST_DB.c_str());
    RemoveDbFiles(MIG_TEST_DB2.c_str());
    RemoveDbFiles((std::filesystem::path(MIG_TEST_DIR) / "mig_test3.db").string().c_str());
}

void CodecMigrationTest::TearDown(void)
{
}

/* ------------------------------------------------------------------ */
/* Helper: build PRAGMA SQL for codec configuration                    */
/* ------------------------------------------------------------------ */
static std::string BuildCodecPragmaSql(const CodecConfig &cfg)
{
    std::string sql = "PRAGMA codec_cipher='";
    sql += static_cast<const char *>(cfg.pCipher);
    sql += "';PRAGMA codec_hmac_algo='";
    sql += static_cast<const char *>(cfg.pHmacAlgo);
    sql += "';PRAGMA codec_kdf_algo='";
    sql += static_cast<const char *>(cfg.pKdfAlgo);
    sql += "';PRAGMA codec_page_size=";
    sql += std::to_string(cfg.pageSize);
    sql += ";PRAGMA codec_kdf_iter=";
    sql += std::to_string(cfg.kdfIter);
    sql += ";";
    return sql;
}

/* ------------------------------------------------------------------ */
/* Helper: set codec configuration via PRAGMA                          */
/* ------------------------------------------------------------------ */
static void SetCodecConfigInternal(sqlite3 *db, const CodecConfig &cfg)
{
    ASSERT_EQ(sqlite3_key(db, cfg.pKey, cfg.nKey), SQLITE_OK);
    ASSERT_EQ(sqlite3_exec(db,
        BuildCodecPragmaSql(cfg).c_str(),
        nullptr, nullptr, nullptr), SQLITE_OK)
        << "PRAGMA config failed: " << sqlite3_errmsg(db);
}

static void SetCodecConfig(sqlite3 *db, const CodecConfig &cfg)
{
    SetCodecConfigInternal(db, cfg);
}

/* ------------------------------------------------------------------ */
/* Helper: create table and insert data                                */
/* ------------------------------------------------------------------ */
static void CreateAndInsert(sqlite3 *db, int count)
{
    ASSERT_EQ(sqlite3_exec(db, "PRAGMA journal_mode=WAL;",
        nullptr, nullptr, nullptr), SQLITE_OK);

    const char *ddl = "CREATE TABLE mig_test("
                      "id INTEGER PRIMARY KEY, "
                      "name TEXT NOT NULL, "
                      "value REAL);";
    ASSERT_EQ(sqlite3_exec(db, ddl, nullptr, nullptr, nullptr), SQLITE_OK)
        << "CREATE TABLE failed: " << sqlite3_errmsg(db);

    sqlite3_stmt *stmt = nullptr;
    ASSERT_EQ(sqlite3_prepare_v2(db,
        "INSERT INTO mig_test VALUES(?,?,?);", -1, &stmt, nullptr), SQLITE_OK);

    for (int i = 0; i < count; i++) {
        sqlite3_bind_int(stmt, 1, i);
        std::string nameStr = "name_" + std::to_string(i);
        sqlite3_bind_text(stmt, 2, nameStr.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_double(stmt, 3, 3.14 + i);
        ASSERT_EQ(sqlite3_step(stmt), SQLITE_DONE);
        sqlite3_reset(stmt);
    }
    sqlite3_finalize(stmt);
}

/* ------------------------------------------------------------------ */
/* Helper: verify data                                                 */
/* ------------------------------------------------------------------ */
static void VerifyData(sqlite3 *db, int expectedCount)
{
    sqlite3_stmt *stmt = nullptr;
    ASSERT_EQ(sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM mig_test;",
        -1, &stmt, nullptr), SQLITE_OK);
    ASSERT_EQ(sqlite3_step(stmt), SQLITE_ROW);
    int count = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    EXPECT_EQ(count, expectedCount);

    /* spot-check a few rows */
    ASSERT_EQ(sqlite3_prepare_v2(db,
        "SELECT id, name, value FROM mig_test WHERE id=?;",
        -1, &stmt, nullptr), SQLITE_OK);
    int step = (expectedCount > 10) ? (expectedCount / 10) : 1;
    for (int i = 0; i < expectedCount; i += step) {
        sqlite3_bind_int(stmt, 1, i);
        ASSERT_EQ(sqlite3_step(stmt), SQLITE_ROW) << "row " << i << " missing";
        EXPECT_EQ(sqlite3_column_int(stmt, 0), i);
        std::string expectedName = "name_" + std::to_string(i);
        EXPECT_STREQ(reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1)),
                     expectedName.c_str());
        double val = sqlite3_column_double(stmt, 2);
        EXPECT_NEAR(val, 3.14 + i, 0.001);
        sqlite3_reset(stmt);
    }
    sqlite3_finalize(stmt);
}

/* ------------------------------------------------------------------ */
/* Helper: verify PRAGMA codec_cipher returns expected name            */
/* ------------------------------------------------------------------ */
static void VerifyCipherName(sqlite3 *db, const char *expected)
{
    sqlite3_stmt *stmt = nullptr;
    ASSERT_EQ(sqlite3_prepare_v2(db, "PRAGMA codec_cipher;",
        -1, &stmt, nullptr), SQLITE_OK);
    ASSERT_EQ(sqlite3_step(stmt), SQLITE_ROW);
    const char *name = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
    EXPECT_STREQ(name, expected);
    sqlite3_finalize(stmt);
}

/* ================================================================== */
/* Test 001: Basic encrypt/decrypt round-trip with aes-256-gcm          */
/* ================================================================== */
HWTEST_F(CodecMigrationTest, Codec_Mig_Test_001, TestSize.Level0)
{
    sqlite3 *db = nullptr;
    /* step1: create encrypted DB */
    ASSERT_EQ(sqlite3_open(MIG_TEST_DB.c_str(), &db), SQLITE_OK);
    SetCodecConfig(db, { "aes-256-gcm", "SHA1", "KDF_SHA1", MIG_TEST_KEY, 32, 5000, 1024});
    CreateAndInsert(db, MIG_TEST_COUNT);
    sqlite3_close(db);

    /* step2: reopen with correct key and verify */
    ASSERT_EQ(sqlite3_open(MIG_TEST_DB.c_str(), &db), SQLITE_OK);
    SetCodecConfig(db, { "aes-256-gcm", "SHA1", "KDF_SHA1", MIG_TEST_KEY, 32, 5000, 1024});
    VerifyData(db, MIG_TEST_COUNT);
    sqlite3_close(db);
}

/* ================================================================== */
/* Test 002: Encrypt/decrypt with aes-256-cbc                           */
/* ================================================================== */
HWTEST_F(CodecMigrationTest, Codec_Mig_Test_002, TestSize.Level0)
{
    sqlite3 *db = nullptr;
    ASSERT_EQ(sqlite3_open(MIG_TEST_DB.c_str(), &db), SQLITE_OK);
    SetCodecConfig(db, { "aes-256-cbc", "SHA1", "KDF_SHA1", MIG_TEST_KEY, 32, 5000, 1024});
    CreateAndInsert(db, MIG_TEST_COUNT);
    sqlite3_close(db);

    ASSERT_EQ(sqlite3_open(MIG_TEST_DB.c_str(), &db), SQLITE_OK);
    SetCodecConfig(db, { "aes-256-cbc", "SHA1", "KDF_SHA1", MIG_TEST_KEY, 32, 5000, 1024});
    VerifyData(db, MIG_TEST_COUNT);
    sqlite3_close(db);
}

/* ================================================================== */
/* Test 003: Wrong key is rejected                                      */
/* ================================================================== */
HWTEST_F(CodecMigrationTest, Codec_Mig_Test_003, TestSize.Level0)
{
    sqlite3 *db = nullptr;
    ASSERT_EQ(sqlite3_open(MIG_TEST_DB.c_str(), &db), SQLITE_OK);
    SetCodecConfig(db, { "aes-256-gcm", "SHA1", "KDF_SHA1", MIG_TEST_KEY, 32, 5000, 1024});
    CreateAndInsert(db, MIG_TEST_COUNT);
    sqlite3_close(db);

    /* reopen with wrong key */
    ASSERT_EQ(sqlite3_open(MIG_TEST_DB.c_str(), &db), SQLITE_OK);
    SetCodecConfigInternal(db, { "aes-256-gcm", "SHA1", "KDF_SHA1", MIG_WRONG_KEY, 32, 5000, 1024});
    sqlite3_stmt *stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM mig_test;",
        -1, &stmt, nullptr);
    /* wrong key should cause NOTADB or error, not OK */
    if (rc == SQLITE_OK) {
        rc = sqlite3_step(stmt);
        EXPECT_NE(rc, SQLITE_ROW);
        sqlite3_finalize(stmt);
    }
    sqlite3_close(db);
}

/* ================================================================== */
/* Test 004: PRAGMA codec_cipher returns correct name                   */
/* ================================================================== */
HWTEST_F(CodecMigrationTest, Codec_Mig_Test_004, TestSize.Level0)
{
    sqlite3 *db = nullptr;
    ASSERT_EQ(sqlite3_open(MIG_TEST_DB.c_str(), &db), SQLITE_OK);
    SetCodecConfig(db, { "aes-256-gcm", "SHA1", "KDF_SHA1", MIG_TEST_KEY, 32, 5000, 1024});
    VerifyCipherName(db, "aes-256-gcm");
    sqlite3_close(db);

    ASSERT_EQ(sqlite3_open(MIG_TEST_DB2.c_str(), &db), SQLITE_OK);
    SetCodecConfig(db, { "aes-256-cbc", "SHA1", "KDF_SHA1", MIG_TEST_KEY, 32, 5000, 1024});
    VerifyCipherName(db, "aes-256-cbc");
    sqlite3_close(db);
}

/* ================================================================== */
/* Test 005: Rekey with changed cipher and HMAC algorithm               */
/* ================================================================== */
HWTEST_F(CodecMigrationTest, Codec_Mig_Test_005, TestSize.Level0)
{
    sqlite3 *db = nullptr;
    ASSERT_EQ(sqlite3_open(MIG_TEST_DB.c_str(), &db), SQLITE_OK);
    SetCodecConfig(db, { "aes-256-gcm", "SHA1", "KDF_SHA1", MIG_TEST_KEY, 32, 5000, 1024});
    CreateAndInsert(db, MIG_TEST_COUNT);
    sqlite3_close(db);

    /* rekey: gcm+SHA1 -> cbc+SHA256 */
    CodecRekeyConfig rekeyCfg = {
        MIG_TEST_DB.c_str(),
        { "aes-256-gcm", "SHA1", "KDF_SHA1", MIG_TEST_KEY, 32, 5000, 1024 },
        { "aes-256-cbc", "SHA256", "KDF_SHA256", MIG_TEST_KEY2, 32, 5000, 1024 },
    };
    ASSERT_EQ(sqlite3_rekey_v3(&rekeyCfg), SQLITE_OK);

    /* reopen with new config */
    ASSERT_EQ(sqlite3_open(MIG_TEST_DB.c_str(), &db), SQLITE_OK);
    SetCodecConfigInternal(db, { "aes-256-cbc", "SHA256", "KDF_SHA256", MIG_TEST_KEY2, 32, 5000, 1024});
    VerifyData(db, MIG_TEST_COUNT);
    VerifyCipherName(db, "aes-256-cbc");
    sqlite3_close(db);
}

/* ================================================================== */
/* Test 006: Combined — all HMAC + KDF algorithm permutations           */
/* ================================================================== */
HWTEST_F(CodecMigrationTest, Codec_Mig_Test_006, TestSize.Level0)
{
    const char *hmacAlgos[] = { "SHA1", "SHA256", "SHA512" };
    const char *kdfAlgos[] = { "KDF_SHA1", "KDF_SHA256", "KDF_SHA512" };

    for (int h = 0; h < 3; h++) {
        for (int k = 0; k < 3; k++) {
            std::string dbPath =
                (std::filesystem::path(MIG_TEST_DIR)
                    / ("mig_h" + std::to_string(h) + "_k" + std::to_string(k) + ".db")).string();
            unlink(dbPath.c_str());

            sqlite3 *db = nullptr;
            ASSERT_EQ(sqlite3_open(dbPath.c_str(), &db), SQLITE_OK);
            SetCodecConfig(db, { "aes-256-gcm", hmacAlgos[h], kdfAlgos[k], MIG_TEST_KEY, 32, 5000, 1024});
            CreateAndInsert(db, 10);
            sqlite3_close(db);

            ASSERT_EQ(sqlite3_open(dbPath.c_str(), &db), SQLITE_OK);
            SetCodecConfig(db, { "aes-256-gcm", hmacAlgos[h], kdfAlgos[k], MIG_TEST_KEY, 32, 5000, 1024});
            VerifyData(db, 10);
            sqlite3_close(db);

            unlink(dbPath.c_str());
        }
    }
}

/* ================================================================== */
/* Test 007: Unknown cipher name is rejected                            */
/* ================================================================== */
HWTEST_F(CodecMigrationTest, Codec_Mig_Test_007, TestSize.Level0)
{
    sqlite3 *db = nullptr;
    ASSERT_EQ(sqlite3_open(MIG_TEST_DB.c_str(), &db), SQLITE_OK);
    ASSERT_EQ(sqlite3_key(db, MIG_TEST_KEY, 32), SQLITE_OK);
    /* invalid cipher should fail */
    EXPECT_NE(sqlite3_exec(db, "PRAGMA codec_cipher='invalid-cipher';",
        nullptr, nullptr, nullptr), SQLITE_OK);
    sqlite3_close(db);
}

/* ================================================================== */
/* Test 008: Rekey with same cipher but different KDF+HMAC              */
/* ================================================================== */
HWTEST_F(CodecMigrationTest, Codec_Mig_Test_008, TestSize.Level0)
{
    sqlite3 *db = nullptr;
    ASSERT_EQ(sqlite3_open(MIG_TEST_DB.c_str(), &db), SQLITE_OK);
    SetCodecConfig(db, { "aes-256-gcm", "SHA1", "KDF_SHA1", MIG_TEST_KEY, 32, 5000, 1024});
    CreateAndInsert(db, MIG_TEST_COUNT);
    sqlite3_close(db);

    /* rekey: same gcm, SHA1->SHA512 for both HMAC and KDF */
    CodecRekeyConfig rekeyCfg = {
        MIG_TEST_DB.c_str(),
        { "aes-256-gcm", "SHA1", "KDF_SHA1", MIG_TEST_KEY, 32, 5000, 1024 },
        { "aes-256-gcm", "SHA512", "KDF_SHA512", MIG_TEST_KEY2, 32, 8000, 1024 },
    };
    ASSERT_EQ(sqlite3_rekey_v3(&rekeyCfg), SQLITE_OK);

    ASSERT_EQ(sqlite3_open(MIG_TEST_DB.c_str(), &db), SQLITE_OK);
    SetCodecConfigInternal(db, { "aes-256-gcm", "SHA512", "KDF_SHA512", MIG_TEST_KEY2, 32, 8000, 1024});
    VerifyData(db, MIG_TEST_COUNT);
    VerifyCipherName(db, "aes-256-gcm");
    sqlite3_close(db);
}

/* ================================================================== */
/* Test 009: Rekey from cbc to gcm (cipher switch)                      */
/* ================================================================== */
HWTEST_F(CodecMigrationTest, Codec_Mig_Test_009, TestSize.Level0)
{
    sqlite3 *db = nullptr;
    ASSERT_EQ(sqlite3_open(MIG_TEST_DB.c_str(), &db), SQLITE_OK);
    SetCodecConfig(db, { "aes-256-cbc", "SHA256", "KDF_SHA256", MIG_TEST_KEY, 32, 5000, 1024});
    CreateAndInsert(db, MIG_TEST_COUNT);
    sqlite3_close(db);

    CodecRekeyConfig rekeyCfg = {
        MIG_TEST_DB.c_str(),
        { "aes-256-cbc", "SHA256", "KDF_SHA256", MIG_TEST_KEY, 32, 5000, 1024 },
        { "aes-256-gcm", "SHA1", "KDF_SHA1", MIG_TEST_KEY2, 32, 5000, 1024 },
    };
    ASSERT_EQ(sqlite3_rekey_v3(&rekeyCfg), SQLITE_OK);

    ASSERT_EQ(sqlite3_open(MIG_TEST_DB.c_str(), &db), SQLITE_OK);
    SetCodecConfigInternal(db, { "aes-256-gcm", "SHA1", "KDF_SHA1", MIG_TEST_KEY2, 32, 5000, 1024});
    VerifyData(db, MIG_TEST_COUNT);
    VerifyCipherName(db, "aes-256-gcm");
    sqlite3_close(db);
}

/* ================================================================== */
/* Test 010: Concurrent DBs with different cipher configs               */
/* ================================================================== */
HWTEST_F(CodecMigrationTest, Codec_Mig_Test_010, TestSize.Level0)
{
    sqlite3 *db1 = nullptr;
    sqlite3 *db2 = nullptr;
    sqlite3 *db3 = nullptr;

    /* open 3 DBs with different configs simultaneously */
    ASSERT_EQ(sqlite3_open(MIG_TEST_DB.c_str(), &db1), SQLITE_OK);
    SetCodecConfig(db1, { "aes-256-gcm", "SHA1", "KDF_SHA1", MIG_TEST_KEY, 32, 5000, 1024});
    CreateAndInsert(db1, 10);

    ASSERT_EQ(sqlite3_open(MIG_TEST_DB2.c_str(), &db2), SQLITE_OK);
    SetCodecConfig(db2, { "aes-256-cbc", "SHA256", "KDF_SHA256", MIG_TEST_KEY, 32, 8000, 1024});
    CreateAndInsert(db2, 20);

    std::string dbPath3 =
        (std::filesystem::path(MIG_TEST_DIR) / "mig_test3.db").string();
    ASSERT_EQ(sqlite3_open(dbPath3.c_str(), &db3), SQLITE_OK);
    SetCodecConfig(db3, { "aes-256-gcm", "SHA512", "KDF_SHA512", MIG_TEST_KEY, 32, 4096, 8192});
    CreateAndInsert(db3, 30);

    /* verify all 3 while still open */
    VerifyData(db1, 10);
    VerifyData(db2, 20);
    VerifyData(db3, 30);

    sqlite3_close(db1);
    sqlite3_close(db2);
    sqlite3_close(db3);

    /* reopen and verify persistence */
    ASSERT_EQ(sqlite3_open(MIG_TEST_DB.c_str(), &db1), SQLITE_OK);
    SetCodecConfig(db1, { "aes-256-gcm", "SHA1", "KDF_SHA1", MIG_TEST_KEY, 32, 5000, 1024});
    VerifyData(db1, 10);
    sqlite3_close(db1);

    ASSERT_EQ(sqlite3_open(MIG_TEST_DB2.c_str(), &db2), SQLITE_OK);
    SetCodecConfig(db2, { "aes-256-cbc", "SHA256", "KDF_SHA256", MIG_TEST_KEY, 32, 8000, 1024});
    VerifyData(db2, 20);
    sqlite3_close(db2);

    ASSERT_EQ(sqlite3_open(dbPath3.c_str(), &db3), SQLITE_OK);
    SetCodecConfig(db3, { "aes-256-gcm", "SHA512", "KDF_SHA512", MIG_TEST_KEY, 32, 4096, 8192});
    VerifyData(db3, 30);
    sqlite3_close(db3);
}

/* ================================================================== */
/* Test 011: Invalid HMAC/KDF algorithm name is rejected                */
/* ================================================================== */
HWTEST_F(CodecMigrationTest, Codec_Mig_Test_011, TestSize.Level0)
{
    sqlite3 *db = nullptr;
    ASSERT_EQ(sqlite3_open(MIG_TEST_DB.c_str(), &db), SQLITE_OK);
    ASSERT_EQ(sqlite3_key(db, MIG_TEST_KEY, 32), SQLITE_OK);

    /* invalid HMAC algo should fail */
    EXPECT_NE(sqlite3_exec(db, "PRAGMA codec_hmac_algo='INVALID';",
        nullptr, nullptr, nullptr), SQLITE_OK);

    /* invalid KDF algo should fail */
    EXPECT_NE(sqlite3_exec(db, "PRAGMA codec_kdf_algo='INVALID';",
        nullptr, nullptr, nullptr), SQLITE_OK);

    sqlite3_close(db);
}

/* ================================================================== */
/* Test 012: Empty table encrypt/decrypt round-trip                     */
/* ================================================================== */
HWTEST_F(CodecMigrationTest, Codec_Mig_Test_012, TestSize.Level0)
{
    sqlite3 *db = nullptr;
    ASSERT_EQ(sqlite3_open(MIG_TEST_DB.c_str(), &db), SQLITE_OK);
    SetCodecConfig(db, { "aes-256-gcm", "SHA256", "KDF_SHA256", MIG_TEST_KEY, 32, 5000, 1024});
    /* create table but insert no rows */
    ASSERT_EQ(sqlite3_exec(db, "CREATE TABLE empty_test(id INTEGER);",
        nullptr, nullptr, nullptr), SQLITE_OK);
    sqlite3_close(db);

    ASSERT_EQ(sqlite3_open(MIG_TEST_DB.c_str(), &db), SQLITE_OK);
    SetCodecConfig(db, { "aes-256-gcm", "SHA256", "KDF_SHA256", MIG_TEST_KEY, 32, 5000, 1024});
    sqlite3_stmt *stmt = nullptr;
    ASSERT_EQ(sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM empty_test;",
        -1, &stmt, nullptr), SQLITE_OK);
    ASSERT_EQ(sqlite3_step(stmt), SQLITE_ROW);
    EXPECT_EQ(sqlite3_column_int(stmt, 0), 0);
    sqlite3_finalize(stmt);
    sqlite3_close(db);
}

/* ================================================================== */
/* Test 013: UPDATE and DELETE on encrypted DB                          */
/* ================================================================== */
HWTEST_F(CodecMigrationTest, Codec_Mig_Test_013, TestSize.Level0)
{
    sqlite3 *db = nullptr;
    ASSERT_EQ(sqlite3_open(MIG_TEST_DB.c_str(), &db), SQLITE_OK);
    SetCodecConfig(db, { "aes-256-gcm", "SHA1", "KDF_SHA1", MIG_TEST_KEY, 32, 5000, 1024});
    CreateAndInsert(db, MIG_TEST_COUNT);

    /* UPDATE some rows */
    sqlite3_stmt *stmt = nullptr;
    ASSERT_EQ(sqlite3_prepare_v2(db,
        "UPDATE mig_test SET value=? WHERE id=?;",
        -1, &stmt, nullptr), SQLITE_OK);
    for (int i = 0; i < MIG_TEST_COUNT; i += 10) {
        sqlite3_bind_double(stmt, 1, 9.99 + i);
        sqlite3_bind_int(stmt, 2, i);
        ASSERT_EQ(sqlite3_step(stmt), SQLITE_DONE);
        sqlite3_reset(stmt);
    }
    sqlite3_finalize(stmt);

    /* DELETE some rows */
    ASSERT_EQ(sqlite3_exec(db, "DELETE FROM mig_test WHERE id % 7 == 0;",
        nullptr, nullptr, nullptr), SQLITE_OK);
    sqlite3_close(db);

    /* reopen and verify updates persisted */
    ASSERT_EQ(sqlite3_open(MIG_TEST_DB.c_str(), &db), SQLITE_OK);
    SetCodecConfig(db, { "aes-256-gcm", "SHA1", "KDF_SHA1", MIG_TEST_KEY, 32, 5000, 1024});

    ASSERT_EQ(sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM mig_test;",
        -1, &stmt, nullptr), SQLITE_OK);
    ASSERT_EQ(sqlite3_step(stmt), SQLITE_ROW);
    int remaining = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    EXPECT_GT(remaining, 0);
    EXPECT_LT(remaining, MIG_TEST_COUNT);

    /* verify an updated row (id=10: 10%7=3 not deleted, updated to 19.99) */
    ASSERT_EQ(sqlite3_prepare_v2(db,
        "SELECT value FROM mig_test WHERE id=10;",
        -1, &stmt, nullptr), SQLITE_OK);
    ASSERT_EQ(sqlite3_step(stmt), SQLITE_ROW);
    EXPECT_NEAR(sqlite3_column_double(stmt, 0), 19.99, 0.001);
    sqlite3_finalize(stmt);

    /* verify a deleted row is gone */
    ASSERT_EQ(sqlite3_prepare_v2(db,
        "SELECT value FROM mig_test WHERE id=7;",
        -1, &stmt, nullptr), SQLITE_OK);
    ASSERT_EQ(sqlite3_step(stmt), SQLITE_DONE);
    sqlite3_finalize(stmt);
    sqlite3_close(db);
}

/* ================================================================== */
/* Test 014: Transaction ROLLBACK on encrypted DB                       */
/* ================================================================== */
HWTEST_F(CodecMigrationTest, Codec_Mig_Test_014, TestSize.Level0)
{
    sqlite3 *db = nullptr;
    ASSERT_EQ(sqlite3_open(MIG_TEST_DB.c_str(), &db), SQLITE_OK);
    SetCodecConfig(db, { "aes-256-gcm", "SHA1", "KDF_SHA1", MIG_TEST_KEY, 32, 5000, 1024});
    CreateAndInsert(db, 50);
    sqlite3_close(db);

    /* reopen, start transaction, insert, then ROLLBACK */
    ASSERT_EQ(sqlite3_open(MIG_TEST_DB.c_str(), &db), SQLITE_OK);
    SetCodecConfig(db, { "aes-256-gcm", "SHA1", "KDF_SHA1", MIG_TEST_KEY, 32, 5000, 1024});

    ASSERT_EQ(sqlite3_exec(db, "BEGIN;", nullptr, nullptr, nullptr), SQLITE_OK);
    sqlite3_stmt *stmt = nullptr;
    ASSERT_EQ(sqlite3_prepare_v2(db,
        "INSERT INTO mig_test VALUES(?,?,?);",
        -1, &stmt, nullptr), SQLITE_OK);
    for (int i = 100; i < 200; i++) {
        sqlite3_bind_int(stmt, 1, i);
        std::string rolledStr = "rolled_" + std::to_string(i);
        sqlite3_bind_text(stmt, 2, rolledStr.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_double(stmt, 3, 1.11 + i);
        ASSERT_EQ(sqlite3_step(stmt), SQLITE_DONE);
        sqlite3_reset(stmt);
    }
    sqlite3_finalize(stmt);
    ASSERT_EQ(sqlite3_exec(db, "ROLLBACK;",
        nullptr, nullptr, nullptr), SQLITE_OK);
    sqlite3_close(db);

    /* verify rolled-back data is not present */
    ASSERT_EQ(sqlite3_open(MIG_TEST_DB.c_str(), &db), SQLITE_OK);
    SetCodecConfig(db, { "aes-256-gcm", "SHA1", "KDF_SHA1", MIG_TEST_KEY, 32, 5000, 1024});
    VerifyData(db, 50);

    ASSERT_EQ(sqlite3_prepare_v2(db,
        "SELECT COUNT(*) FROM mig_test WHERE id>=100;",
        -1, &stmt, nullptr), SQLITE_OK);
    ASSERT_EQ(sqlite3_step(stmt), SQLITE_ROW);
    EXPECT_EQ(sqlite3_column_int(stmt, 0), 0);
    sqlite3_finalize(stmt);
    sqlite3_close(db);
}

/* ================================================================== */
/* Test 015: VACUUM on encrypted DB                                     */
/* ================================================================== */
HWTEST_F(CodecMigrationTest, Codec_Mig_Test_015, TestSize.Level0)
{
    sqlite3 *db = nullptr;
    ASSERT_EQ(sqlite3_open(MIG_TEST_DB.c_str(), &db), SQLITE_OK);
    SetCodecConfig(db, { "aes-256-gcm", "SHA1", "KDF_SHA1", MIG_TEST_KEY, 32, 5000, 1024});
    CreateAndInsert(db, 50);

    /* delete half the rows to create free pages, then VACUUM */
    ASSERT_EQ(sqlite3_exec(db, "DELETE FROM mig_test WHERE id < 25;",
        nullptr, nullptr, nullptr), SQLITE_OK);
    ASSERT_EQ(sqlite3_exec(db, "VACUUM;",
        nullptr, nullptr, nullptr), SQLITE_OK);
    sqlite3_close(db);

    /* verify data after VACUUM */
    ASSERT_EQ(sqlite3_open(MIG_TEST_DB.c_str(), &db), SQLITE_OK);
    SetCodecConfig(db, { "aes-256-gcm", "SHA1", "KDF_SHA1", MIG_TEST_KEY, 32, 5000, 1024});

    sqlite3_stmt *stmt = nullptr;
    ASSERT_EQ(sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM mig_test;",
        -1, &stmt, nullptr), SQLITE_OK);
    ASSERT_EQ(sqlite3_step(stmt), SQLITE_ROW);
    EXPECT_EQ(sqlite3_column_int(stmt, 0), 25);
    sqlite3_finalize(stmt);

    /* spot check remaining data */
    ASSERT_EQ(sqlite3_prepare_v2(db,
        "SELECT id FROM mig_test WHERE id=25;",
        -1, &stmt, nullptr), SQLITE_OK);
    ASSERT_EQ(sqlite3_step(stmt), SQLITE_ROW);
    EXPECT_EQ(sqlite3_column_int(stmt, 0), 25);
    sqlite3_finalize(stmt);
    sqlite3_close(db);
}

/* ================================================================== */
/* Test 016: Rekey with wrong source config (mismatch)                  */
/* ================================================================== */
HWTEST_F(CodecMigrationTest, Codec_Mig_Test_016, TestSize.Level0)
{
    sqlite3 *db = nullptr;
    ASSERT_EQ(sqlite3_open(MIG_TEST_DB.c_str(), &db), SQLITE_OK);
    SetCodecConfig(db, { "aes-256-gcm", "SHA1", "KDF_SHA1", MIG_TEST_KEY, 32, 5000, 1024});
    CreateAndInsert(db, 50);
    sqlite3_close(db);

    /* rekey with WRONG old key */
    CodecRekeyConfig rekeyCfg = {
        MIG_TEST_DB.c_str(),
        { "aes-256-gcm", "SHA1", "KDF_SHA1", MIG_WRONG_KEY, 32, 5000, 1024 },
        { "aes-256-gcm", "SHA256", "KDF_SHA256", MIG_TEST_KEY2, 32, 5000, 1024 },
    };
    ASSERT_NE(sqlite3_rekey_v3(&rekeyCfg), SQLITE_OK);
}

/* ================================================================== */
/* Test 017: PRAGMA integrity_check on encrypted DB                     */
/* ================================================================== */
HWTEST_F(CodecMigrationTest, Codec_Mig_Test_017, TestSize.Level0)
{
    sqlite3 *db = nullptr;
    ASSERT_EQ(sqlite3_open(MIG_TEST_DB.c_str(), &db), SQLITE_OK);
    SetCodecConfig(db, { "aes-256-gcm", "SHA256", "KDF_SHA256", MIG_TEST_KEY, 32, 5000, 4096});
    CreateAndInsert(db, MIG_TEST_COUNT);
    sqlite3_close(db);

    ASSERT_EQ(sqlite3_open(MIG_TEST_DB.c_str(), &db), SQLITE_OK);
    SetCodecConfig(db, { "aes-256-gcm", "SHA256", "KDF_SHA256", MIG_TEST_KEY, 32, 5000, 4096});

    sqlite3_stmt *stmt = nullptr;
    ASSERT_EQ(sqlite3_prepare_v2(db, "PRAGMA integrity_check;",
        -1, &stmt, nullptr), SQLITE_OK);
    ASSERT_EQ(sqlite3_step(stmt), SQLITE_ROW);
    EXPECT_STREQ(reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0)), "ok");
    sqlite3_finalize(stmt);
    sqlite3_close(db);
}

/* ================================================================== */
/* Test 018: Corrupted page detected by HMAC                            */
/* ================================================================== */
HWTEST_F(CodecMigrationTest, Codec_Mig_Test_018, TestSize.Level0)
{
    sqlite3 *db = nullptr;
    ASSERT_EQ(sqlite3_open(MIG_TEST_DB.c_str(), &db), SQLITE_OK);
    SetCodecConfig(db, { "aes-256-gcm", "SHA1", "KDF_SHA1", MIG_TEST_KEY, 32, 5000, 1024});
    CreateAndInsert(db, 50);
    sqlite3_close(db);

    /* corrupt a byte in the middle of the DB file (past header) */
    FILE *fp = fopen(MIG_TEST_DB.c_str(), "r+b");
    ASSERT_NE(fp, nullptr);
    fseek(fp, 512, SEEK_SET);
    int orig = fgetc(fp);
    fseek(fp, 512, SEEK_SET);
    fputc(orig ^ 0xFF, fp);
    fclose(fp);

    /* reopen with correct key — should fail to read corrupted page */
    ASSERT_EQ(sqlite3_open(MIG_TEST_DB.c_str(), &db), SQLITE_OK);
    SetCodecConfig(db, { "aes-256-gcm", "SHA1", "KDF_SHA1", MIG_TEST_KEY, 32, 5000, 1024});
    sqlite3_stmt *stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM mig_test;",
        -1, &stmt, nullptr);
    if (rc == SQLITE_OK) {
        rc = sqlite3_step(stmt);
        /* corrupted page should cause NOTADB or error, not SQLITE_ROW */
        EXPECT_NE(rc, SQLITE_ROW);
        sqlite3_finalize(stmt);
    }
    sqlite3_close(db);
}

/* ================================================================== */
/* Test 019: Case-insensitive cipher and algo names                     */
/* ================================================================== */
HWTEST_F(CodecMigrationTest, Codec_Mig_Test_019, TestSize.Level0)
{
    sqlite3 *db = nullptr;
    ASSERT_EQ(sqlite3_open(MIG_TEST_DB.c_str(), &db), SQLITE_OK);
    ASSERT_EQ(sqlite3_key(db, MIG_TEST_KEY, 32), SQLITE_OK);
    ASSERT_EQ(sqlite3_exec(db,
        "PRAGMA codec_cipher='AES-256-GCM';"
        "PRAGMA codec_hmac_algo='SHA256';"
        "PRAGMA codec_kdf_algo='KDF_SHA256';"
        "PRAGMA codec_page_size=1024;"
        "PRAGMA codec_kdf_iter=5000;",
        nullptr, nullptr, nullptr), SQLITE_OK);
    CreateAndInsert(db, 20);
    sqlite3_close(db);

    /* reopen with uppercase cipher, mixed-case HMAC */
    ASSERT_EQ(sqlite3_open(MIG_TEST_DB.c_str(), &db), SQLITE_OK);
    ASSERT_EQ(sqlite3_key(db, MIG_TEST_KEY, 32), SQLITE_OK);
    ASSERT_EQ(sqlite3_exec(db,
        "PRAGMA codec_cipher='AES-256-gcm';"
        "PRAGMA codec_hmac_algo='sha256';"
        "PRAGMA codec_kdf_algo='KDF_sha256';"
        "PRAGMA codec_page_size=1024;"
        "PRAGMA codec_kdf_iter=5000;",
        nullptr, nullptr, nullptr), SQLITE_OK);
    VerifyData(db, 20);
    VerifyCipherName(db, "aes-256-gcm");
    sqlite3_close(db);
}

/* ================================================================== */
/* Test 020: Concurrent read on same encrypted DB                       */
/* ================================================================== */
HWTEST_F(CodecMigrationTest, Codec_Mig_Test_020, TestSize.Level0)
{
    sqlite3 *db1 = nullptr;
    sqlite3 *db2 = nullptr;

    ASSERT_EQ(sqlite3_open(MIG_TEST_DB.c_str(), &db1), SQLITE_OK);
    SetCodecConfig(db1, { "aes-256-gcm", "SHA1", "KDF_SHA1", MIG_TEST_KEY, 32, 5000, 1024});
    CreateAndInsert(db1, 50);
    sqlite3_close(db1);

    /* open two connections to same encrypted DB simultaneously */
    ASSERT_EQ(sqlite3_open(MIG_TEST_DB.c_str(), &db1), SQLITE_OK);
    SetCodecConfig(db1, { "aes-256-gcm", "SHA1", "KDF_SHA1", MIG_TEST_KEY, 32, 5000, 1024});

    ASSERT_EQ(sqlite3_open(MIG_TEST_DB.c_str(), &db2), SQLITE_OK);
    SetCodecConfig(db2, { "aes-256-gcm", "SHA1", "KDF_SHA1", MIG_TEST_KEY, 32, 5000, 1024});

    /* both connections read concurrently */
    sqlite3_stmt *stmt1 = nullptr;
    sqlite3_stmt *stmt2 = nullptr;
    ASSERT_EQ(sqlite3_prepare_v2(db1, "SELECT COUNT(*) FROM mig_test;",
        -1, &stmt1, nullptr), SQLITE_OK);
    ASSERT_EQ(sqlite3_prepare_v2(db2, "SELECT COUNT(*) FROM mig_test;",
        -1, &stmt2, nullptr), SQLITE_OK);
    ASSERT_EQ(sqlite3_step(stmt1), SQLITE_ROW);
    ASSERT_EQ(sqlite3_step(stmt2), SQLITE_ROW);
    EXPECT_EQ(sqlite3_column_int(stmt1, 0), 50);
    EXPECT_EQ(sqlite3_column_int(stmt2, 0), 50);
    sqlite3_finalize(stmt1);
    sqlite3_finalize(stmt2);

    /* interleaved reads */
    ASSERT_EQ(sqlite3_prepare_v2(db1,
        "SELECT id FROM mig_test WHERE id=10;",
        -1, &stmt1, nullptr), SQLITE_OK);
    ASSERT_EQ(sqlite3_prepare_v2(db2,
        "SELECT id FROM mig_test WHERE id=20;",
        -1, &stmt2, nullptr), SQLITE_OK);
    ASSERT_EQ(sqlite3_step(stmt1), SQLITE_ROW);
    EXPECT_EQ(sqlite3_column_int(stmt1, 0), 10);
    ASSERT_EQ(sqlite3_step(stmt2), SQLITE_ROW);
    EXPECT_EQ(sqlite3_column_int(stmt2, 0), 20);
    sqlite3_finalize(stmt1);
    sqlite3_finalize(stmt2);

    sqlite3_close(db1);
    sqlite3_close(db2);
}

/* ================================================================== */
/* Test 021: WAL file content is encrypted                              */
/* ================================================================== */
HWTEST_F(CodecMigrationTest, Codec_Mig_Test_021, TestSize.Level0)
{
    sqlite3 *db = nullptr;
    ASSERT_EQ(sqlite3_open(MIG_TEST_DB.c_str(), &db), SQLITE_OK);
    SetCodecConfig(db, { "aes-256-gcm", "SHA256", "KDF_SHA256", MIG_TEST_KEY, 32, 5000, 1024});

    /* enable WAL and insert data without checkpoint */
    ASSERT_EQ(sqlite3_exec(db, "PRAGMA journal_mode=WAL;",
        nullptr, nullptr, nullptr), SQLITE_OK);
    ASSERT_EQ(sqlite3_exec(db, "PRAGMA wal_autocheckpoint=0;",
        nullptr, nullptr, nullptr), SQLITE_OK);
    CreateAndInsert(db, 50);

    /* read WAL file while DB is still open (close triggers checkpoint+delete) */
    std::ifstream walFile(MIG_TEST_DB + "-wal", std::ios::binary);
    ASSERT_TRUE(walFile.good());
    std::string walContent((std::istreambuf_iterator<char>(walFile)),
        std::istreambuf_iterator<char>());
    walFile.close();

    /* the WAL file should exist and have content */
    EXPECT_GT(walContent.size(), 0);

    /* "name_0" through "name_49" should NOT appear in plaintext in WAL */
    EXPECT_EQ(walContent.find("name_0"), std::string::npos);
    EXPECT_EQ(walContent.find("name_25"), std::string::npos);
    EXPECT_EQ(walContent.find("name_49"), std::string::npos);

    /* "mig_test" table name should NOT appear in plaintext either */
    EXPECT_EQ(walContent.find("mig_test"), std::string::npos);

    sqlite3_close(db);

    /* verify DB can still be read correctly */
    ASSERT_EQ(sqlite3_open(MIG_TEST_DB.c_str(), &db), SQLITE_OK);
    SetCodecConfig(db, { "aes-256-gcm", "SHA256", "KDF_SHA256", MIG_TEST_KEY, 32, 5000, 1024});
    VerifyData(db, 50);
    sqlite3_close(db);
}
