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

#include "common.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

using namespace std;

namespace OHOS {
namespace SQLiteTest {
int Common::RemoveDir(const char *dir)
{
    if (dir == nullptr) {
        return TEST_STATUS_ERR;
    }
    if (access(dir, F_OK) != 0) {
        return TEST_STATUS_OK;
    }
    struct stat dirStat;
    if (stat(dir, &dirStat) < 0) {
        return TEST_STATUS_ERR;
    }
    DIR *dirPtr = nullptr;
    struct dirent *dr = nullptr;
    if (S_ISREG(dirStat.st_mode)) {
        remove(dir);
    } else if (S_ISDIR(dirStat.st_mode)) {
        dirPtr = opendir(dir);
        while ((dr = readdir(dirPtr)) != nullptr) {
            if ((strcmp(".", dr->d_name) == 0) || (strcmp("..", dr->d_name) == 0)) {
                continue;
            }
            string dirName = dir;
            dirName += "/";
            dirName += dr->d_name;
            RemoveDir(dirName.c_str());
        }
        closedir(dirPtr);
        rmdir(dir);
    } else {
        return TEST_STATUS_ERR;
    }
    return TEST_STATUS_OK;
}

int Common::MakeDir(const char *dir)
{
    if (dir == nullptr) {
        return TEST_STATUS_ERR;
    }
    if (strlen(dir) > PATH_MAX) {
        return TEST_STATUS_ERR;
    }
    if (access(dir, 0) != -1) {
        return TEST_STATUS_OK;
    }
    char tmpPath[PATH_MAX + 1] = {0};
    const char *pcur = dir;
    int pos = 0;
    while (*pcur++ != '\0') {
        tmpPath[pos++] = *const_cast<char *>(pcur - 1);
        if ((*pcur == '/' || *pcur == '\0') && access(tmpPath, 0) != 0 && strlen(tmpPath) > 0) {
            if (mkdir(tmpPath, (S_IRUSR | S_IWUSR | S_IXUSR)) != 0) {
                return TEST_STATUS_ERR;
            }
        }
    }
    return TEST_STATUS_OK;
}

bool Common::IsFileExist(const char *fullPath)
{
    struct stat statBuf;
    if (stat(fullPath, &statBuf) < 0) {
        return false;
    }
    return S_ISREG(statBuf.st_mode);
}

}  // namespace SQLiteTest
}  // namespace OHOS