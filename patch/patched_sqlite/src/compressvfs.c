/*
** 2025-06-10
**
** The author disclaims copyright to this source code.  In place of
** a legal notice, here is a blessing:
**
**    May you do good and not evil.
**    May you find forgiveness for yourself and forgive others.
**    May you share freely, never taking more than you give.
**
*************************************************************************
**
** This file implements a VFS shim that compress each page's data
** of an SQLite database file. It will Open a OutterDB which used to
** manager compressed page data and create a vfs_pages table  into OutterDB.
** When read pages's data, the data will be selected from vfs_pages and
** decompress by compression.
**
** COMPILING
**
** This extension requires SQLite 3.44.4 or later. It uses the
** sqlite3_database_file_object() interface which was added in
** version 3.44.4, so it will not link with an earlier version of
** SQLite.
**
** To build this extension as a separately loaded shared library or
** DLL, use compiler command-lines similar to the following:
**
**    (linux)    gcc -fPIC -shared compressvfs.c -o sqlitecompressvfs.z.so
**
** You may want to add additional compiler options, of course,
** according to the needs of your project.
**
** LOADING
**
** To use this extension as a shared library, you first have to
** open SQLite database connection with compressvfs.
** It will load autometically when connect to OutterDB.
** And then compressvfs will be reegistered before open db connection.
** After first used , all subsequent databse connections that are opened
** will include this extension.
**
** Compressvfs is a VFS Shim. When loaded, "compressvfs" will registered based on
** default VFS. This is normally what you want.
**
** USING
**
** Open database connections using the sqlite3_open() with uri "?vfs=compressvfs" or
** sqlite3_open_v2() interfaces with zVfs parameter.
**
** For example:
**
**    sqlite3 *db;
**    sqlite3_open("file:example.db?vfs=compressvfs", &db);
**    or
**    sqlite3_open_v2("example.db", &db, flag, "compressvfs");
**
** After open databse connections with compressvfs, page data will be auto compressed.
** And data can be read and written normally.
**
*/
#ifdef SQLITE_ENABLE_PAGE_COMPRESS
#include <sqlite3sym.h>
SQLITE_EXTENSION_INIT1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stddef.h>
#include "securec.h"
#ifndef _WIN32
#include <dlfcn.h>
#endif

// export the symbols
#ifdef SQLITE_EXPORT_SYMBOLS
#if defined(__GNUC__)
#  define EXPORT_SYMBOLS  __attribute__ ((visibility ("default")))
#elif defined(_MSC_VER)
#  define EXPORT_SYMBOLS  __declspec(dllexport)
#else
#  define EXPORT_SYMBOLS
#endif
#endif

/*
** Useful datatype
*/
#ifndef UINT32_TYPE
# ifdef HAVE_UINT32_T
#  define UINT32_TYPE uint32_t
# else
#  define UINT32_TYPE unsigned int
# endif
#endif
#ifndef UINT16_TYPE
# ifdef HAVE_UINT16_T
#  define UINT16_TYPE uint16_t
# else
#  define UINT16_TYPE unsigned short int
# endif
#endif
#ifndef INT16_TYPE
# ifdef HAVE_INT16_T
#  define INT16_TYPE int16_t
# else
#  define INT16_TYPE short int
# endif
#endif
#ifndef UINT8_TYPE
# ifdef HAVE_UINT8_T
#  define UINT8_TYPE uint8_t
# else
#  define UINT8_TYPE unsigned char
# endif
#endif
#ifndef INT8_TYPE
# ifdef HAVE_INT8_T
#  define INT8_TYPE int8_t
# else
#  define INT8_TYPE signed char
# endif
#endif
#ifndef LONGDOUBLE_TYPE
# define LONGDOUBLE_TYPE long double
#endif
#ifdef SQLITE_INT64_TYPE
  typedef SQLITE_INT64_TYPE sqlite_int64;
# ifdef SQLITE_UINT64_TYPE
    typedef SQLITE_UINT64_TYPE sqlite_uint64;
# else
    typedef unsigned SQLITE_INT64_TYPE sqlite_uint64;
# endif
#elif defined(_MSC_VER) || defined(__BORLANDC__)
  typedef __int64 sqlite_int64;
  typedef unsigned __int64 sqlite_uint64;
#else
  typedef long long int sqlite_int64;
  typedef unsigned long long int sqlite_uint64;
#endif
typedef sqlite_int64 sqlite3_int64;
typedef sqlite_uint64 sqlite3_uint64;
typedef sqlite_int64 i64;          /* 8-byte signed integer */
typedef sqlite_uint64 u64;         /* 8-byte unsigned integer */
typedef UINT32_TYPE u32;           /* 4-byte unsigned integer */
typedef UINT16_TYPE u16;           /* 2-byte unsigned integer */
typedef INT16_TYPE i16;            /* 2-byte signed integer */
typedef UINT8_TYPE u8;             /* 1-byte unsigned integer */
typedef INT8_TYPE i8;              /* 1-byte signed integer */

typedef u32 Pgno;
/* VFS's name */
#define COMPRESS_VFS_NAME "compressvfs"
#define CKSM_VFS_NAME "cksmvfs"

/* COMPRESSION OPTIONS */
#define COMPRESSION_UNDEFINED 0
#define COMPRESSION_BROTLI    1
#define COMPRESSION_ZSTD      2

#define COMPRESSION_SQL_MAX_LENGTH 100

#define SQLITE_SHMMAP_IS_WRITE       0x00000001  /* Flag for xShmMap, extend file if necessary */
#define SQLITE_OPEN_COMPRESS_SHM     0x00010000  /* Flag for xShmMap, need to rename shm file */

#define SQLITE_WARNING_DUMP          (SQLITE_WARNING | (2<<8))

/* An open file */
typedef struct{
  sqlite3_file base;        /* IO methods */
  sqlite3* pDb;             /* Ptr to OutterDB */
  u8 bOutterDbOpen;         /* True to OutterDB is opened */
  u8 bSubDbOpen;            /* True to SubDB is opened */
  u8 bBegin;                /* True to xSync() need commit */
  u8 compression;           /* Compression options */
  int pageSize;             /* Uncompressed page size */
  int persistWalFlag;       /* Flag to persist flag */
  int openFlags;            /* Flag to open file */
  sqlite3_file *pLockFd;    /* File handle to lock file */
} CompressFile;


