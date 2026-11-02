#ifndef SQLITE_QUERY_CACHE_H
#define SQLITE_QUERY_CACHE_H

#include <pthread.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <string.h>
#include <fcntl.h>
#include <limits.h>

#ifdef __cplusplus
extern "C" {
#endif
typedef struct sqlite3_mutex sqlite3_mutex;
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

/* Default cache file size: 1MB */
#define DEFAULT_CACHE_FILE_SIZE (1 * 1024 * 1024u)
#define HOT_CACHE_BUFFER_SIZE (DEFAULT_CACHE_FILE_SIZE / 2)

/* Add 'u' suffix to ensure unsigned constants */
#define SHARED_BLOCK_PAGE_MAGIC 0x8B8B8B8B8B8B8B8BULL
#define SHARED_BLOCK_PAGE_VERSION 0x100u
#define SHARED_BLOCK_PAGE_INVALID_SLOTID 0xFFFFu
#define SHARED_BLOCK_PAGE_STEP_SIZE (128 * 1024u)

#define QCACHE_HOTSQL_REGISTER "hot_sql_register"
#define QCACHE_HOTSQL_UNREGISTER "hot_sql_unregister"
#define QCACHE_HOTSQL_INVALID_TIME "hot_sql_invalid_time"
#define QCACHE_HOTSQL_CACHE_SIZE "hot_sql_cache_size"
#define QCACHE_HOTSQL_ENABLE "hot_sql_enable"
#define QCACHE_HOTSQL_DEBUG "hot_sql_debug"
#define QCACHE_HOTSQL_INFO "hot_sql_info"
#define QCACHE_HOTSQL_PREFIX "hot_sql_"
#define QCACHE_PRAGMA_HOTSQL "pragma hot_sql_"
#define HOTSQL_DELE_ALL_CODE 0x9

#define QCACHE_FLAGS_IS_CHECKED     0x00000001
#define QCACHE_FLAGS_IS_HOTSQL      0x00000002
#define QCACHE_FLAGS_IS_TOOBIG      0x00000004

typedef struct QCacheEntry QCacheEntry;
typedef struct SBlkPage SBlkPage;
typedef struct SBlkPgHead SBlkPgHead;
typedef struct SBlkRowHead SBlkRowHead;
typedef enum SBlkRowType SBlkRowType;
typedef enum SBlkRowState SBlkRowState;
typedef u16 SBlkSlotID;
typedef u32 SBlkRowAddr;

/*
** Types of SharedBlockRow
*/
enum SBlkRowType {
    SBLKR_TYPE_SQL = 0, /* Hot SQL statement row */
    SBLKR_TYPE_DATA,    /* Hot SQL result set data row */
    SBLKR_TYPE_MAX
};

/*
** States of SharedBlockRow
*/
enum SBlkRowState {
    SBLKR_STATE_UNINIT = 0, /* Row not initialized */
    SBLKR_STATE_USED,       /* Row is in use */
    SBLKR_STATE_DELETE,     /* Row is deleted */
    SBLKR_STATE_MAX
};


struct QCacheVersion {
    u32 schemaCookie;  /* Schema version number of main database */
    u32 changeCounter; /* Change counter in non-WAL mode */
    u32 walSalt1;      /* WAL mode salt value for data changes */
    u32 walSalt2;
};
typedef struct QCacheVersion QCacheVersion;

/*
** SharedBlockRowHead structure definition
*/
struct SBlkRowHead {
    SBlkRowState state; /* Row state: used, free, deleted, invalid */
    SBlkRowType type;   /* Row type: SQL or DATA */
    u32 len;            /* Total length of this row */
    QCacheVersion ver;
};

/*
** +---------------------------------------------+
** |  cksum    |  SBlkPgHead |  row1  |   row2   |
** +-------------------                          +
** |beginPos                                     |
** |                    Data area                |
** |                                             |
** +                            -----------------+
** |         endPos|slot2|slot1|    SBlkPgTail   |
** +---------------------------------------------+
*/
struct SBlkPgHead {
    u64 magic;          /* Magic number to verify page initialization */
    u32 version;        /* Version number for cache file compatibility */
    u32 startOffset;    /* Offset to row data start, sizeof(SBlkPgHead) */
    u32 tailOffset;     /* Offset to page tail (SBlkPgTail) in page */
    u32 beginPos;       /* Start offset of free space */
    u32 endPos;         /* End offset of free space */
    u32 totalSize;      /* Total size including page overhead */
    u32 freeSize;       /* Available free space */
    u32 maxRowSize;     /* Maximum row size, rows exceeding this are not stored */
    SBlkSlotID slotCnt; /* Number of allocated slots */
    u16 reserve;
};

/*
** SharedBlockPage tail structure definition
*/
typedef struct SBlkPgTail {
    u64 magic; /* Magic number to verify page validity */
} SBlkPgTail;

/*
** SharedBlockPage structure definition
*/
struct SBlkPage {
    u32 checksum;   /* Checksum for data tampering/damage detection */
    u32 restoreMode; /* the cache page restore mode, 0-new created, 1-restore from file */
    SBlkPgHead head;
};

typedef struct QHashTable QHashTable;
typedef struct QHashNode QHashNode;
#define QHASH_BUCKET_SIZE 32u
#define QHASH_CALC_MAGIC 5381u
#define QHASH_HIT_LFU_CYCLE (6 * 60 * 60u)

/*
** QHashNode member definitions
*/
struct QHashNode {
    SBlkSlotID sqlSlotId;
    SBlkSlotID dataSlotId;
    u32 hitSqlCnt;
    u32 hitDataCnt;
    QHashNode *next;
};

/*
** QHashTable structure definition
*/
struct QHashTable {
    int lockFd; /* File lock handle */
    u32 bucketCnt;
    u32 maskCode;
    u32 calcMagic;
    u32 curMemSize; /* Current accessible page memory size, can dynamically
                    ** grow until maxMemSize or other shared process's maxMemSize */
    u32 maxMemSize; /* User-specified (or default) maximum accessible page memory size */
    QHashNode *bucket[QHASH_BUCKET_SIZE];
    SBlkPage *page;
    void *(*malloc)(int);
    void (*free)(void *);
    int (*lockPage)(int, u8);
    void (*unLockPage)(int);
    SBlkPage *(*expandPage4Free)(void *, u32);
    int (*compareRow4Free)(SBlkPage *, SBlkSlotID, const char *, u8 *);
    int (*insertRow4Free)(SBlkPage *, SBlkRowType, QCacheVersion *, u8 *, u32, SBlkSlotID *);
    int (*deleteRow4Free)(SBlkPage *, SBlkRowType, SBlkSlotID);
    int (*updateSqlRow4Free)(SBlkPage *, SBlkSlotID, SBlkSlotID);
    int (*copyRowData4Free)(SBlkPage *, SBlkSlotID, QCacheVersion *, u8 **, u32 *);
    int (*compressPage4Free)(SBlkPage *, int *);
};

typedef enum qCacheInitMode {
    QCACHE_DATA_INIT = 0, /* Fully initialize cache */
    QCACHE_DATA_RECOVERY, /* Recover cache from historical data */
    QCACHE_DATA_MAX
} qCacheInitMode;

/*
** QCacheEntry structure definition for hot SQL query cache layer
*/
struct QCacheEntry {
    struct timespec ts;      /* Instance startup time */
    struct stat dbFstat;     /* Database file permissions */
    QHashTable sql2Data;     /* key: complete SQL query, value: query result set.
                              ** Set by background thread */
    u32 ttlCycle;            /* Refresh period for cache hit count (seconds) */
    u32 qCacheMemSize;       /* Shared memory address space size */
    u32 cacheFileSize;       /* Cache file size (user-specified or default 1MB) */
    int inAccessible;
    int fd;                  /* Cache file handle, opened/closed by background thread */
    int multiInstFd;
    u8 isMultiInst;          /* Multiple instances detected on startup */
    char *QCacheMapFPath; /* Cache file path, created and deleted by main worker */
    char *QCacheInsFlock; /* Cache instance file path, created and deleted by main worker */
};

// ==============================================================================================================
// ------------------------------
// Row header (fixed length)
// ------------------------------
typedef struct __attribute__((packed)) RowHeader {
    int nCol; /* Number of columns */
} RowHeader;

// ------------------------------
// Column header (fixed length)
// ------------------------------
typedef struct __attribute__((packed)) ColHeader {
    int type;  /* SQLITE_INTEGER/TEXT/FLOAT/BLOB/NULL */
    int bytes; /* Data length */
} ColHeader;

// ------------------------------
// Entire contiguous memory block = result set
// ------------------------------
typedef struct CacheBuffer {
    int countRows;
    int totalRows; /* Total number of rows */
    int totalSize; /* Total size of buffer */
    char data[];   /* Flexible array: all rows stored contiguously */
} CacheBuffer;

/*
** [CacheBuffer]
** [Row1][Row2][Row3]...
** Each row format:
** RowHeader + [ColHeader + data][ColHeader + data]...
*/
// ==============================================================================================================
// ==============================================================================================================
typedef struct QCacheMemBlock QCacheMemBlock;
typedef struct QCacheMemBlockPool QCacheMemBlockPool;
struct QCacheMemBlock {
    int isFromPool;         // 1: from pool, 0: from heap. If heap: free when done; if pool: return to pool list
    int dataLen;            // Data length, fixed value 512
    QCacheMemBlock *next;   // Next pointer for pool free list (used when isFromPool=1)
    char data[0];           // Flexible array member for actual data
};

struct QCacheMemBlockPool {
    pthread_mutex_t mutex;  // Mutex for thread-safe pool operations
    int maxCount;           // Maximum number of blocks the pool can manage (e.g., 10)
    int totalCount;         // Total number of blocks currently allocated from this pool
    int freeCount;          // Number of free blocks currently available in the pool list
    QCacheMemBlock *pool;   // Free list of pool-managed memory blocks
};

// ==============================================================================================================

typedef enum QCacheMsgType QCacheMsgType;

/* Async message types */
enum QCacheMsgType {
    QCACHE_MSG_INIT,        /* Start: enable persistent cache */
    QCACHE_MSG_EXIT,        /* Exit: disable persistent cache */
    QCACHE_MSG_REG_SQL,     /* Register hot SQL */
    QCACHE_MSG_UNREG_SQL,   /* Unregister hot SQL */
    QCACHE_MSG_UPDATE_DATA, /* Update SQL result data */
    QCACHE_MSG_INVALID_ALL, /* Global change: invalidate all SQL caches */
    QCACHE_MSG_DELETE_ALL,
    QCACHE_MSG_DEBUG,
    QCACHE_MSG_INFO,
    QCACHE_MSG_INFO_ALL,
    QCACHE_MSG_BUTTON
};

#ifdef __cplusplus
}
#endif
#endif
