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

#include <string>

#include "sqlite3sym.h"

using namespace testing::ext;

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
    mkdir("./sqlitetest", 0770);
}

void LibSQLiteTest::TearDownTestCase(void)
{
}

void LibSQLiteTest::SetUp(void)
{
}

void LibSQLiteTest::TearDown(void)
{
}

/**
 * @tc.name: Lib_SQLite_Test_001
 * @tc.desc: demo for LibSQLiteTest.
 * @tc.type: FUNC
 */
HWTEST_F(LibSQLiteTest, Lib_SQLite_Test_001, TestSize.Level0)
{
}
