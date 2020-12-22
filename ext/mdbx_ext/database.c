/* vim: set noet sta sw=4 ts=4 : */

#include "mdbx_ext.h"

/* VALUE str = rb_sprintf( "path: %+"PRIsVALUE", opts: %+"PRIsVALUE, path, opts ); */
/* printf( "%s\n", StringValueCStr(str) ); */

VALUE rmdbx_cDatabase;


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
	int mode;
	int open;
	int max_collections;
	char *path;
	char *subdb;
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
	rmdbx_db_t *new = RB_ALLOC( rmdbx_db_t );
	return TypedData_Make_Struct( klass, rmdbx_db_t, &rmdbx_db_data, new );
}


/*
 * Ensure all database file descriptors are collected and
 * removed.
 */
void
rmdbx_close_all( rmdbx_db_t* db )
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
		rmdbx_close_all( db );
		free( db );
	}
}


/*
 * Cleanly close an opened database from Ruby.
 */
VALUE
rmdbx_close( VALUE self )
{
	UNWRAP_DB( self, db );
	rmdbx_close_all( db );
	return Qtrue;
}


/*
 * Open the DB environment handle.
 */
VALUE
rmdbx_open_env( VALUE self )
{
	int rc;
	UNWRAP_DB( self, db );
	rmdbx_close_all( db );

	/* Allocate an mdbx environment.
	 */
	rc = mdbx_env_create( &db->env );
	if ( rc != MDBX_SUCCESS )
		rb_raise( rmdbx_eDatabaseError, "mdbx_env_create: (%d) %s", rc, mdbx_strerror(rc) );

	/* Set the maximum number of named databases for the environment. */
	// FIXME: potenially more env setups here?  maxreaders, pagesize?
	mdbx_env_set_maxdbs( db->env, db->max_collections );

	rc = mdbx_env_open( db->env, db->path, db->env_flags, db->mode );
	if ( rc != MDBX_SUCCESS ) {
		rmdbx_close( self );
		rb_raise( rmdbx_eDatabaseError, "mdbx_env_open: (%d) %s", rc, mdbx_strerror(rc) );
	}
	db->open = 1;

	return Qtrue;
}


/*
 * call-seq:
 *    db.closed? #=> false
 *
 * Predicate: return true if the database environment is closed.
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
		rc = mdbx_dbi_open( db->txn, db->subdb, MDBX_CREATE, &db->dbi );
		if ( rc != MDBX_SUCCESS ) {
			rmdbx_close( self );
			rb_raise( rmdbx_eDatabaseError, "mdbx_dbi_open: (%d) %s", rc, mdbx_strerror(rc) );
		}
	}

	return;
}


/*
 * call-seq:
 *    db.clear
 *
 * Empty the database (or collection) on disk.  Unrecoverable!
 */
VALUE
rmdbx_clear( VALUE self )
{
	UNWRAP_DB( self, db );

	rmdbx_open_txn( self, MDBX_TXN_READWRITE );
	int rc = mdbx_drop( db->txn, db->dbi, true );

	if ( rc != MDBX_SUCCESS )
		rb_raise( rmdbx_eDatabaseError, "mdbx_drop: (%d) %s", rc, mdbx_strerror(rc) );

	mdbx_txn_commit( db->txn );

	/* Refresh the environment handles. */
	rmdbx_open_env( self );

	return Qnil;
}


/*
 * Given a ruby +arg+, convert and return a structure
 * suitable for usage as a key for mdbx.  All keys are explicitly
 * converted to strings.
 */
MDBX_val
rmdbx_key_for( VALUE arg )
{
	MDBX_val rv;

	arg = rb_funcall( arg, rb_intern("to_s"), 0 );
	rv.iov_len  = RSTRING_LEN( arg );
	rv.iov_base = StringValuePtr( arg );

	return rv;
}


/*
 * Given a ruby +arg+, convert and return a structure
 * suitable for usage as a value for mdbx.
 */
MDBX_val
rmdbx_val_for( VALUE self, VALUE arg )
{
	MDBX_val rv;
	VALUE serialize_proc;

	serialize_proc = rb_iv_get( self, "@serializer" );
	if ( ! NIL_P( serialize_proc ) )
		arg = rb_funcall( serialize_proc, rb_intern("call"), 1, arg );

	rv.iov_len  = RSTRING_LEN( arg );
	rv.iov_base = StringValuePtr( arg );

	return rv;
}


/* call-seq:
 *    db.keys #=> [ 'key1', 'key2', ... ]
 *
 * Return an array of all keys in the current collection.
 */
