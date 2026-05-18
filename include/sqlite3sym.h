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

typedef enum {
  ROW = 0,
  ROW_FOR_SEARCH = 2
} Sqlite3BinlogMode;

typedef struct Sqlite3BinlogConfig {
    Sqlite3BinlogMode mode;
    unsigned short fullCallbackThreshold;
    unsigned int maxFileSize;
    void (*xErrorCallback)(void *pCtx, int errNo, char *errMsg, const char *dbPath);
    void (*xLogFullCallback)(void *pCtx, unsigned short currentCount, const char *dbPath);
    void *callbackCtx;
} Sqlite3BinlogConfig;

typedef struct BinlogSearchResult {
  int op;                // one of the SQLITE_INSERT, SQLITE_UPDATE, SQLITE_DELETE
  sqlite3_int64 rowid;   // rowid of changed data, -1 for without rowid table
  sqlite3_uint64 nCol;  // size of pData
  const char *tableName;
  int fileIndex;
  sqlite3_uint64 readPos;
  char **nameAndValues; // pointer to the row data in record format
} BinlogSearchResult;

typedef struct BinlogSearchResultSet{
  int capacity;      // capacity of data
  int row_count;     // count of data
  BinlogSearchResult *results;       // results array
} BinlogSearchResultSet;

typedef enum BinlogHwmSetMode {
  BINLOG_PERSIST_MODE = 0,
  BINLOG_CACHE_MODE = 1,
  BINLOG_MODE_MAX,
} BinlogHwmSetModeE;

typedef struct BinlogSearchHwm {
  int readFileIndex;
  sqlite3_uint64 readPos;
} BinlogSearchHwmT;

typedef struct MonitorTableCol {
  const char *tableName;    // tableName
  char **cols;        // colsArray
  int colCount;       // colsCount
} MonitorTableCol;

typedef struct MonitorTablesConfig {
  MonitorTableCol *tables;  // tableCols
  int tableCount;           // tableCount
} MonitorTablesConfig;
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
  int (*set_monitor_config)(sqlite3*, MonitorTablesConfig*);
  int (*set_xChange_callback)(sqlite3*, void (*xChangeCallback)(const char *dbPath, char *tableName));
  int (*set_json_parse_callback)(sqlite3*, MonitorTablesConfig*(*jsonParseCallback)(const char *dbPath));
  int (*get_search_data)(sqlite3*, sqlite3*, BinlogSearchResultSet**);
  int (*free_search_data)(sqlite3*, BinlogSearchResultSet**);
  int (*set_search_hwm)(sqlite3*, BinlogSearchHwmT*, BinlogHwmSetModeE);
  int (*reset_search_hwm)(sqlite3*);
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
#define sqlite3_set_monitor_config_binlog  sqlite3_export_extra_symbols->set_monitor_config
#define sqlite3_set_xChange_callback_binlog  sqlite3_export_extra_symbols->set_xChange_callback
#define sqlite3_set_json_parse_callback_binlog  sqlite3_export_extra_symbols->set_json_parse_callback
#define sqlite3_get_search_data_binlog  sqlite3_export_extra_symbols->get_search_data
#define sqlite3_free_search_data_binlog sqlite3_export_extra_symbols->free_search_data
#define sqlite3_set_search_hwm_binlog   sqlite3_export_extra_symbols->set_search_hwm
#define sqlite3_reset_search_hwm_binlog sqlite3_export_extra_symbols->reset_search_hwm
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