static int compressClose(sqlite3_file *pFile);
static int compressRead(sqlite3_file *pFile, void *pBuf, int iAmt, sqlite_int64 iOfst);
static int compressWrite(sqlite3_file *pFile, const void *pBuf, int iAmt, sqlite_int64 iOfst);
static int compressTruncate(sqlite3_file *pFile, sqlite_int64 size);
static int compressSync(sqlite3_file *pFile, int flags);
static int compressFileSize(sqlite3_file *pFile, i64 *pSize);
static int compressLock(sqlite3_file *pFile, int eFileLock);
static int compressUnlock(sqlite3_file *pFile, int eFileLock);
static int compressCheckReservedLock(sqlite3_file *pFile, int *pResOut);
static int compressFileControl(sqlite3_file *pFile, int op, void *pArg);
static int compressSectorSize(sqlite3_file *pFile);
static int compressDeviceCharacteristics(sqlite3_file *pFile);
static int compressShmMap(sqlite3_file *pFile, int iPg, int pgsz, int fileFlag, void volatile **pp);
static int compressShmLock(sqlite3_file *pFile, int offset, int n, int flags);
static void compressShmBarrier(sqlite3_file *pFile);
static int compressShmUnmap(sqlite3_file *pFile, int deleteFlag);
static int compressFetch(sqlite3_file *pFile, sqlite3_int64 iOfst, int iAmt, void **pp);
static int compressUnfetch(sqlite3_file *pFile, sqlite3_int64 iOfst, void *pPage);

static sqlite3_vfs compress_vfs = {0};
static sqlite3_io_methods compress_io_methods = {
  3,
  compressClose,
  compressRead,
  compressWrite,
  compressTruncate,
  compressSync,
  compressFileSize,
  compressLock,
  compressUnlock,
  compressCheckReservedLock,
  compressFileControl,
  compressSectorSize,
  compressDeviceCharacteristics,
  compressShmMap,
  compressShmLock,
  compressShmBarrier,
  compressShmUnmap,
  compressFetch,
  compressUnfetch
};

/*----------------------------brotli header begin----------------------------*/
#define BROTLI_BOOL int
#define BROTLI_TRUE 1
#define BROTLI_FALSE 0
#define BROTLI_MAX_WINDOW_BITS 24
typedef enum{
  // Decoding error
  BROTLI_DECODER_RESULT_ERROR = 0,
  // Decoding successfully completed.
  BROTLI_DECODER_RESULT_SUCCESS = 1,
  // Should be called again more input.
  BROTLI_DECODER_RESULT_NEEDS_MORE_INPUT = 2,
  // Should be called again more output.
  BROTLI_DECODER_RESULT_NEEDS_MORE_OUTPUT = 3,
} BrotliDecoderResult;

/** Options for ::BROTLI_PARAM_MODE parameter. */
typedef enum BrotliEncoderMde {
  /**
   * Default compression mode.
   *
   * In this mode compressor does not know anything in advance about the
   * properties of the input.
   */
  BROTLI_MODE_GENERIC = 0,
  /** Compression mode for UTF-8 formatted text input. */
  BROTLI_MODE_TEXT = 1,
  /** Compression mode used in WOFF 2.0 */
  BROTLI_MODE_FONT = 2
} BrotliEncoderMde;
/** Default value for ::BROTLI_PARAM_MODE parameter. */
#define BROTLI_DEFAULT_MODE BROTLI_MODE_GENERIC

typedef BROTLI_BOOL (*brotliCompress_ptr)(int, int, BrotliEncoderMde, size_t, const u8*, size_t*, u8*);
typedef BrotliDecoderResult (*brotliDecompress_ptr)(size_t, const u8*, size_t*, u8*);
/*----------------------------brotli header end----------------------------*/

/*----------------------------zstd header begin----------------------------*/
typedef size_t (*zstdCompress_ptr)(void*, size_t, const void*, size_t, int);
typedef size_t (*zstdDecompress_ptr)(void*, size_t, const void*, size_t);
/*----------------------------zstd header begin----------------------------*/

/*
** Access to a lower-level VFS that (might) implement dynamic loading, access to randomness, etc.
*/
#define ORIGFILE(p) ((sqlite3_file*)(((CompressFile*)(p))+1))
#define ORIGVFS(p) ((sqlite3_vfs*)((p)->pAppData))

static u32 g_compress_algo_load = COMPRESSION_UNDEFINED;
static void *g_compress_algo_library = NULL;
typedef size_t (*compressBound_ptr)(size_t);
static compressBound_ptr compressBoundPtr = NULL;

static brotliCompress_ptr brotliCompressPtr = NULL;
static brotliDecompress_ptr brotliDecompressPtr = NULL;
static zstdCompress_ptr zstdCompressPtr = NULL;
static zstdDecompress_ptr zstdDecompressPtr = NULL;

static int loadBrotliExtension(){
  g_compress_algo_library = dlopen("libbrotli_shared.z.so", RTLD_LAZY);
  if( g_compress_algo_library==NULL ){
    sqlite3_log(SQLITE_NOTICE, "load brotli so failed: %s", dlerror());
    return SQLITE_ERROR;
  }
  compressBoundPtr = (compressBound_ptr)dlsym(g_compress_algo_library, "BrotliEncoderMaxCompressedSize");
  if( compressBoundPtr==NULL ){
    goto failed;
  }
  brotliCompressPtr = (brotliCompress_ptr)dlsym(g_compress_algo_library, "BrotliEncoderCompress");
  if( brotliCompressPtr==NULL ){
    goto failed;
  }
  brotliDecompressPtr = (brotliDecompress_ptr)dlsym(g_compress_algo_library, "BrotliDecoderDecompress");
  if( brotliDecompressPtr==NULL ){
    goto failed;
  }
  g_compress_algo_load = COMPRESSION_BROTLI;
  return SQLITE_OK;

 failed:
  sqlite3_log(SQLITE_NOTICE, "load brotli dlsym failed :%s", dlerror());
  compressBoundPtr = NULL;
  brotliCompressPtr = NULL;
  brotliDecompressPtr = NULL;
  dlclose(g_compress_algo_library);
  return SQLITE_ERROR;
}

