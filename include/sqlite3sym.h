/*
 * Copyright (c) 2021 Huawei Device Co., Ltd.
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

#ifndef SQLITE3SYM_H
#define SQLITE3SYM_H

#define SQLITE3_EXPORT_SYMBOLS
#define SQLITE_OMIT_LOAD_EXTENSION

// We extend the original purpose of the "sqlite3ext.h".
#include "sqlite3ext.h"

/*************************************************************************
** BINLOG CONFIG
*/
#define SQLITE_DBCONFIG_ENABLE_BINLOG    2006 /* Sqlite3BinlogConfig */

typedef enum BinlogFileCleanMode {
  BINLOG_FILE_CLEAN_ALL_MODE = 0,
  BINLOG_FILE_CLEAN_READ_MODE = 1,
  BINLOG_FILE_CLEAN_MODE_MAX,
} BinlogFileCleanModeE;

typedef struct BinlogReplayConfig {
  int batchNum;
  int batchWaitTime;
} BinlogReplayConfig;

typedef enum {
  ROW = 0,
} Sqlite3BinlogMode;

typedef struct Sqlite3BinlogConfig {
    Sqlite3BinlogMode mode;
    unsigned short fullCallbackThreshold;
    unsigned int maxFileSize;
    void (*xErrorCallback)(void *pCtx, int errNo, char *errMsg, const char *dbPath);
    void (*xLogFullCallback)(void *pCtx, unsigned short currentCount, const char *dbPath);
    void *callbackCtx;
} Sqlite3BinlogConfig;
/*
** END OF BINLOG CONFIG
*************************************************************************/
typedef struct {
  // aes-256-gcm, aes-256-cbc
  const void *pCipher;
  // SHA1, SHA256, SHA512
  const void *pHmacAlgo;
  // KDF_SHA1, KDF_SHA256, KDF_SHA512
  const void *pKdfAlgo;
  const void *pKey;
  int nKey;
  int kdfIter;
  int pageSize;
} CodecConfig;

typedef struct {
  const char *dbPath;
  CodecConfig dbCfg;
  CodecConfig rekeyCfg;
} CodecRekeyConfig;

struct sqlite3_api_routines_extra {
  int (*initialize)();
  int (*config)(int,...);
  int (*key)(sqlite3*,const void*,int);
  int (*key_v2)(sqlite3*,const char*,const void*,int);
  int (*rekey)(sqlite3*,const void*,int);
  int (*rekey_v2)(sqlite3*,const char*,const void*,int);
  int (*rekey_v3)(CodecRekeyConfig *);
  int (*is_support_binlog)(const char*);
  int (*replay_binlog)(sqlite3*, sqlite3*);
  int (*replay_binlog_v2)(sqlite3*, sqlite3*, BinlogReplayConfig*);
  int (*clean_binlog)(sqlite3*, BinlogFileCleanModeE);
  int (*compressdb_backup)(sqlite3*, const char*);
};

extern const struct sqlite3_api_routines_extra *sqlite3_export_extra_symbols;
#define sqlite3_initialize          sqlite3_export_extra_symbols->initialize
#define sqlite3_config              sqlite3_export_extra_symbols->config
#define sqlite3_key                 sqlite3_export_extra_symbols->key
#define sqlite3_key_v2              sqlite3_export_extra_symbols->key_v2
#define sqlite3_rekey               sqlite3_export_extra_symbols->rekey
#define sqlite3_rekey_v2            sqlite3_export_extra_symbols->rekey_v2
#define sqlite3_rekey_v3            sqlite3_export_extra_symbols->rekey_v3
#define sqlite3_is_support_binlog   sqlite3_export_extra_symbols->is_support_binlog
#define sqlite3_replay_binlog       sqlite3_export_extra_symbols->replay_binlog
#define sqlite3_replay_binlog_v2    sqlite3_export_extra_symbols->replay_binlog_v2
#define sqlite3_clean_binlog        sqlite3_export_extra_symbols->clean_binlog
#define sqlite3_compressdb_backup   sqlite3_export_extra_symbols->compressdb_backup

struct sqlite3_api_routines_cksumvfs {
  int (*register_cksumvfs)(const char *);
  int (*unregister_cksumvfs)();
};
extern const struct sqlite3_api_routines_cksumvfs *sqlite3_export_cksumvfs_symbols;
#define sqlite3_register_cksumvfs sqlite3_export_cksumvfs_symbols->register_cksumvfs
#define sqlite3_unregister_cksumvfs sqlite3_export_cksumvfs_symbols->unregister_cksumvfs

#endif
