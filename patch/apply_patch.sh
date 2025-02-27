#!/bin/bash
# Copyright (C) 2025 Huawei Device Co., Ltd.
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

set -e

cur_dir=$(pwd)
src_dir=$1
dst_dir=$2
patch_dir=$3

echo "cur_dir: $cur_dir"
echo "src_dir: $src_dir"
echo "dst_dir: $dst_dir"
echo "patch_dir: $patch_dir"

if [ "$src_dir" == "" ] || [ "$dst_dir" == "" ] || [ "$patch_dir" == "" ]; then
    echo "invalid params for apply_patch."
exit 1
fi

echo "check and clear patched_sqlite_dir: $dst_dir"
if [ -d "$dst_dir" ]; then
    echo "remove $dst_dir begin"
    rm -rf "$dst_dir"
fi

echo "create patched_sqlite_dir: $dst_dir"
mkdir -p $dst_dir

echo "copy $src_dir/* to $dst_dir/"
cp -fa $src_dir/* $dst_dir

ls -l $patch_dir/*.patch
if [ $? -ne 0 ]; then
    echo "WARNING: no patch."
    exit 0
fi

PATCH_FILES=$(realpath $(ls $patch_dir/*.patch))
echo "found the following patchs:"
echo "$PATCH_FILES"

cd $dst_dir
echo "pwd: $(pwd)"

for patch_file in $PATCH_FILES; do
    echo "applying patch: $patch_file"
    patch -p1 -i "$patch_file"
    if [ $? -ne 0 ]; then
        echo "failed to apply patch: $patch_file"
        exit 1
    fi
done

cd $cur_dir
exit 0