static int loadZstdExtension(){
  g_compress_algo_library = dlopen("libzstd.z.so", RTLD_LAZY);
  if( g_compress_algo_library==NULL ){
    sqlite3_log(SQLITE_NOTICE, "load zstd so failed :%s", dlerror());
    return SQLITE_ERROR;
  }
  compressBoundPtr = (compressBound_ptr)dlsym(g_compress_algo_library, "ZSTD_compressBound");
  if( compressBoundPtr==NULL ){
    goto failed;
  }
  zstdCompressPtr = (zstdCompress_ptr)dlsym(g_compress_algo_library, "ZSTD_compress");
  if( zstdCompressPtr==NULL ){
    goto failed;
  }
  zstdDecompressPtr = (zstdDecompress_ptr)dlsym(g_compress_algo_library, "ZSTD_decompress");
  if( zstdDecompressPtr==NULL ){
    goto failed;
  }
  g_compress_algo_load = COMPRESSION_ZSTD;
  return SQLITE_OK;

 failed:
  sqlite3_log(SQLITE_NOTICE, "load zstd dlsym failed :%s", dlerror());
  compressBoundPtr = NULL;
  zstdCompressPtr = NULL;
  zstdDecompressPtr = NULL;
  dlclose(g_compress_algo_library);
  return SQLITE_ERROR;
}

static int loadCompressAlgorithmExtension(u8 compression){
  if( g_compress_algo_load!=0u ){
    return SQLITE_OK;
  }
#ifndef _WIN32
  if( compression==COMPRESSION_UNDEFINED ){
    if( loadZstdExtension()==SQLITE_ERROR ){
      sqlite3_log(SQLITE_NOTICE, "load zstd failed :%s", dlerror());
      if( loadBrotliExtension()==SQLITE_ERROR ){
        sqlite3_log(SQLITE_ERROR, "load compress so failed :%s", dlerror());
        return SQLITE_ERROR;
      }
    }
  }else if( compression==COMPRESSION_BROTLI ){
    if( loadBrotliExtension()==SQLITE_ERROR ){
      sqlite3_log(SQLITE_ERROR, "load brotli so failed :%s", dlerror());
      return SQLITE_ERROR;
    }
  }else if( compression==COMPRESSION_ZSTD ){
    if( loadZstdExtension()==SQLITE_ERROR ){
      sqlite3_log(SQLITE_ERROR, "load zstd so failed :%s", dlerror());
      return SQLITE_ERROR;
    }
  }else{
    sqlite3_log(SQLITE_ERROR, "load compress so failed, compression is invalid :%u", compression);
    return SQLITE_ERROR;
  }
#endif
  return SQLITE_OK;
}

/* Get compress bound */
static int compressLen(int src_len, int compression){
  if( compression==COMPRESSION_BROTLI || compression==COMPRESSION_ZSTD ){
    return compressBoundPtr(src_len);
  }
  return -1;
}

/* Compress buf with compression */
static int compressBuf(
  u8 *dst,
  int dst_buf_len,
  int *dst_written_len,
  const u8 *src,
  int src_len,
  int compression
){
  int ret_len = 0;
  if( g_compress_algo_load==COMPRESSION_UNDEFINED &&
    loadCompressAlgorithmExtension(compression)==SQLITE_ERROR ){
    return SQLITE_ERROR;
  }
  if( g_compress_algo_load!=(u32)compression ){
    sqlite3_log(SQLITE_MISUSE, "already load %u, but need load %d", g_compress_algo_load, compression);
    return SQLITE_MISUSE;
  }
  if( compression==COMPRESSION_BROTLI ){
    size_t dst_len = dst_buf_len;
    int ret = brotliCompressPtr(
      3,                        // COMPRESS QUALITY (1-11)
      BROTLI_MAX_WINDOW_BITS,   // WINDOWS SIZE (10-24)
      BROTLI_DEFAULT_MODE,      // MODE(BROTLI_MODE_GENERIC, TEXT, FONT)
      (size_t)src_len,
      (const u8 *)src,
      &dst_len,
      (u8 *)dst);
    if( ret!=BROTLI_TRUE ){
      return SQLITE_ERROR;
    }
    ret_len = dst_len;
  }else if( compression==COMPRESSION_ZSTD ){
    ret_len = (int)zstdCompressPtr(dst, dst_buf_len, src, src_len, 3);
  }
  if( ret_len<=0 ){
    return SQLITE_ERROR;
  }
  *dst_written_len = ret_len;
  return SQLITE_OK;
}

/* Decompress buf with compression */
EXPORT_SYMBOLS int decompressBuf(
  u8 *dst,
  int dst_buf_len,
  int *dst_written_len,
  const u8 *src,
  int src_len,
  int compression
){
  int ret_len = -1;
  if( g_compress_algo_load==COMPRESSION_UNDEFINED &&
    loadCompressAlgorithmExtension(compression)==SQLITE_ERROR ){
    return SQLITE_ERROR;
  }
  if( g_compress_algo_load!=(u32)compression ){
    sqlite3_log(SQLITE_MISUSE, "already load %u, but need load %d", g_compress_algo_load, compression);
    return SQLITE_MISUSE;
  }
  if( compression==COMPRESSION_BROTLI ){
    size_t dst_len = dst_buf_len;
    int ret = (int)brotliDecompressPtr(src_len, src, &dst_len, dst);
    if( ret!=BROTLI_TRUE ){
      return SQLITE_ERROR;
    }
    ret_len = dst_len;
  }else if( compression==COMPRESSION_ZSTD ){
    ret_len = (int)zstdDecompressPtr(dst, dst_buf_len, src, src_len);
  }
  if( ret_len<=0 ){
    return SQLITE_ERROR;
  }
  *dst_written_len = ret_len;
  return SQLITE_OK;
}

static int compressConvertErrCode(int errCode){
  if( errCode==SQLITE_CORRUPT || errCode==SQLITE_BUSY ){
    return errCode;
  }
  return SQLITE_IOERR;
}

/* Check whether the table exists in the OutterDB. */
static int tableExists(sqlite3 *db, const char *table_name, u8 *isExist){
  sqlite3_stmt *stmt = NULL;
  const char *sql = "SELECT 1 FROM sqlite_master WHERE type='table' AND name=?;";

  int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if( rc!=SQLITE_OK ){
    sqlite3_log(rc, "Check table wrong while prepare stat");
    return compressConvertErrCode(rc);
  }
  sqlite3_bind_text(stmt, 1, table_name, -1, SQLITE_STATIC);
  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  if( rc!=SQLITE_ROW && rc!=SQLITE_DONE ){
    sqlite3_log(rc, "Check table wrong while step stat");
    return compressConvertErrCode(rc);
  }
  *isExist = (rc==SQLITE_ROW) ? 1 : 0;
  return SQLITE_OK;
}

