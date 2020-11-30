/* vim: set noet sta sw=4 ts=4 : */

#include "mdbx_ext.h"

/* VALUE str = rb_sprintf( "path: %+"PRIsVALUE", opts: %+"PRIsVALUE, path, opts ); */
/* printf( "%s\n", StringValueCStr(str) ); */


/* Shortcut for fetching current DB variables.
 *
 */
#define UNWRAP_DB( val, db ) \
	rmdbx_db_t *db; \
	TypedData_Get_Struct( val, rmdbx_db_t, &rmdbx_db_data, db );


/*
 * A struct encapsulating an instance's DB state.
 */
struct rmdbx_db {
	MDBX_env *env;
	MDBX_dbi dbi;
	MDBX_txn *txn;
	MDBX_cursor *cursor;
	int open;
};
typedef struct rmdbx_db rmdbx_db_t;


/*
 * Ruby allocation hook.
 */
void rmdbx_free( void *db );  /* forward declaration */
static const rb_data_type_t rmdbx_db_data = {
	.wrap_struct_name = "MDBX::Database::Data",
	.function = { .dfree = rmdbx_free },
	.flags = RUBY_TYPED_FREE_IMMEDIATELY
};


/*
 * Allocate a DB environment onto the stack.
 */
VALUE
rmdbx_alloc( VALUE klass )
{
	int *data = malloc( sizeof(rmdbx_db_t) );
	return TypedData_Wrap_Struct( klass, &rmdbx_db_data, data );
}


/*
 * Cleanup a previously allocated DB environment.
 * FIXME:  ... this should also close if not already closed?
 */
void
rmdbx_free( void *db )
{
	if ( db ) free( db );
}


/*
 * Cleanly close an opened database.
 */
VALUE
rmdbx_close( VALUE self )
{
	UNWRAP_DB( self, db );
	if ( db->cursor ) mdbx_cursor_close( db->cursor );
	if ( db->txn )    mdbx_txn_abort( db->txn );
	if ( db->dbi )    mdbx_dbi_close( db->env, db->dbi );
	if ( db->env )    mdbx_env_close( db->env );
	db->open = 0;

	// FIXME: or rather, maybe free() from here?

	return Qtrue;
}


/*
 * call-seq:
 *    db.closed? #=> false
 *
 * Predicate: return true if the database has been closed,
 * (or never was actually opened for some reason?)
 */
VALUE
rmdbx_closed_p( VALUE self )
{
	UNWRAP_DB( self, db );
	return db->open == 1 ? Qfalse : Qtrue;
}


/*
 * call-seq:
 *    MDBX::Database.open( path ) -> db
 *    MDBX::Database.open( path, options ) -> db
 *    MDBX::Database.open( path, options ) do |db|
 *    		db...
 *    end
 *
 * Open an existing (or create a new) mdbx database at filesystem
 * +path+.  In block form, the database is automatically closed.
 *
 */
VALUE
rmdbx_database_initialize( int argc, VALUE *argv, VALUE self )
{
	int rc       = 0;
	int mode     = 0644;
	int db_flags = MDBX_ENV_DEFAULTS;
	VALUE path, opts, opt;

	rb_scan_args( argc, argv, "11", &path, &opts );

	/* Ensure options is a hash if it was passed in.
	 */
	if ( NIL_P(opts) ) {
		opts = rb_hash_new();
	}
	else {
		Check_Type( opts, T_HASH );
	}
	rb_hash_freeze( opts );

	/* Options setup, overrides.
	 */
	opt = rb_hash_aref( opts, ID2SYM( rb_intern("mode") ) );
	if ( ! NIL_P(opt) ) mode = FIX2INT( opt );
	opt = rb_hash_aref( opts, ID2SYM( rb_intern("nosubdir") ) );
	if ( RTEST(opt) ) db_flags = db_flags | MDBX_NOSUBDIR;
	opt = rb_hash_aref( opts, ID2SYM( rb_intern("readonly") ) );
	if ( RTEST(opt) ) db_flags = db_flags | MDBX_RDONLY;
	opt = rb_hash_aref( opts, ID2SYM( rb_intern("exclusive") ) );
	if ( RTEST(opt) ) db_flags = db_flags | MDBX_EXCLUSIVE;
	opt = rb_hash_aref( opts, ID2SYM( rb_intern("compat") ) );
	if ( RTEST(opt) ) db_flags = db_flags | MDBX_ACCEDE;
	opt = rb_hash_aref( opts, ID2SYM( rb_intern("writemap") ) );
	if ( RTEST(opt) ) db_flags = db_flags | MDBX_WRITEMAP;
	opt = rb_hash_aref( opts, ID2SYM( rb_intern("no_threadlocal") ) );
	if ( RTEST(opt) ) db_flags = db_flags | MDBX_NOTLS;
	opt = rb_hash_aref( opts, ID2SYM( rb_intern("no_readahead") ) );
	if ( RTEST(opt) ) db_flags = db_flags | MDBX_NORDAHEAD;
	opt = rb_hash_aref( opts, ID2SYM( rb_intern("no_memory_init") ) );
	if ( RTEST(opt) ) db_flags = db_flags | MDBX_NOMEMINIT;
	opt = rb_hash_aref( opts, ID2SYM( rb_intern("coalesce") ) );
	if ( RTEST(opt) ) db_flags = db_flags | MDBX_COALESCE;
	opt = rb_hash_aref( opts, ID2SYM( rb_intern("lifo_reclaim") ) );
	if ( RTEST(opt) ) db_flags = db_flags | MDBX_LIFORECLAIM;
	opt = rb_hash_aref( opts, ID2SYM( rb_intern("no_metasync") ) );
	if ( RTEST(opt) ) db_flags = db_flags | MDBX_NOMETASYNC;


	/* Initialize the DB vals.
	 */
	UNWRAP_DB( self, db );
	db->env    = NULL;
	db->dbi    = 0;
	db->txn    = NULL;
	db->cursor = NULL;
	db->open   = 0;

	/* Allocate an mdbx environment.
	 */
	rc = mdbx_env_create( &db->env );
	if ( rc != MDBX_SUCCESS )
		rb_raise( rmdbx_eDatabaseError, "mdbx_env_create: (%d) %s", rc, mdbx_strerror(rc) );

	/* Open the DB handle on disk.
	 */
	rc = mdbx_env_open( db->env, StringValueCStr(path), db_flags, mode );
	if ( rc != MDBX_SUCCESS ) {
		rmdbx_close( self );
		rb_raise( rmdbx_eDatabaseError, "mdbx_env_open: (%d) %s", rc, mdbx_strerror(rc) );
	}

	/* Set instance variables.
	 */
	db->open = 1;
	rb_iv_set( self, "@path", path );
	rb_iv_set( self, "@options", opts );

	return self;
}


/*
 * Initialization for the MDBX::Database class.
 */
void
rmdbx_init_database()
{
	rmdbx_cDatabase = rb_define_class_under( rmdbx_mMDBX, "Database", rb_cData );

	rb_define_alloc_func( rmdbx_cDatabase, rmdbx_alloc );

	rb_define_method( rmdbx_cDatabase, "close", rmdbx_close, 0 );
	rb_define_method( rmdbx_cDatabase, "closed?", rmdbx_closed_p, 0 );
	rb_define_protected_method( rmdbx_cDatabase, "initialize", rmdbx_database_initialize, -1 );

	rb_require( "mdbx/database" );
}

