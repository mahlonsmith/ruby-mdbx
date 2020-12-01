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
	int env_flags;
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
	rmdbx_db_t *data;
	return TypedData_Make_Struct( klass, rmdbx_db_t, &rmdbx_db_data, data );
}


/*
 * Ensure all database file descriptors are collected and
 * removed.
 */
void
rmdbx_destroy( rmdbx_db_t* db )
{
	if ( db->cursor ) mdbx_cursor_close( db->cursor );
	if ( db->txn )    mdbx_txn_abort( db->txn );
	if ( db->dbi )    mdbx_dbi_close( db->env, db->dbi );
	if ( db->env )    mdbx_env_close( db->env );
	db->open = 0;
}


/*
 * Cleanup a previously allocated DB environment.
 */
void
rmdbx_free( void *db )
{
	if ( db ) {
		rmdbx_destroy( db );
		free( db );
	}
}


/*
 * Cleanly close an opened database.
 */
VALUE
rmdbx_close( VALUE self )
{
	UNWRAP_DB( self, db );
	rmdbx_destroy( db );
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
 * Open a new database transaction.
 *
 * +rwflag+ must be either MDBX_TXN_RDONLY or MDBX_TXN_READWRITE.
 */
void
rmdbx_open_txn( VALUE self, int rwflag )
{
	int rc;
	UNWRAP_DB( self, db );

	rc = mdbx_txn_begin( db->env, NULL, rwflag, &db->txn);
	if ( rc != MDBX_SUCCESS ) {
		rmdbx_close( self );
		rb_raise( rmdbx_eDatabaseError, "mdbx_txn_begin: (%d) %s", rc, mdbx_strerror(rc) );
	}

	if ( db->dbi == 0 ) {
		// FIXME: dbi_flags
		rc = mdbx_dbi_open( db->txn, NULL, 0, &db->dbi );
		if ( rc != MDBX_SUCCESS ) {
			rmdbx_close( self );
			rb_raise( rmdbx_eDatabaseError, "mdbx_dbi_open: (%d) %s", rc, mdbx_strerror(rc) );
		}
	}

	return;
}


/*
 * Given a ruby +arg+, convert and return a structure
 * suitable for usage as a key for mdbx.
 */
MDBX_val
rmdbx_vec_for( VALUE arg )
{
	MDBX_val rv;

	// FIXME: arbitrary data types!
	rv.iov_len  = RSTRING_LEN( arg );
	rv.iov_base = StringValuePtr( arg );

	return rv;
}


/* call-seq:
 *    db[ 'key' ]  #=> value
 *
 * Convenience method:  return a single value for +key+ immediately.
 */
VALUE
rmdbx_get_val( VALUE self, VALUE key )
{
	int rc;
	UNWRAP_DB( self, db );

	if ( RTEST(rmdbx_closed_p(self)) ) rb_raise( rmdbx_eDatabaseError, "Closed database." );

	rmdbx_open_txn( self, MDBX_TXN_RDONLY );

	MDBX_val ckey = rmdbx_vec_for( key );
	MDBX_val data;
	rc = mdbx_get( db->txn, db->dbi, &ckey, &data );
	mdbx_txn_abort( db->txn );

	switch ( rc ) {
		case MDBX_SUCCESS:
			// FIXME: arbitrary data types!
			return rb_str_new2( data.iov_base );

		case MDBX_NOTFOUND:
			return Qnil;

		default:
			rmdbx_close( self );
			rb_raise( rmdbx_eDatabaseError, "Unable to fetch value: (%d) %s", rc, mdbx_strerror(rc) );
	}
}


/* call-seq:
 *    db[ 'key' ] = value #=> value
 *
 * Convenience method:  set a single value for +key+
 */
VALUE
rmdbx_put_val( VALUE self, VALUE key, VALUE val )
{
	int rc;
	UNWRAP_DB( self, db );

	if ( RTEST(rmdbx_closed_p(self)) ) rb_raise( rmdbx_eDatabaseError, "Closed database." );

	rmdbx_open_txn( self, MDBX_TXN_READWRITE );
	MDBX_val ckey = rmdbx_vec_for( key );

	// FIXME: DUPSORT is enabled -- different api?
	// See:  MDBX_NODUPDATA / MDBX_NOOVERWRITE
	if ( NIL_P(val) ) { /* remove if set to nil */
		rc = mdbx_del( db->txn, db->dbi, &ckey, NULL );
	}
	else {
		MDBX_val old;
		MDBX_val data = rmdbx_vec_for( val );
		rc = mdbx_replace( db->txn, db->dbi, &ckey, &data, &old, 0 );
	}

	mdbx_txn_commit( db->txn );

	switch ( rc ) {
		case MDBX_SUCCESS:
			return val;
		default:
			rb_raise( rmdbx_eDatabaseError, "Unable to store value: (%d) %s", rc, mdbx_strerror(rc) );
	}
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
	int env_flags = MDBX_ENV_DEFAULTS;
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
	if ( RTEST(opt) ) env_flags = env_flags | MDBX_NOSUBDIR;
	opt = rb_hash_aref( opts, ID2SYM( rb_intern("readonly") ) );
	if ( RTEST(opt) ) env_flags = env_flags | MDBX_RDONLY;
	opt = rb_hash_aref( opts, ID2SYM( rb_intern("exclusive") ) );
	if ( RTEST(opt) ) env_flags = env_flags | MDBX_EXCLUSIVE;
	opt = rb_hash_aref( opts, ID2SYM( rb_intern("compat") ) );
	if ( RTEST(opt) ) env_flags = env_flags | MDBX_ACCEDE;
	opt = rb_hash_aref( opts, ID2SYM( rb_intern("writemap") ) );
	if ( RTEST(opt) ) env_flags = env_flags | MDBX_WRITEMAP;
	opt = rb_hash_aref( opts, ID2SYM( rb_intern("no_threadlocal") ) );
	if ( RTEST(opt) ) env_flags = env_flags | MDBX_NOTLS;
	opt = rb_hash_aref( opts, ID2SYM( rb_intern("no_readahead") ) );
	if ( RTEST(opt) ) env_flags = env_flags | MDBX_NORDAHEAD;
	opt = rb_hash_aref( opts, ID2SYM( rb_intern("no_memory_init") ) );
	if ( RTEST(opt) ) env_flags = env_flags | MDBX_NOMEMINIT;
	opt = rb_hash_aref( opts, ID2SYM( rb_intern("coalesce") ) );
	if ( RTEST(opt) ) env_flags = env_flags | MDBX_COALESCE;
	opt = rb_hash_aref( opts, ID2SYM( rb_intern("lifo_reclaim") ) );
	if ( RTEST(opt) ) env_flags = env_flags | MDBX_LIFORECLAIM;
	opt = rb_hash_aref( opts, ID2SYM( rb_intern("no_metasync") ) );
	if ( RTEST(opt) ) env_flags = env_flags | MDBX_NOMETASYNC;

	/* Duplicate keys, on mdbx_dbi_open, maybe set here? */
	/* MDBX_DUPSORT = UINT32_C(0x04), */


	/* Initialize the DB vals.
	 */
	UNWRAP_DB( self, db );
	db->env       = NULL;
	db->dbi       = 0;
	db->txn       = NULL;
	db->cursor    = NULL;
	db->env_flags = env_flags;
	db->open      = 0;

	/* Allocate an mdbx environment.
	 */
	rc = mdbx_env_create( &db->env );
	if ( rc != MDBX_SUCCESS )
		rb_raise( rmdbx_eDatabaseError, "mdbx_env_create: (%d) %s", rc, mdbx_strerror(rc) );

	/* Open the DB handle on disk.
	 */
	rc = mdbx_env_open( db->env, StringValueCStr(path), env_flags, mode );
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

	rb_define_protected_method( rmdbx_cDatabase, "initialize", rmdbx_database_initialize, -1 );
	rb_define_method( rmdbx_cDatabase, "close", rmdbx_close, 0 );
	rb_define_method( rmdbx_cDatabase, "closed?", rmdbx_closed_p, 0 );
	rb_define_method( rmdbx_cDatabase, "[]", rmdbx_get_val, 1 );
	rb_define_method( rmdbx_cDatabase, "[]=", rmdbx_put_val, 2 );

	rb_require( "mdbx/database" );
}