/* Get page size before compressed from OutterDB. */
static int getCompressPgsize(sqlite3 *db, int *pagesize){
  int rc = SQLITE_OK;
  if( *pagesize!=0 ){
    return rc;
  }
  sqlite3_stmt *stmt = NULL;
  const char *sql = "SELECT pagesize FROM vfs_compression;";
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if( rc!=SQLITE_OK ){
    sqlite3_log(rc, "Compress db prepare to get pgsz wrong");
    return compressConvertErrCode(rc);
  }
  rc = sqlite3_step(stmt);
  if( rc!=SQLITE_ROW ){
    sqlite3_log(rc, "Compress db get pgsz wrong, expect at least one row");
    sqlite3_finalize(stmt);
    return compressConvertErrCode(rc);
  }
  *pagesize = sqlite3_column_int(stmt, 0);
  sqlite3_finalize(stmt);
  return SQLITE_OK;
}

/* Set page size before compressed to OutterDB. */
static int setCompressPgsize(sqlite3 *db, int pagesize){
  int rc = SQLITE_OK;
  sqlite3_stmt *stmt = NULL;
  const char *sql = "UPDATE vfs_compression SET pagesize=?;";

  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if( rc!=SQLITE_OK ){
    sqlite3_log(rc, "Compress db prepare update pgsz wrong, size:%d", pagesize);
    return compressConvertErrCode(rc);
  }
  sqlite3_bind_int(stmt, 1, pagesize);
  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  if( rc!=SQLITE_DONE ){
    sqlite3_log(rc, "Compress db update pgsz wrong, size:%d", pagesize);
    return compressConvertErrCode(rc);
  }
  return SQLITE_OK;
}

/* Get max page number from OutterDB. */
static int getMaxCompressPgno(sqlite3 *db){
  sqlite3_stmt *stmt = NULL;
  const char *sql = "SELECT MAX(pageno) FROM vfs_pages;";
  int mxValue = 0;

  int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if( rc!=SQLITE_OK ){
    sqlite3_log(SQLITE_WARNING_DUMP, "try get max pgno wrong while prepare stat, rc:%d", rc);
    return mxValue;
  }
  rc = sqlite3_step(stmt);
  if( rc!=SQLITE_ROW && rc!=SQLITE_DONE ){
    sqlite3_log(SQLITE_WARNING_DUMP, "try get max pgno wrong while step stat, rc:%d", rc);
    sqlite3_finalize(stmt);
    return mxValue;
  }
  if( rc==SQLITE_ROW ){  // May not exist any pages, if so, return 0, else fetch the value
    mxValue = sqlite3_column_int(stmt, 0);
  }
  sqlite3_finalize(stmt);
  return mxValue;
}

/* Get Compression option from OutterDB. */
static int getCompression(sqlite3 *db, CompressFile *pCompress){
  sqlite3_stmt *stmt = NULL;
  const char *sql = "SELECT count(*), compression, pagesize FROM vfs_compression;";
  int count = 0;
  pCompress->compression = COMPRESSION_ZSTD;
  pCompress->pageSize = 0;

  int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if( rc!=SQLITE_OK ){
    sqlite3_log(rc, "Compress db get compression wrong while prepare stat.");
    return compressConvertErrCode(rc);
  }
  rc = sqlite3_step(stmt);
  if( rc!=SQLITE_ROW && rc!=SQLITE_DONE ){
    sqlite3_log(rc, "Compress db get compression wrong while step stat.");
    sqlite3_finalize(stmt);
    return compressConvertErrCode(rc);
  }
  if( rc==SQLITE_DONE ){
    sqlite3_finalize(stmt);  // No compression options, use default
    return SQLITE_OK;
  }
  count = sqlite3_column_int(stmt, 0);
  if( count!=1 ){
    sqlite3_finalize(stmt);
    return SQLITE_IOERR;
  }
  pCompress->compression = sqlite3_column_int(stmt, 1);
  pCompress->pageSize = sqlite3_column_int(stmt, 2);
  sqlite3_finalize(stmt);
  return SQLITE_OK;
}

/*
** Sync a compress file. If need commit a transaction
** which begin in compressWrite or compressTruncate.
*/
static int compressSync(sqlite3_file *pFile, int flags){
  assert( pFile );
  CompressFile *pCompress = (CompressFile *)pFile;
  sqlite3 *db = pCompress->pDb;
  if( pCompress->bBegin==1 ){
    int rc = sqlite3_exec(db, "COMMIT;", NULL, NULL, NULL);
    if( rc!=SQLITE_OK ){
      return compressConvertErrCode(rc);
    }
    pCompress->bBegin = 0;
  }
  return SQLITE_OK;
}

/*
** Check if another file-handle holds a RESERVED lock on a compress file.
*/
static int compressCheckReservedLock(sqlite3_file *pFile, int *pResOut){
  assert( pFile );
  *pResOut = 0;
  return SQLITE_OK;
}

/*
** File control method. Use default VFS's xFileControl.
*/
static int compressFileControl(sqlite3_file *pFile, int op, void *pArg){
  assert( pFile );
  CompressFile *pCompress = (CompressFile *)pFile;
  if( op==SQLITE_FCNTL_PERSIST_WAL ){
    int persistFlag = *(int*)pArg;
    if( persistFlag<0 ){
      *(int*)pArg = pCompress->persistWalFlag;
    }else{
      pCompress->persistWalFlag = persistFlag>0 ? 1 : 0;
    }
    return SQLITE_OK;
  }
  pFile = ORIGFILE(pFile);
  return pFile->pMethods->xFileControl(pFile, op, pArg);
}

/*
** Return the sector-size in bytes for a file. Use default VFS's xSectorSize.
*/
static int compressSectorSize(sqlite3_file *pFile){
  assert( pFile );
  pFile = ORIGFILE(pFile);
  return pFile->pMethods->xSectorSize(pFile);
}

/*
** Return the device characteristic flags supported by a file.
** Use default VFS's xDeviceCharacteristics.
*/
static int compressDeviceCharacteristics(sqlite3_file *pFile){
  assert( pFile );
  pFile = ORIGFILE(pFile);
  return pFile->pMethods->xDeviceCharacteristics(pFile);
}

/*
** Lock a compress file.Never lock InnerDB, because it wil not control the database file.
*/
static int compressLock(sqlite3_file *pFile, int eFileLock){
  assert( pFile );
  CompressFile *pCompress = (CompressFile *)pFile;
  sqlite3_file *pSubFile = ORIGFILE(pFile);
  return pSubFile->pMethods->xLock(pCompress->pLockFd, eFileLock);
}

/*
** Unlock a compress file.Never unlock InnerDB, because it wil not control the database file.
*/
static int compressUnlock(sqlite3_file *pFile, int eFileLock){
  assert( pFile );
  CompressFile *pCompress = (CompressFile *)pFile;
  sqlite3_file *pSubFile = ORIGFILE(pFile);
  return pSubFile->pMethods->xUnlock(pCompress->pLockFd, eFileLock);
}

