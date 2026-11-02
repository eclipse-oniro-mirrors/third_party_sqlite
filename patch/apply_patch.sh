#!/bin/bash
# Copyright (C) 2026 Huawei Device Co., Ltd.
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

# Convert patch_dir to an absolute path so derived paths stay valid after cd $dst_dir
patch_dir=$(cd "$patch_dir" && pwd)

echo "check and clear patched_sqlite_dir: $dst_dir"
if [ -d "$dst_dir" ]; then
    echo "remove $dst_dir begin"
    rm -rf "$dst_dir"
fi

echo "create patched_sqlite_dir: $dst_dir"
mkdir -p $dst_dir

echo "copy $src_dir/* to $dst_dir/"
cp -fa $src_dir/* $dst_dir

ALL_PATCH="${patch_dir}/all_sqlite.patch"
if [ ! -f "$ALL_PATCH" ]; then
    echo "WARNING: all_sqlite.patch not found in $patch_dir."
    exit 1
fi

cd $dst_dir
echo "pwd: $(pwd)"

echo "applying patch: $ALL_PATCH"
patch -p1 -i "$ALL_PATCH"
if [ $? -ne 0 ]; then
    echo "failed to apply patch: $ALL_PATCH"
    exit 1
fi

# Checking whether the reference directory exists
ref_dir="${patch_dir}/patched_sqlite"
echo "verify reference directory: $ref_dir"
if [ ! -d "$ref_dir" ]; then
    echo "reference directory not found: $ref_dir"
    exit 1
fi

# Verify patched source against reference directory
echo "check whether the files in $dst_dir and $ref_dir are the same."
diff_output=$(diff -r "$dst_dir" "$ref_dir") || true
if [ -n "$diff_output" ]; then
    echo "patched source does not match reference directory."
    echo "$diff_output"
    exit 1
fi
echo "patched source matches reference directory."

cd $cur_dir
exit 0
