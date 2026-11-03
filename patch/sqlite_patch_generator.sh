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

# ============================================================================
# sqlite_patch_generator.sh
#
# Generates a single consolidated git patch (patch/all_sqlite.patch) capturing
# every difference between the local source directories (src/, include/, ext/)
# and the reference directory (patched_sqlite/).
#
# Stages:
#   1. Copy src/, include/, ext/ to a temp dir.
#   2. Diff temp dir vs patched_sqlite/, regenerate all_sqlite.patch if needed.
# ============================================================================

ORIGIN_DIR=$(pwd)
SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
REPO_DIR=$(cd "${SCRIPT_DIR}/.." && pwd)
REFERENCE_DIR="${SCRIPT_DIR}/patched_sqlite"
OUTPUT_PATCH="${SCRIPT_DIR}/all_sqlite.patch"

# ---------------------------------------------------------------------------
# Stage 1: Copy src/include/ext to temp dir
# ---------------------------------------------------------------------------
echo "========== Stage 1: Copy src/include/ext to temp dir =========="

tmp_git_dir=$(mktemp -d)
echo "Create temp dir: ${tmp_git_dir}"
trap 'rm -rf "${tmp_git_dir}" && echo "Temp dir has been cleared"' EXIT

for sub in src include; do
    if [ -d "${REPO_DIR}/${sub}" ]; then
        mkdir -p "${tmp_git_dir}/${sub}"
        cp -a "${REPO_DIR}/${sub}/." "${tmp_git_dir}/${sub}/"
    fi
done

# ---------------------------------------------------------------------------
# Stage 2: Generate consolidated git patch via diff against patched_sqlite/
# ---------------------------------------------------------------------------
echo "========== Stage 2: Generate all_sqlite.patch =========="

if [ ! -d "${REFERENCE_DIR}" ]; then
    echo "ERROR: reference directory not found: ${REFERENCE_DIR}"
    exit 1
fi

# Check if there are any differences
diff_output=$(diff -r "${tmp_git_dir}" "${REFERENCE_DIR}" 2>&1) || true

if [ -z "${diff_output}" ]; then
    echo "No differences found between amalgamation source and reference directory."
    echo "No patch will be generated. Existing all_sqlite.patch (if any) is preserved."
else
    echo "Differences detected. Generating all_sqlite.patch ..."
    # Use a temporary git repo to produce a standard git-format patch.
    # Commit the old (amalgamation) state, then overlay the new
    # (patched_sqlite) state and commit again. Use 'git format-patch -1'
    # to generate a patch for the second commit (old -> new).
    # A trap ensures the temp dir is cleaned up even if 'set -e' exits early.
    cd "${tmp_git_dir}"
    git init -q
    git config user.name "tmp"
    git config user.email "tmp@tmp.com"
    git add -A
    git commit -q -m "old"
    # Remove old content and copy in new (reference) content
    find . -mindepth 1 -not -path './.git' -not -path './.git/*' -delete
    cp -a "${REFERENCE_DIR}/." "${tmp_git_dir}/"
    git add -A
    git commit -q -m "new"
    git format-patch -1 -o "${tmp_git_dir}" HEAD
    mv "${tmp_git_dir}"/*.patch "${OUTPUT_PATCH}"
    cd "${ORIGIN_DIR}"
    rm -rf "${tmp_git_dir}" && echo "Temp dir has been cleared"
    trap - EXIT
    echo "all_sqlite.patch has been saved to: ${OUTPUT_PATCH}"
fi

cd "${ORIGIN_DIR}"
echo "========== sqlite_patch_generator.sh completed =========="