/*
** Get File's size. Here wil return InnerDB's file size.
*/
static int compressFileSize(sqlite3_file *pFile, i64 *pSize){
  assert( pFile );
  CompressFile *pCompress = (CompressFile *)pFile;
  sqlite3 *db = pCompress->pDb;
  int rc = getCompressPgsize(db, &pCompress->pageSize);
  if( rc!=SQLITE_OK ){
    sqlite3_log(rc, "compress db get pgsz wrong");
    return rc;
  }
  int pgsize = pCompress->pageSize;
  int maxpgno = getMaxCompressPgno(db);
  *pSize = (i64)maxpgno * pgsize;
  return SQLITE_OK;
}

/*
** Truncate a compress file by delete from vfs_pages.
*/
static int compressTruncate(sqlite3_file *pFile, sqlite_int64 size){
  assert( pFile );
  CompressFile *pCompress = (CompressFile *)pFile;
  sqlite3 *db = pCompress->pDb;
  int rc = getCompressPgsize(db, &pCompress->pageSize);
  if( rc!=SQLITE_OK ){
    sqlite3_log(rc, "Get pgsize wrong before truncate pages(%lld)", size);
    return rc;
  }
  int pgsize = pCompress->pageSize;
  if( pgsize==0 || size%pgsize!=0 ){
    return SQLITE_IOERR_TRUNCATE;
  }
  int pgno = size / pgsize;
  int maxPgno = getMaxCompressPgno(db);
  if( maxPgno<pgno ){
    sqlite3_log(SQLITE_CORRUPT, "Get max(%d) wrong before truncate pages(%d)", maxPgno, pgno);
    return SQLITE_CORRUPT;
  }
  if( pCompress->bBegin!=1 ){
    rc = sqlite3_exec(db, "BEGIN;", NULL, NULL, NULL);
    if( rc!=SQLITE_OK ){
      return compressConvertErrCode(rc);
    }
    pCompress->bBegin = 1;
  }
  sqlite3_stmt *stmt = NULL;
  const char *sql = "DELETE FROM vfs_pages WHERE pageno > ?;";
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if( rc!=SQLITE_OK ){
    sqlite3_log(rc, "Truncate page wrong while prepare stat, pgno:%d", pgno);
    return compressConvertErrCode(rc);
  }
  sqlite3_bind_int(stmt, 1, pgno);
  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  if( rc!=SQLITE_DONE ){
    sqlite3_log(rc, "Truncate page wrong while step stat, pgno:%d", pgno);
    return compressConvertErrCode(rc);
  }
  return SQLITE_OK;
}

/*
** Write one page of data to compress file at a time.
** It will be compressed and insert into vfs_pages in OutterDB.
*/
static int compressWrite(sqlite3_file *pFile, const void *pBuf, int iAmt, sqlite_int64 iOfst){
  assert( pFile );
  assert( iAmt>0 );
  CompressFile *pCompress = (CompressFile *)pFile;
  sqlite3 *db = pCompress->pDb;
  int rc = getCompressPgsize(db, &pCompress->pageSize);
  if( rc!=SQLITE_OK ){
    sqlite3_log(rc, "Missing pgsz(%d), write ofst:%lld, iAmt:%d, flags:%d", pCompress->pageSize,
      iOfst, iAmt, pCompress->openFlags);
    return rc;
  }

  if( pCompress->pageSize<=0 && iAmt >= 512 && iAmt <= 64*1024 && !(iAmt & (iAmt - 1)) ){
    // new compress db need set orignal db's pagesize
    rc = setCompressPgsize(db, iAmt);
    if( rc!=SQLITE_OK ){
      sqlite3_log(rc, "Save page size(%d) wrong, ofst:%lld, flags:%d", iAmt, iOfst, pCompress->openFlags);
      return rc;
    }
    pCompress->pageSize = iAmt;
  }
  int pgsize = pCompress->pageSize;
  if( pgsize==0 ){
    sqlite3_log(SQLITE_IOERR_WRITE, "Zero pageSize in compressWrite, iAmt:%d, iOfst:%lld", iAmt, iOfst);
    return SQLITE_IOERR_WRITE;
  }
  int pgno = iOfst / pgsize + 1;
  if( iAmt!=pgsize || iOfst % pgsize!=0 ){
    sqlite3_log(SQLITE_IOERR_WRITE, "Mismatch info, iAmt(%d), pgsz(%d), offset:%ld", iAmt, pgsize, iOfst);
    return SQLITE_IOERR_WRITE;
  }

  int maxSize = compressLen(iAmt, pCompress->compression);
  if( maxSize<=0 ){
    sqlite3_log(SQLITE_IOERR_WRITE, "Get compress size(%d) wrong, compression(%d)", maxSize, pCompress->compression);
    return SQLITE_IOERR_WRITE;
  }
  u8 *tmpData = sqlite3_malloc(maxSize);
  if( tmpData==NULL ){
    sqlite3_log(SQLITE_NOMEM, "Malloc size(%d) wrong", maxSize);
    return SQLITE_NOMEM;
  }
  int len = 0;
  if( compressBuf(tmpData, maxSize, &len, pBuf, iAmt, pCompress->compression) ){
    sqlite3_free(tmpData);
    sqlite3_log(SQLITE_IOERR_WRITE, "Compress buf wrong, pgno(%d), amt(%d), ofst(%lld)", pgno, iAmt, iOfst);
    return SQLITE_IOERR_WRITE;
  }
  if( pCompress->bBegin!=1 ){
    if( (rc = sqlite3_exec(db, "BEGIN;", NULL, NULL, NULL))!=SQLITE_OK ){
      sqlite3_free(tmpData);
      sqlite3_log(rc, "Begin transaction to insert compressed page wrong, pgno(%d), ofst(%lld)", pgno, iOfst);
      return compressConvertErrCode(rc);
    }
    pCompress->bBegin = 1;
  }
  sqlite3_stmt *stmt = NULL;
  const char *sql = "INSERT OR REPLACE INTO vfs_pages(data, pageno) VALUES (?,?);";
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if( rc!=SQLITE_OK ){
    sqlite3_free(tmpData);
    sqlite3_log(rc, "Prepare stat to insert page wrong, pgno(%d), ofst(%lld)", pgno, iOfst);
    return compressConvertErrCode(rc);
  }
  sqlite3_bind_blob(stmt, 1, tmpData, len, SQLITE_STATIC);
  sqlite3_bind_int(stmt, 2, pgno);
  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  sqlite3_free(tmpData);
  if( rc!=SQLITE_DONE ){
    sqlite3_log(rc, "Compress db insert page wrong, pgno(%d), ofst(%lld)", pgno, iOfst);
    return compressConvertErrCode(rc);
  }
  return SQLITE_OK;
}