VALUE
rmdbx_keys( VALUE self )
{
	UNWRAP_DB( self, db );
	VALUE rv = rb_ary_new();
	MDBX_val key, data;
	int rc;

	if ( ! db->open ) rb_raise( rmdbx_eDatabaseError, "Closed database." );

	rmdbx_open_txn( self, MDBX_TXN_RDONLY );
	rc = mdbx_cursor_open( db->txn, db->dbi, &db->cursor);

	if ( rc != MDBX_SUCCESS ) {
		rmdbx_close( self );
		rb_raise( rmdbx_eDatabaseError, "Unable to open cursor: (%d) %s", rc, mdbx_strerror(rc) );
	}

	rc = mdbx_cursor_get( db->cursor, &key, &data, MDBX_FIRST );
	if ( rc == MDBX_SUCCESS ) {
		rb_ary_push( rv, rb_str_new( key.iov_base, key.iov_len ) );
		while ( mdbx_cursor_get( db->cursor, &key, &data, MDBX_NEXT ) == 0 ) {
			rb_ary_push( rv, rb_str_new( key.iov_base, key.iov_len ) );
		}
	}

	mdbx_cursor_close( db->cursor );
	db->cursor = NULL;
	mdbx_txn_abort( db->txn );
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
	VALUE deserialize_proc;
	UNWRAP_DB( self, db );

	if ( ! db->open ) rb_raise( rmdbx_eDatabaseError, "Closed database." );

	rmdbx_open_txn( self, MDBX_TXN_RDONLY );

	MDBX_val ckey = rmdbx_key_for( key );
	MDBX_val data;
	rc = mdbx_get( db->txn, db->dbi, &ckey, &data );
	mdbx_txn_abort( db->txn );

	switch ( rc ) {
		case MDBX_SUCCESS:
			deserialize_proc = rb_iv_get( self, "@deserializer" );
			VALUE rv = rb_str_new( data.iov_base, data.iov_len );
			if ( ! NIL_P( deserialize_proc ) )
				return rb_funcall( deserialize_proc, rb_intern("call"), 1, rv );
			return rv;

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

	if ( ! db->open ) rb_raise( rmdbx_eDatabaseError, "Closed database." );

	rmdbx_open_txn( self, MDBX_TXN_READWRITE );

	MDBX_val ckey = rmdbx_key_for( key );

	// FIXME: DUPSORT is enabled -- different api?
	// See:  MDBX_NODUPDATA / MDBX_NOOVERWRITE
	if ( NIL_P(val) ) { /* remove if set to nil */
		rc = mdbx_del( db->txn, db->dbi, &ckey, NULL );
	}
	else {
		MDBX_val old;
		MDBX_val data = rmdbx_val_for( self, val );
		rc = mdbx_replace( db->txn, db->dbi, &ckey, &data, &old, 0 );
	}

	mdbx_txn_commit( db->txn );

	switch ( rc ) {
		case MDBX_SUCCESS:
			return val;
		case MDBX_NOTFOUND:
			return Qnil;
		default:
			rb_raise( rmdbx_eDatabaseError, "Unable to store value: (%d) %s", rc, mdbx_strerror(rc) );
	}
}


/*
 * call-seq:
 *    db.collection( 'collection_name' ) # => db
 *    db.collection( nil ) # => db (main)
 *
 * Operate on a sub-database "collection".  Passing +nil+
 * sets the database to the main, top-level namespace.
 *
 */
VALUE
rmdbx_set_subdb( int argc, VALUE *argv, VALUE self )
{
	UNWRAP_DB( self, db );
	VALUE subdb;

	rb_scan_args( argc, argv, "01", &subdb );
	if ( argc == 0 ) {
		if ( db->subdb == NULL ) return Qnil;
		return rb_str_new_cstr( db->subdb );
	}

	rb_iv_set( self, "@collection", subdb );
	db->subdb = NIL_P( subdb ) ? NULL : StringValueCStr( subdb );

	/* Close any currently open dbi handle, to be re-opened with
	 * the new collection on next access.
	 *
	  FIXME:  Immediate transaction write to auto-create new env?
	  Fetching from here at the moment causes an error if you
	  haven't written anything yet.
	 */
	if ( db->dbi ) {
		mdbx_dbi_close( db->env, db->dbi );
		db->dbi = 0;
	}

	return self;
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
	int mode            = 0644;
	int max_collections = 0;
	int env_flags       = MDBX_ENV_DEFAULTS;
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
	opt = rb_hash_aref( opts, ID2SYM( rb_intern("max_collections") ) );
	if ( ! NIL_P(opt) ) max_collections = FIX2INT( opt );
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
	db->mode      = mode;
	db->max_collections = max_collections;
	db->path      = StringValueCStr( path );
	db->open      = 0;
	db->subdb     = NULL;

	/* Set instance variables.
	 */
	rb_iv_set( self, "@path", path );
	rb_iv_set( self, "@options", opts );

	rmdbx_open_env( self );
	return self;
}


/*
 * Initialization for the MDBX::Database class.
 */
void
rmdbx_init_database()
{
	rmdbx_cDatabase = rb_define_class_under( rmdbx_mMDBX, "Database", rb_cData );

#ifdef FOR_RDOC
	rmdbx_mMDBX = rb_define_module( "MDBX" );
#endif

	rb_define_alloc_func( rmdbx_cDatabase, rmdbx_alloc );

	rb_define_protected_method( rmdbx_cDatabase, "initialize", rmdbx_database_initialize, -1 );
	rb_define_method( rmdbx_cDatabase, "collection", rmdbx_set_subdb, -1 );
	rb_define_method( rmdbx_cDatabase, "close", rmdbx_close, 0 );
	rb_define_method( rmdbx_cDatabase, "reopen", rmdbx_open_env, 0 );
	rb_define_method( rmdbx_cDatabase, "closed?", rmdbx_closed_p, 0 );
	rb_define_method( rmdbx_cDatabase, "clear", rmdbx_clear, 0 );
	rb_define_method( rmdbx_cDatabase, "keys", rmdbx_keys, 0 );
	rb_define_method( rmdbx_cDatabase, "[]", rmdbx_get_val, 1 );
	rb_define_method( rmdbx_cDatabase, "[]=", rmdbx_put_val, 2 );

	rb_require( "mdbx/database" );
}

