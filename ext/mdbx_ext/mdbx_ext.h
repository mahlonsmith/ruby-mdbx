
#include <ruby.h>
#include "extconf.h"

#include "mdbx.h"

#ifndef MDBX_EXT_0_9_2
#define MDBX_EXT_0_9_2

#define RMDBX_TXN_ROLLBACK 0
#define RMDBX_TXN_COMMIT 1

/*
 * A struct encapsulating an instance's DB
 * state and settings.
 */
struct rmdbx_db {
	MDBX_env *env;
	MDBX_dbi dbi;
	MDBX_txn *txn;
	MDBX_cursor *cursor;

    struct {
       int env_flags;
       int mode;
       int open;
       int max_collections;
       int max_readers;
       uint64_t max_size;
    } settings;

    struct {
       int open;
    } state;

	char *path;
	char *subdb;
};
typedef struct rmdbx_db rmdbx_db_t;

static const rb_data_type_t rmdbx_db_data;
extern void rmdbx_free( void *db ); /* forward declaration for the allocator */

/* ------------------------------------------------------------
 * Globals
 * ------------------------------------------------------------ */

extern VALUE rmdbx_mMDBX;
extern VALUE rmdbx_cDatabase;
extern VALUE rmdbx_eDatabaseError;
extern VALUE rmdbx_eRollback;


/* ------------------------------------------------------------
 * Functions
 * ------------------------------------------------------------ */
extern void Init_rmdbx ( void );
extern void rmdbx_init_database ( void );
extern void rmdbx_open_txn( rmdbx_db_t*, int );
extern void rmdbx_close_txn( rmdbx_db_t*, int );

extern VALUE rmdbx_gather_stats( rmdbx_db_t* );


#endif /* define MDBX_EXT_0_9_2 */