/*
** Read data from compress file.
** It will be selected from vfs_pages in OutterDB and return a decompressed buf.
*/
static int compressRead(sqlite3_file *pFile, void *pBuf, int iAmt, sqlite_int64 iOfst){
  assert( pFile );
  assert( iOfst>=0 );
  assert( iAmt>0 );
  CompressFile *pCompress = (CompressFile *)pFile;
  if( pCompress->compression!=COMPRESSION_BROTLI && pCompress->compression!=COMPRESSION_ZSTD ){
    // nowaday only support brotli/zstd.
    return SQLITE_CORRUPT;
  }
  (void)memset_s(pBuf, iAmt, 0, iAmt);
  sqlite3 *db = pCompress->pDb;
  int rc = getCompressPgsize(db, &pCompress->pageSize);
  if( rc!=SQLITE_OK || pCompress->pageSize==0 ){
    sqlite3_log(SQLITE_WARNING_DUMP, "Missing pgsz(%d), read ofst(%lld), amt(%d), flags(%d)", pCompress->pageSize,
      iOfst, iAmt, pCompress->openFlags);
    return SQLITE_IOERR_SHORT_READ;
  }
  int pgsize = pCompress->pageSize;
  int pgno = iOfst / pgsize + 1;
  int dataidx = iOfst % pgsize;
  sqlite3_stmt *stmt = NULL;
  const char *sql = "SELECT data, length(data) FROM vfs_pages WHERE pageno=?;";
  const void *data = NULL;
  int dataLen = 0;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if( rc!=SQLITE_OK ){
    sqlite3_log(rc, "Prepare to get compressed page(%d) wrong, ofst(%lld)", pgno, iOfst);
    return SQLITE_CORRUPT;
  }
  u8 *decompressedData = NULL;
  if( pgsize!=iAmt ){
    decompressedData = sqlite3_malloc(pgsize);
    if( decompressedData==NULL ){
      sqlite3_finalize(stmt);
      sqlite3_log(SQLITE_NOMEM, "Malloc for decompress size(%d) wrong, amt(%d), ofst(%lld)", pgsize, iAmt, iOfst);
      return SQLITE_NOMEM;
    }
  }else{
    decompressedData = (u8*)pBuf;
  }

  int decompressLen = 0;
  sqlite3_bind_int(stmt, 1, pgno);
  rc = sqlite3_step(stmt);
  if( rc==SQLITE_ROW ){
    data = sqlite3_column_blob(stmt, 0);
    if( data==NULL ){
      rc = SQLITE_IOERR_SHORT_READ;
      sqlite3_log(rc, "Get compress page(%d) wrong, empty data, amt(%d), ofst(%lld)", pgno, iAmt, iOfst);
      goto END_OUT;
    }
    dataLen = sqlite3_column_int(stmt, 1);
    if( dataLen==0 ){
      rc = SQLITE_IOERR_SHORT_READ;
      sqlite3_log(rc, "Get compress page(%d) wrong, short data, amt(%d), ofst(%lld)", pgno, iAmt, iOfst);
      goto END_OUT;
    }
    if( decompressBuf(decompressedData, pgsize, &decompressLen, data, dataLen, pCompress->compression)!=SQLITE_OK ){
      rc = SQLITE_IOERR_SHORT_READ;
      sqlite3_log(rc, "Decompress page(%d) wrong, compression(%d), len(%d), amt(%d), ofst(%lld)", pgno,
        (int)pCompress->compression, dataLen, iAmt, iOfst);
      goto END_OUT;
    }
    if( decompressLen!=pgsize ){
      rc = SQLITE_IOERR_SHORT_READ;
      sqlite3_log(rc, "Decompress page(%d) wrong, size(%d), pgsz(%d), amt(%d), ofst(%lld)", pgno,
        decompressLen, pgsize, iAmt, iOfst);
      goto END_OUT;
    }
    if( pgsize!=iAmt ){
      rc = memcpy_s(pBuf, iAmt, decompressedData+dataidx, iAmt);
      if( rc!=SQLITE_OK ){
        rc = SQLITE_IOERR_SHORT_READ;
        sqlite3_log(rc, "Copy decompress page(%d) wrong, size(%d), pgsz(%d), amt(%d), ofst(%lld)", pgno,
          decompressLen, pgsize, iAmt, iOfst);
        goto END_OUT;
      }
    }
    rc = SQLITE_OK;
  }else if( rc==SQLITE_DONE ){
    rc = SQLITE_BUSY;
    sqlite3_log(SQLITE_WARNING_DUMP, "Missing page(%d) while try read, may busy now", pgno);
  }else if( rc!=SQLITE_OK ){
    sqlite3_log(rc, "Got page(%d) from compress db wrong", pgno);
  }

END_OUT:
  sqlite3_finalize(stmt);
  if( pgsize!=iAmt ){
    sqlite3_free(decompressedData);
  }

  return rc;
}

/*
** Close compress file. Need sync data and close OutterDB and InnerDB.
*/
static int compressClose(sqlite3_file *pFile){
  assert( pFile );
  CompressFile *pCompress = (CompressFile *)pFile;
  sqlite3 *db = pCompress->pDb;
  sqlite3_file *pSubFile = ORIGFILE(pFile);
  int rc = compressSync(pFile, 0);
  if( rc!=SQLITE_OK ){
    sqlite3_log(SQLITE_WARNING_DUMP, "Sync wrong while close compress file, rc:%d", rc);
  }
  if( pCompress->bOutterDbOpen ){
    if( db!=NULL ){
      rc = sqlite3_close_v2(db);
      if( rc!=SQLITE_OK ){
        sqlite3_log(rc, "compress db close wrong");
        return compressConvertErrCode(rc);
      }
      pCompress->pDb = NULL;
    }
  }
  if( pCompress->pLockFd ){
    pSubFile->pMethods->xClose(pCompress->pLockFd);
    sqlite3_free(pCompress->pLockFd);
    pCompress->pLockFd = NULL;
  }
  if( pCompress->bSubDbOpen ){
    rc = pSubFile->pMethods->xClose(pSubFile);
  }
  return rc;
}

/* 
** Create a shared memory file mapping. Need to rename the shm file, append a tail named "compress."
*/
static int compressShmMap(
  sqlite3_file *pFile,
  int iPg,
  int pgsz,
  int fileFlag,
  void volatile **pp
){
  assert( pFile );
  pFile = ORIGFILE(pFile);
  fileFlag |= SQLITE_OPEN_COMPRESS_SHM;
  return pFile->pMethods->xShmMap(pFile, iPg, pgsz, fileFlag, pp);
}

