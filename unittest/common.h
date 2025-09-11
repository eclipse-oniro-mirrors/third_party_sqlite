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
#ifndef COMMON_H
#define COMMON_H

#include <string>

namespace UnitTest {
namespace SQLiteTest {

#define TEST_STATUS_OK 0
#define TEST_STATUS_ERR (-1)

class Common {
public:
    static int RemoveDir(const char *dir);
    static int MakeDir(const char *dir);
    static bool IsFileExist(const char *fullPath);
    static void DestroyDbFile(const std::string &dbPath, int offset, const std::string replaceStr);
};
}  // namespace SQLiteTest
}  // namespace UnitTest

#endif /* COMMON_H */
