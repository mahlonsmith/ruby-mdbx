
#include <ruby.h>
#include "extconf.h"

#include "mdbx.h"
#include "ruby/thread.h"

#ifndef RBMDBX_EXT
#define RBMDBX_EXT

#define RMDBX_TXN_ROLLBACK 0
#define RMDBX_TXN_COMMIT 1

/* Shortcut for fetching wrapped data structure.
 */
#define UNWRAP_DB( self, db ) \
	rmdbx_db_t *db; \
	TypedData_Get_Struct( self, rmdbx_db_t, &rmdbx_db_data, db )

/* Raise if current DB is not open. */
#define CHECK_HANDLE() \
	if ( ! db->state.open ) rb_raise( rmdbx_eDatabaseError, "Closed database." )


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
       unsigned int env_flags;
       unsigned int db_flags;
       int mode;
       int open;
       int max_collections;
       int max_readers;
       uint64_t max_size;
    } settings;

    struct {
       int open;
       int retain_txn;
    } state;

	char *path;
	char *subdb;
};
typedef struct rmdbx_db rmdbx_db_t;

static const rb_data_type_t rmdbx_db_data;


/* ------------------------------------------------------------
 * Logging
 * ------------------------------------------------------------ */
#ifdef HAVE_STDARG_PROTOTYPES
#include <stdarg.h>
#define va_init_list(a,b) va_start(a,b)
void rmdbx_log_obj( VALUE, const char *, const char *, ... );
void rmdbx_log( const char *, const char *, ... );
#else
#include <varargs.h>
#define va_init_list(a,b) va_start(a)
void rmdbx_log_obj( VALUE, const char *, const char *, va_dcl );
void rmdbx_log( const char *, const char *, va_dcl );
#endif


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
extern void rmdbx_free( void *db ); /* forward declaration for the allocator */
extern void Init_rmdbx ( void );
extern void rmdbx_init_database ( void );
extern void rmdbx_close_all( rmdbx_db_t* );
extern void rmdbx_open_txn( rmdbx_db_t*, int );
extern void rmdbx_close_txn( rmdbx_db_t*, int );
extern void rmdbx_open_cursor( rmdbx_db_t* );
extern VALUE rmdbx_gather_stats( rmdbx_db_t* );


#endif /* define RBMDBX_EXT */