/* Perform locking on a shared-memory segment */
static int compressShmLock(sqlite3_file *pFile, int offset, int n, int flags){
  assert( pFile );
  pFile = ORIGFILE(pFile);
  flags |= SQLITE_OPEN_COMPRESS_SHM;
  return pFile->pMethods->xShmLock(pFile, offset, n, flags);
}

/* Memory barrier operation on shared memory */
static void compressShmBarrier(sqlite3_file *pFile){
  assert( pFile );
  pFile = ORIGFILE(pFile);
  return pFile->pMethods->xShmBarrier(pFile);
}

/* Unmap a shared memory segment */
static int compressShmUnmap(sqlite3_file *pFile, int deleteFlag){
  assert( pFile );
  pFile = ORIGFILE(pFile);
  deleteFlag |= SQLITE_OPEN_COMPRESS_SHM;
  return pFile->pMethods->xShmUnmap(pFile, deleteFlag);
}

/* Fetch a page of a memory-mapped file */
static int compressFetch(sqlite3_file *pFile, sqlite3_int64 iOfst, int iAmt, void **pp){
  assert( pFile );
  pFile = ORIGFILE(pFile);
  if( pFile->pMethods->iVersion>2 && pFile->pMethods->xFetch ){
    return pFile->pMethods->xFetch(pFile, iOfst, iAmt, pp);
  }
  *pp = 0;
  return SQLITE_OK;
}

/* Release a memory-mapped page */
static int compressUnfetch(sqlite3_file *pFile, sqlite3_int64 iOfst, void *pPage){
  assert( pFile );
  pFile = ORIGFILE(pFile);
  if( pFile->pMethods->iVersion>2 && pFile->pMethods->xUnfetch ){
    return pFile->pMethods->xUnfetch(pFile, iOfst, pPage);
  }
  return SQLITE_OK;
}

static int compressOpenLockFile(sqlite3_vfs *pVfs, const char *zName, int flags, sqlite3_file **pFile){
  const char *walPath = sqlite3_filename_wal(zName);
  const char *lockPath = walPath + strlen(walPath) + 1;  // For compress vfs, lock path place at this position
  int isLockExist = 0;
  int rc = ORIGVFS(pVfs)->xAccess(ORIGVFS(pVfs), lockPath, SQLITE_ACCESS_READWRITE, &isLockExist);
  if( rc!=SQLITE_OK ){
    sqlite3_log(rc, "Access lock file failed, path:%s", lockPath);
    return rc;
  }
  if( isLockExist==0 && ((flags&SQLITE_OPEN_CREATE)==0) ){
    sqlite3_log(SQLITE_WARNING_NOTCOMPRESSDB, "Check lock wrong, compress db should have lock file");
    return SQLITE_WARNING_NOTCOMPRESSDB;
  }
  if( pVfs->szOsFile<=(int)sizeof(CompressFile) ){
    sqlite3_log(SQLITE_CANTOPEN, "Unexpected file handle size(%d), vfs:%s", pVfs->szOsFile, pVfs->zName);
    return SQLITE_CANTOPEN;
  }
  sqlite3_file *pLockFd = sqlite3_malloc(pVfs->szOsFile - sizeof(CompressFile));
  if( pLockFd==NULL ){
    rc = SQLITE_NOMEM;
    sqlite3_log(rc, "Malloc file handle for lock wrong, size(%d)", (int)(pVfs->szOsFile - sizeof(CompressFile)));
    return rc;
  }
  memset(pLockFd, 0, pVfs->szOsFile - sizeof(CompressFile));
  rc = ORIGVFS(pVfs)->xOpen(ORIGVFS(pVfs), lockPath, pLockFd, flags, NULL);
  if( rc!=SQLITE_OK ){
    sqlite3_log(rc, "Compress vfs lock file open wrong, path:%s, flag(%d)", lockPath, flags);
    sqlite3_free(pLockFd);
    return rc;
  }
  *pFile = pLockFd;
  return rc;
}

static int compressDbInitCompression(CompressFile *pCompress, sqlite3 *db, u32 compression){
  const char *pragmaStr = "PRAGMA checksum_persist_enable=ON;PRAGMA page_size=4096;PRAGMA auto_vacuum=INCREMENTAL;";
  int rc = sqlite3_exec(db, pragmaStr, NULL, NULL, NULL);
  if( rc!=SQLITE_OK ){
    sqlite3_log(rc, "Config compress db wrong, name");
    return SQLITE_CANTOPEN;
  }
  const char *initCompressionStr = "CREATE TABLE IF NOT EXISTS vfs_compression(compression INTEGER, pagesize INTEGER);";
  rc = sqlite3_exec(db, initCompressionStr, NULL, NULL, NULL);
  if( rc!=SQLITE_OK ){
    sqlite3_log(rc, "Configure compress db wrong, create vfs_compression failed");
    return SQLITE_CANTOPEN;
  }
  char compressionSql[COMPRESSION_SQL_MAX_LENGTH] = {0};
  if( sprintf_s(compressionSql, COMPRESSION_SQL_MAX_LENGTH,
    "INSERT OR IGNORE INTO vfs_compression(compression, pagesize) VALUES (%u, 0);", compression)<=0 ){
    sqlite3_log(rc, "Concatenate config stat wrong, compression(%u)", compression);
    return SQLITE_CANTOPEN;
  }
  rc = sqlite3_exec(db, compressionSql, NULL, NULL, NULL);
  if( rc!=SQLITE_OK ){
    sqlite3_log(rc, "Config compress db wrong, insert compression(%u) failed", compression);
    return SQLITE_CANTOPEN;
  }
  pCompress->compression = compression;
  pCompress->pageSize = 0;
  return SQLITE_OK;
}

/*
** Open a compress file.If this file is not a journal or wal file,
** it will open a OutterDB and create vfs_pages and vfs_compression table
** which used to manager compressed pages's data.
*/
static int compressOpen(
  sqlite3_vfs *pVfs,
  const char *zName,
  sqlite3_file *pFile,
  int flags,
  int *pOutFlags
){
  sqlite3_file *pSubFile = ORIGFILE(pFile);
  CompressFile *pCompress = (CompressFile *)pFile;
  pCompress->openFlags = flags;
  int rc = SQLITE_OK;
  if( !(flags & (SQLITE_OPEN_MAIN_DB|SQLITE_OPEN_TEMP_DB)) ){
    return ORIGVFS(pVfs)->xOpen(ORIGVFS(pVfs), zName, pFile, flags, pOutFlags);
  }
  rc = ORIGVFS(pVfs)->xOpen(ORIGVFS(pVfs), zName, pSubFile, flags, pOutFlags);
  if( rc!=SQLITE_OK ){
    sqlite3_log(rc, "Compress file open wrong, path:%s, flags:%d", zName, flags);
    return rc;
  }
  pCompress->bSubDbOpen = 1;
  sqlite3_int64 fileSize = 0;
  u8 isExist = 0;
  sqlite3 *db = NULL;
  rc = pSubFile->pMethods->xFileSize(pSubFile, &fileSize);
  if( rc!=SQLITE_OK ){
    sqlite3_log(rc, "Calculate compress file size wrong, path:%s", zName);
    rc = SQLITE_CANTOPEN;
    goto END_OUT;
  }

  rc = compressOpenLockFile(pVfs, zName, flags, &pCompress->pLockFd);
  if( rc!=SQLITE_OK ){
    sqlite3_log(rc, "Compress vfs lock file open wrong, path:%s, flag(%d)", zName, flags);
    goto END_OUT;
  }
  pFile->pMethods = &compress_io_methods;
  rc = sqlite3_open_v2(zName, &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, CKSM_VFS_NAME);
  if( rc!=SQLITE_OK ){
    sqlite3_log(rc, "Open compress db wrong, name:%s", zName);
    rc = SQLITE_CANTOPEN;
    goto END_OUT;
  }
  sqlite3_busy_timeout(db, 2000);  // Set time out:2s
  rc = sqlite3_exec(db, "PRAGMA journal_mode=OFF;", NULL, NULL, NULL);
  if( rc!=SQLITE_OK ){
    sqlite3_log(rc, "Config compress db journal mode wrong, name:%s", zName);
    goto END_OUT;
  }
  pCompress->bOutterDbOpen = 1;
  rc = tableExists(db, "vfs_compression", &isExist);
  if( rc!=SQLITE_OK ) {
    goto END_OUT;
  }
  if( isExist ){
    rc = getCompression(db, pCompress);
    if( rc!=SQLITE_OK ){
      goto END_OUT;
    }
    if( pCompress->compression!=COMPRESSION_BROTLI && pCompress->compression!=COMPRESSION_ZSTD ){
      rc = SQLITE_CANTOPEN;
      sqlite3_log(rc, "Unrecognized compression(%d), name:%s", pCompress->compression, zName);
      goto END_OUT;
    }
    if( loadCompressAlgorithmExtension(pCompress->compression)!=SQLITE_OK ){
      rc = SQLITE_CANTOPEN;
      goto END_OUT;
    }
  }else if( flags&SQLITE_OPEN_MAIN_DB && fileSize!=0 ){
    rc = SQLITE_WARNING_NOTCOMPRESSDB;
    sqlite3_log(rc, "open compress database go wrong, it should be a compressed db");
    goto END_OUT;
  }else{
    if( loadCompressAlgorithmExtension(COMPRESSION_UNDEFINED)==SQLITE_ERROR ){
      rc = SQLITE_CANTOPEN;
      goto END_OUT;
    }
    rc = compressDbInitCompression(pCompress, db, g_compress_algo_load);
    if( rc!=SQLITE_OK ){
      sqlite3_log(rc, "Init compression info wrong, name:%s", zName);
      goto END_OUT;
    }
  }
  rc = tableExists(db, "vfs_pages", &isExist);
  if( rc!=SQLITE_OK ){
    goto END_OUT;
  }
  if( isExist ){
    goto END_OUT;
  }
  const char *pageDdlStr = "CREATE TABLE IF NOT EXISTS vfs_pages (pageno INTEGER PRIMARY KEY, data BLOB NOT NULL);";
  rc = sqlite3_exec(db, pageDdlStr, NULL, NULL, NULL);
  if( rc!=SQLITE_OK ){
    sqlite3_log(rc, "Config compress db wrong, name:%s, create page table failed", zName);
    rc = SQLITE_CANTOPEN;
  }

END_OUT:
  if( rc==SQLITE_OK ){
    pCompress->pDb = db;
  }else{
    if( db!=NULL ){
      sqlite3_close_v2(db);
    }
    if( pCompress->pLockFd ){
      pSubFile->pMethods->xClose(pCompress->pLockFd);
      sqlite3_free(pCompress->pLockFd);
      pCompress->pLockFd = NULL;
    }
  }
  return rc;
}

/*
** Delete compress file. If this file is journal or wal, need to delete the InnerDB's renamed file.
*/
static int compressDelete(sqlite3_vfs *pVfs, const char *zName, int syncDir){
  return ORIGVFS(pVfs)->xDelete(ORIGVFS(pVfs), zName, syncDir);
}

/*
** Access compress file. If this file is journal or wal need to access the InnerDB's renamed file.
*/
static int compressAccess(
  sqlite3_vfs *pVfs,
  const char *zName,
  int flags,
  int *pResOut
){
  return ORIGVFS(pVfs)->xAccess(ORIGVFS(pVfs), zName, flags, pResOut);
}


/*
** Init compressvfs and register the vfs
*/
EXPORT_SYMBOLS int sqlite3CompressvfsInit(){
  // copy default vfs for compressvfs
  if( sqlite3_vfs_find(COMPRESS_VFS_NAME) ){
    return SQLITE_OK;
  }
  sqlite3_vfs *default_vfs = sqlite3_vfs_find(0);
  if( default_vfs==0 ){
    sqlite3_log(SQLITE_ERROR, "Default vfs not found.");
    return SQLITE_ERROR;
  }
  sqlite3_vfs *pVfs = &compress_vfs;
  *pVfs = *default_vfs;
  pVfs->pNext = 0;
  pVfs->zName = COMPRESS_VFS_NAME;
  pVfs->pAppData = default_vfs;
  pVfs->szOsFile = pVfs->szOsFile+sizeof(CompressFile);

  pVfs->xOpen = compressOpen;
  pVfs->xDelete = compressDelete;
  pVfs->xAccess = compressAccess;
  int rc = sqlite3_vfs_register(pVfs, 0);
  if( rc!=SQLITE_OK ){
    sqlite3_log(SQLITE_ERROR, "Compress vfs register error!");
    return rc;
  }
  return SQLITE_OK;
}

EXPORT_SYMBOLS sqlite3_file *compressvfsGetOrigFile(sqlite3_file *file){
  return ORIGFILE(file);
}

#endif /* SQLITE_ENABLE_PAGE_COMPRESS */