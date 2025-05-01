/* vim: set noet sta sw=4 ts=4 fdm=marker: */
/*
 * Primary database handle functions.
 *
 */

#include "mdbx_ext.h"

VALUE rmdbx_cDatabase;


/*
 * Ruby data allocation wrapper.
 */
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
	rmdbx_db_t *new;
	return TypedData_Make_Struct( klass, rmdbx_db_t, &rmdbx_db_data, new );
}


/*
 * Cleanup a previously allocated DB environment.
 */
void
rmdbx_free( void *db )
{
	if ( db ) {
		rmdbx_close_all( db );
		xfree( db );
	}
}


/*
 * Ensure all database file descriptors are collected and
 * removed.
 */
void
rmdbx_close_all( rmdbx_db_t *db )
{
	if ( db->cursor ) mdbx_cursor_close( db->cursor );
	if ( db->txn )    mdbx_txn_abort( db->txn );
	if ( db->dbi )    mdbx_dbi_close( db->env, db->dbi );
	if ( db->env )    mdbx_env_close( db->env );
	db->state.open = 0;
}


/*
 * Close any open database handle.  Will be automatically
 * re-opened on next transaction.  This is primarily useful for
 * switching between subdatabases.
 */
void
rmdbx_close_dbi( rmdbx_db_t *db )
{
	if ( ! db->dbi ) return;
	mdbx_dbi_close( db->env, db->dbi );
	db->dbi = 0;
}


/*
 * call-seq:
 *    db.close => true
 *
 * Cleanly close an opened database.
 */
VALUE
rmdbx_close( VALUE self )
{
	UNWRAP_DB( self, db );
	rmdbx_close_all( db );
	return Qtrue;
}


/*
 * call-seq:
 *    db.closed? => false
 *
 * Predicate: return true if the database environment is closed.
 */
VALUE
rmdbx_closed_p( VALUE self )
{
	UNWRAP_DB( self, db );
	return db->state.open == 1 ? Qfalse : Qtrue;
}


/*
 * Given a ruby string +key+ and a pointer to an MDBX_val, prepare the
 * key for usage within mdbx.  All keys are explicitly converted to
 * strings.
 *
 */
void
rmdbx_key_for( VALUE key, MDBX_val *ckey )
{
	VALUE key_str  = rb_funcall( key, rb_intern("to_s"), 0 );
	ckey->iov_len  = RSTRING_LEN( key_str );
	ckey->iov_base = malloc( ckey->iov_len );
	memcpy( ckey->iov_base, StringValuePtr(key_str), ckey->iov_len );
}


/*
 * Given a ruby +value+ and a pointer to an MDBX_val, prepare
 * the value for usage within mdbx.  Values are potentially serialized.
 *
 */
void
rmdbx_val_for( VALUE self, VALUE val, MDBX_val *data )
{
	val = rb_funcall( self, rb_intern("serialize"), 1, val );
	Check_Type( val, T_STRING );

	data->iov_len  = RSTRING_LEN( val );
	data->iov_base = malloc( data->iov_len );
	memcpy( data->iov_base, StringValuePtr(val), data->iov_len );
}


/*
 * Open the DB environment handle.
 *
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
	mdbx_env_set_maxdbs( db->env, db->settings.max_collections );

	/* Customize the maximum number of simultaneous readers. */
	if ( db->settings.max_readers )
		mdbx_env_set_maxreaders( db->env, db->settings.max_readers );

	/* Set an upper boundary (in bytes) for the database map size. */
	if ( db->settings.max_size )
		mdbx_env_set_geometry( db->env, -1, -1, db->settings.max_size, -1, -1, -1 );

	rc = mdbx_env_open( db->env, db->path, db->settings.env_flags, db->settings.mode );
	if ( rc != MDBX_SUCCESS ) {
		rmdbx_close_all( db );
		rb_raise( rmdbx_eDatabaseError, "mdbx_env_open: (%d) %s", rc, mdbx_strerror(rc) );
	}

	/* Force populate the db->dbi handle.  Under 0.12.x, getting a
	 * 'permission denied' doing this for the first access with a RDONLY
	 * for some reason. */
	rmdbx_open_txn( db, MDBX_TXN_READWRITE );
	rmdbx_close_txn( db, RMDBX_TXN_ROLLBACK );

	db->state.open = 1;
	return Qtrue;
}


/*
 * call-seq:
 *    db.clear
 *
 * Empty the current collection on disk.  If collections are not enabled
 * or the database handle is set to the top-level (main) db - this
 * deletes *all records* from the database.
 */
VALUE
rmdbx_clear( VALUE self )
{
	UNWRAP_DB( self, db );

	rmdbx_open_txn( db, MDBX_TXN_READWRITE );
	int rc = mdbx_drop( db->txn, db->dbi, false );

	if ( rc != MDBX_SUCCESS ) {
		rmdbx_close_txn( db, RMDBX_TXN_ROLLBACK );
		rb_raise( rmdbx_eDatabaseError, "mdbx_drop: (%d) %s", rc, mdbx_strerror(rc) );
	}

	rmdbx_close_txn( db, RMDBX_TXN_COMMIT );

	return Qnil;
}


/*
 * call-seq:
 *    db.drop( collection ) -> db
 *
 * Destroy a collection.  You must be in the top level database to call
 * this method.
 */
VALUE
rmdbx_drop( VALUE self, VALUE name )
{
	UNWRAP_DB( self, db );

	/* Provide a friendlier error message if max_collections is 0. */
	if ( db->settings.max_collections == 0 )
		rb_raise( rmdbx_eDatabaseError, "Unable to drop collection: collections are not enabled." );

	/* All transactions must be closed when dropping a database. */
	if ( db->txn )
		rb_raise( rmdbx_eDatabaseError, "Unable to drop collection: transaction open" );

	/* A drop can only be performed from the top-level database. */
	if ( db->subdb != NULL )
		rb_raise( rmdbx_eDatabaseError, "Unable to drop collection: switch to top-level db first" );

	name = rb_funcall( name, rb_intern("to_s"), 0 );
	db->subdb = StringValueCStr( name );

	rmdbx_close_dbi( db ); /* ensure we're reopening within the new subdb */
	rmdbx_open_txn( db, MDBX_TXN_READWRITE );
	int rc = mdbx_drop( db->txn, db->dbi, true );

	if ( rc != MDBX_SUCCESS ) {
		rmdbx_close_txn( db, RMDBX_TXN_ROLLBACK );
		rb_raise( rmdbx_eDatabaseError, "mdbx_drop: (%d) %s", rc, mdbx_strerror(rc) );
	}

	rmdbx_close_txn( db, RMDBX_TXN_COMMIT );

	/* Reset the current collection to the top level. */
	db->subdb = NULL;
	rmdbx_close_dbi( db ); /* ensure next access is not in the defunct subdb */

	/* Force populate the new db->dbi handle.  Under 0.12.x, getting a
	 * 'permission denied' doing this for the first access with a RDONLY
	 * for some reason. */
	rmdbx_open_txn( db, MDBX_TXN_READWRITE );
	rmdbx_close_txn( db, RMDBX_TXN_ROLLBACK );

	return self;
}


/* call-seq:
 *    db.length -> Integer
 *
 * Returns the count of keys in the currently selected collection.
 */
VALUE
rmdbx_length( VALUE self )
{
	UNWRAP_DB( self, db );
	MDBX_stat mstat;

	CHECK_HANDLE();
	rmdbx_open_txn( db, MDBX_TXN_RDONLY );

	int rc = mdbx_dbi_stat( db->txn, db->dbi, &mstat, sizeof(mstat) );
	if ( rc != MDBX_SUCCESS )
		rb_raise( rmdbx_eDatabaseError, "mdbx_dbi_stat: (%d) %s", rc, mdbx_strerror(rc) );

	VALUE rv = LONG2FIX( mstat.ms_entries );
	rmdbx_close_txn( db, RMDBX_TXN_ROLLBACK );

	return rv;
}


/* call-seq:
 *    db.include?( 'key' ) => bool
 *
 * Returns true if the current collection contains +key+.
 */
VALUE
rmdbx_include( VALUE self, VALUE key )
{
	UNWRAP_DB( self, db );

	CHECK_HANDLE();
	rmdbx_open_txn( db, MDBX_TXN_RDONLY );

	MDBX_val ckey;
	MDBX_val data;
	rmdbx_key_for( key, &ckey );

	int rc = mdbx_get( db->txn, db->dbi, &ckey, &data );
	rmdbx_close_txn( db, RMDBX_TXN_ROLLBACK );
	xfree( ckey.iov_base );

	switch ( rc ) {
		case MDBX_SUCCESS:
			return Qtrue;

		case MDBX_NOTFOUND:
			return Qfalse;

		default:
			rmdbx_close( self );
			rb_raise( rmdbx_eDatabaseError, "Unable to fetch key: (%d) %s", rc, mdbx_strerror(rc) );
	}
}


/* call-seq:
 *    db[ 'key' ] => value
 *
 * Return a single value for +key+ immediately.
 */
VALUE
rmdbx_get_val( VALUE self, VALUE key )
{
	UNWRAP_DB( self, db );

	CHECK_HANDLE();
	rmdbx_open_txn( db, MDBX_TXN_RDONLY );

	MDBX_val ckey;
	MDBX_val data;

	rmdbx_key_for( key, &ckey );
	int rc = mdbx_get( db->txn, db->dbi, &ckey, &data );
	rmdbx_close_txn( db, RMDBX_TXN_ROLLBACK );
	xfree( ckey.iov_base );

	VALUE rv;
	switch ( rc ) {
		case MDBX_SUCCESS:
			rv = rb_str_new( data.iov_base, data.iov_len );
			return rb_funcall( self, rb_intern("deserialize"), 1, rv );

		case MDBX_NOTFOUND:
			return Qnil;

		default:
			rmdbx_close( self );
			rb_raise( rmdbx_eDatabaseError, "Unable to fetch value: (%d) %s", rc, mdbx_strerror(rc) );
	}
}


/* call-seq:
 *    db[ 'key' ] = value
 *
 * Set a single value for +key+.  If the value is +nil+, the
 * key is removed.
 */
VALUE
rmdbx_put_val( VALUE self, VALUE key, VALUE val )
{
	int rc;
	UNWRAP_DB( self, db );

	CHECK_HANDLE();
	rmdbx_open_txn( db, MDBX_TXN_READWRITE );

	MDBX_val ckey;
	rmdbx_key_for( key, &ckey );

	if ( NIL_P(val) ) { /* remove if set to nil */
		rc = mdbx_del( db->txn, db->dbi, &ckey, NULL );
	}
	else {
		MDBX_val data;
		rmdbx_val_for( self, val, &data );
		rc = mdbx_put( db->txn, db->dbi, &ckey, &data, 0 );
		xfree( data.iov_base );
	}

	rmdbx_close_txn( db, RMDBX_TXN_COMMIT );
	xfree( ckey.iov_base );

	switch ( rc ) {
		case MDBX_SUCCESS:
			return val;
		case MDBX_NOTFOUND:
			return Qnil;
		default:
			rb_raise( rmdbx_eDatabaseError, "Unable to update value: (%d) %s", rc, mdbx_strerror(rc) );
	}
}


/*
 * Return the currently selected collection, or +nil+ if at the
 * top-level.
 */
VALUE
rmdbx_get_subdb( VALUE self )
{
	UNWRAP_DB( self, db );
	return ( db->subdb == NULL ) ? Qnil : rb_str_new_cstr( db->subdb );
}


/*
 * Sets the current collection name for read/write operations.
 *
 */
VALUE
rmdbx_set_subdb( VALUE self, VALUE name )
{
	UNWRAP_DB( self, db );

	/* Provide a friendlier error message if max_collections is 0. */
	if ( db->settings.max_collections == 0 )
		rb_raise( rmdbx_eDatabaseError, "Unable to change collection: collections are not enabled." );

	/* All transactions must be closed when switching database handles. */
	if ( db->txn )
		rb_raise( rmdbx_eDatabaseError, "Unable to change collection: transaction open" );

	xfree( db->subdb );
	db->subdb = NULL;

	if ( ! NIL_P(name) ) {
		size_t len = RSTRING_LEN( name ) + 1;
		db->subdb = malloc( len );
		strlcpy( db->subdb, StringValuePtr(name), len );
		rmdbx_log_obj( self, "debug", "setting subdb: %s", RSTRING_PTR(name) );
	}
	else {
		rmdbx_log_obj( self, "debug", "clearing subdb" );
	}

	/* Reset the db handle and issue a single transaction to reify
	   the collection.
	*/
	rmdbx_close_dbi( db );
	rmdbx_open_txn( db, MDBX_TXN_READWRITE );
	rmdbx_close_txn( db, RMDBX_TXN_COMMIT );

	return self;
}


/*
 * call-seq:
 *    db.in_transaction? => false
 *
 * Predicate: return true if a transaction (or snapshot)
 * is currently open.
 */
VALUE
rmdbx_in_transaction_p( VALUE self )
{
	UNWRAP_DB( self, db );
	return db->txn ? Qtrue : Qfalse;
}


/* Inline struct for transaction arguments, passed as a void pointer. */
struct txn_open_args_s {
	rmdbx_db_t *db;
	int rwflag;
};


/* Opens a transaction outside of the GVL. */
void *
rmdbx_open_txn_without_gvl( void *ptr )
{
	struct txn_open_args_s *txn_open_args = (struct txn_open_args_s *)ptr;

	rmdbx_db_t *db = txn_open_args->db;
	int rc = mdbx_txn_begin( db->env, NULL, txn_open_args->rwflag, &db->txn );

	return (void *)rc;
}


/*
 * Open a new database transaction.  If a transaction is already
 * open, this is a no-op.
 *
 * +rwflag+ must be either MDBX_TXN_RDONLY or MDBX_TXN_READWRITE.
 */
void
rmdbx_open_txn( rmdbx_db_t *db, int rwflag )
{
	if ( db->txn ) return;

	struct txn_open_args_s txn_open_args;
	txn_open_args.db = db;
	txn_open_args.rwflag = rwflag;

	void *result_ptr = rb_thread_call_without_gvl(
		rmdbx_open_txn_without_gvl, (void *)&txn_open_args,
		RUBY_UBF_IO, NULL
	);

	int rc = (int)result_ptr;

	if ( rc != MDBX_SUCCESS ) {
		rmdbx_close_all( db );
		rb_raise( rmdbx_eDatabaseError, "mdbx_txn_begin: (%d) %s", rc, mdbx_strerror(rc) );
	}

	if ( db->dbi == 0 ) {
		rc = mdbx_dbi_open( db->txn, db->subdb, db->settings.db_flags, &db->dbi );
		if ( rc != MDBX_SUCCESS ) {
			rmdbx_close_all( db );
			rb_raise( rmdbx_eDatabaseError, "mdbx_dbi_open: (%d) %s", rc, mdbx_strerror(rc) );
		}
	}

	return;
}


/*
 * Close any existing database transaction. If there is no
 * active transaction, this is a no-op.  If there is a long
 * running transaction open, this is a no-op.
 *
 * +txnflag must either be RMDBX_TXN_ROLLBACK or RMDBX_TXN_COMMIT.
 */
void
rmdbx_close_txn( rmdbx_db_t *db, int txnflag )
{
	if ( ! db->txn || db->state.retain_txn > -1 ) return;

	if ( txnflag == RMDBX_TXN_COMMIT ) {
		mdbx_txn_commit( db->txn );
	}
	else {
		mdbx_txn_abort( db->txn );
	}

	db->txn = 0;
	return;
}


/*
 * call-seq:
 *    db.open_transaction( mode )
 *
 * Open a new long-running transaction.  If +mode+ is true,
 * it is opened read/write.
 *
 */
VALUE
rmdbx_rb_opentxn( VALUE self, VALUE mode )
{
	UNWRAP_DB( self, db );
	CHECK_HANDLE();

	rmdbx_open_txn( db, RTEST(mode) ? MDBX_TXN_READWRITE : MDBX_TXN_RDONLY );
	db->state.retain_txn = RTEST(mode) ? 1 : 0;

	return Qtrue;
}


/*
 * call-seq:
 *    db.close_transaction( mode )
 *
 * Close a long-running transaction.  If +write+ is true,
 * the transaction is committed.  Otherwise, rolled back.
 *
 */
VALUE
rmdbx_rb_closetxn( VALUE self, VALUE write )
{
	UNWRAP_DB( self, db );

	db->state.retain_txn = -1;
	rmdbx_close_txn( db, RTEST(write) ? RMDBX_TXN_COMMIT : RMDBX_TXN_ROLLBACK );

	return Qtrue;
}


/*
 * Open a cursor for iteration.
 */
void
rmdbx_open_cursor( rmdbx_db_t *db )
{
	CHECK_HANDLE();
	if ( ! db->txn ) rb_raise( rmdbx_eDatabaseError, "No snapshot or transaction currently open." );

	int rc = mdbx_cursor_open( db->txn, db->dbi, &db->cursor );
	if ( rc != MDBX_SUCCESS ) {
		rmdbx_close_all( db );
		rb_raise( rmdbx_eDatabaseError, "Unable to open cursor: (%d) %s", rc, mdbx_strerror(rc) );
	}

	return;
}


/*
 * Enumerate over keys for the current collection.
 */
VALUE
rmdbx_each_key_i( VALUE self )
{
	UNWRAP_DB( self, db );
	MDBX_val key, data;

	if ( mdbx_cursor_get( db->cursor, &key, &data, MDBX_FIRST ) == MDBX_SUCCESS ) {
		rb_yield( rb_str_new( key.iov_base, key.iov_len ) );
		while ( mdbx_cursor_get( db->cursor, &key, &data, MDBX_NEXT ) == MDBX_SUCCESS ) {
			rb_yield( rb_str_new( key.iov_base, key.iov_len ) );
		}
	}

	return self;
}


/* call-seq:
 *    db.each_key {|key| block } => self
 *
 * Calls the block once for each key, returning self.
 * A transaction must be opened prior to use.
 */
VALUE
rmdbx_each_key( VALUE self )
{
	UNWRAP_DB( self, db );
	int state;

	CHECK_HANDLE();
	rmdbx_open_cursor( db );
	RETURN_ENUMERATOR( self, 0, 0 );

	rb_protect( rmdbx_each_key_i, self, &state );

	mdbx_cursor_close( db->cursor );
	db->cursor = NULL;

	if ( state ) rb_jump_tag( state );

	return self;
}


/* Enumerate over values for the current collection.
 */
VALUE
rmdbx_each_value_i( VALUE self )
{
	UNWRAP_DB( self, db );
	MDBX_val key, data;

	if ( mdbx_cursor_get( db->cursor, &key, &data, MDBX_FIRST ) == MDBX_SUCCESS ) {
		VALUE rv = rb_str_new( data.iov_base, data.iov_len );
		rb_yield( rb_funcall( self, rb_intern("deserialize"), 1, rv ) );

		while ( mdbx_cursor_get( db->cursor, &key, &data, MDBX_NEXT ) == MDBX_SUCCESS ) {
			rv = rb_str_new( data.iov_base, data.iov_len );
			rb_yield( rb_funcall( self, rb_intern("deserialize"), 1, rv ) );
		}
	}

	return self;
}


/* call-seq:
 *    db.each_value {|value| block } => self
 *
 * Calls the block once for each value, returning self.
 * A transaction must be opened prior to use.
 */
VALUE
rmdbx_each_value( VALUE self )
{
	UNWRAP_DB( self, db );
	int state;

	CHECK_HANDLE();
	rmdbx_open_cursor( db );
	RETURN_ENUMERATOR( self, 0, 0 );

	rb_protect( rmdbx_each_value_i, self, &state );

	mdbx_cursor_close( db->cursor );
	db->cursor = NULL;

	if ( state ) rb_jump_tag( state );

	return self;
}


/* Enumerate over key and value pairs for the current collection.
 */
VALUE
rmdbx_each_pair_i( VALUE self )
{
	UNWRAP_DB( self, db );
	MDBX_val key, data;

	if ( mdbx_cursor_get( db->cursor, &key, &data, MDBX_FIRST ) == MDBX_SUCCESS ) {
		VALUE rkey = rb_str_new( key.iov_base, key.iov_len );
		VALUE rval = rb_str_new( data.iov_base, data.iov_len );
		rval = rb_funcall( self, rb_intern("deserialize"), 1, rval );
		rb_yield( rb_assoc_new( rkey, rval  ) );

		while ( mdbx_cursor_get( db->cursor, &key, &data, MDBX_NEXT ) == MDBX_SUCCESS ) {
			rkey = rb_str_new( key.iov_base, key.iov_len );
			rval = rb_str_new( data.iov_base, data.iov_len );
			rval = rb_funcall( self, rb_intern("deserialize"), 1, rval );

			rb_yield( rb_assoc_new( rkey, rval ) );
		}
	}

	return self;
}


/* call-seq:
 *    db.each_pair {|key, value| block } => self
 *
 * Calls the block once for each key and value, returning self.
 * A transaction must be opened prior to use.
 */
VALUE
rmdbx_each_pair( VALUE self )
{
	UNWRAP_DB( self, db );
	int state;

	CHECK_HANDLE();
	rmdbx_open_cursor( db );
	RETURN_ENUMERATOR( self, 0, 0 );

	rb_protect( rmdbx_each_pair_i, self, &state );

	mdbx_cursor_close( db->cursor );
	db->cursor = NULL;

	if ( state ) rb_jump_tag( state );

	return self;
}


/*
 * Open an existing (or create a new) mdbx database at filesystem
 * +path+.  In block form, the database is automatically closed.
 *
 *    MDBX::Database.open( path ) -> db
 *    MDBX::Database.open( path, options ) -> db
 *    MDBX::Database.open( path, options ) do |db|
 *        db...
 *    end
 *
 */
VALUE
rmdbx_database_initialize( int argc, VALUE *argv, VALUE self )
{
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

	/* Initialize the DB vals.
	 */
	UNWRAP_DB( self, db );
	db->env    = NULL;
	db->dbi    = 0;
	db->txn    = NULL;
	db->cursor = NULL;
	db->path   = StringValueCStr( path );
	db->subdb  = NULL;
	db->state.open       = 0;
	db->state.retain_txn = -1;
	db->settings.env_flags       = MDBX_ENV_DEFAULTS;
	db->settings.db_flags        = MDBX_DB_DEFAULTS | MDBX_CREATE;
	db->settings.mode            = 0644;
	db->settings.max_collections = 0;
	db->settings.max_readers     = 0;
	db->settings.max_size        = 0;

	/* Set instance variables.
	 */
	rb_iv_set( self, "@path", path );
	rb_iv_set( self, "@options", rb_hash_dup( opts ) );

	/* Environment and database options setup, overrides.
	 */
	opt = rb_hash_delete( opts, ID2SYM( rb_intern("compatible") ) );
	if ( RTEST(opt) ) {
		db->settings.db_flags  = db->settings.db_flags | MDBX_DB_ACCEDE;
		db->settings.env_flags = db->settings.env_flags | MDBX_ACCEDE;
	}
	opt = rb_hash_delete( opts, ID2SYM( rb_intern("exclusive") ) );
	if ( RTEST(opt) ) db->settings.env_flags = db->settings.env_flags | MDBX_EXCLUSIVE;
	opt = rb_hash_delete( opts, ID2SYM( rb_intern("lifo_reclaim") ) );
	if ( RTEST(opt) ) db->settings.env_flags = db->settings.env_flags | MDBX_LIFORECLAIM;
	opt = rb_hash_delete( opts, ID2SYM( rb_intern("max_collections") ) );
	if ( ! NIL_P(opt) ) db->settings.max_collections = FIX2INT( opt );
	opt = rb_hash_delete( opts, ID2SYM( rb_intern("max_readers") ) );
	if ( ! NIL_P(opt) ) db->settings.max_readers = FIX2INT( opt );
	opt = rb_hash_delete( opts, ID2SYM( rb_intern("max_size") ) );
	if ( ! NIL_P(opt) ) db->settings.max_size = NUM2LONG( opt );
	opt = rb_hash_delete( opts, ID2SYM( rb_intern("mode") ) );
	if ( ! NIL_P(opt) ) db->settings.mode = FIX2INT( opt );
	opt = rb_hash_delete( opts, ID2SYM( rb_intern("no_memory_init") ) );
	if ( RTEST(opt) ) db->settings.env_flags = db->settings.env_flags | MDBX_NOMEMINIT;
	opt = rb_hash_delete( opts, ID2SYM( rb_intern("no_metasync") ) );
	if ( RTEST(opt) ) db->settings.env_flags = db->settings.env_flags | MDBX_NOMETASYNC;
	opt = rb_hash_delete( opts, ID2SYM( rb_intern("no_subdir") ) );
	if ( RTEST(opt) ) db->settings.env_flags = db->settings.env_flags | MDBX_NOSUBDIR;
	opt = rb_hash_delete( opts, ID2SYM( rb_intern("no_readahead") ) );
	if ( RTEST(opt) ) db->settings.env_flags = db->settings.env_flags | MDBX_NORDAHEAD;
#if defined(HAVE_CONST_MDBX_NOSTICKYTHREADS)
	opt = rb_hash_delete( opts, ID2SYM( rb_intern("no_stickythreads") ) );
	if ( RTEST(opt) ) db->settings.env_flags = db->settings.env_flags | MDBX_NOSTICKYTHREADS;
#else
	opt = rb_hash_delete( opts, ID2SYM( rb_intern("no_threadlocal") ) );
	if ( RTEST(opt) ) db->settings.env_flags = db->settings.env_flags | MDBX_NOTLS;
#endif
	opt = rb_hash_delete( opts, ID2SYM( rb_intern("readonly") ) );
	if ( RTEST(opt) ) db->settings.env_flags = db->settings.env_flags | MDBX_RDONLY;
	opt = rb_hash_delete( opts, ID2SYM( rb_intern("writemap") ) );
	if ( RTEST(opt) ) db->settings.env_flags = db->settings.env_flags | MDBX_WRITEMAP;

	if ( rb_hash_size_num(opts) > 0 ) {
		rb_raise( rb_eArgError, "Unknown option(s): %"PRIsVALUE, opts );
	}

	rmdbx_open_env( self );
	return self;
}


/*
 * call-seq:
 *    db.statistics => (hash of stats)
 *
 * Returns a hash populated with various metadata for the opened
 * database.
 *
 */
VALUE
rmdbx_stats( VALUE self )
{
	UNWRAP_DB( self, db );
	CHECK_HANDLE();

	return rmdbx_gather_stats( db );
}


/*
 * call-seq:
 *    db.clone => [copy of db]
 *
 * Copy the object (clone/dup).  The returned copy is closed and needs
 * to be reopened before use.  This function likely has limited use,
 * considering you can't open two handles within the same process.
 */
static VALUE rmdbx_init_copy( VALUE copy, VALUE orig )
{
	rmdbx_db_t *orig_db;
	rmdbx_db_t *copy_db;

	if ( copy == orig ) return copy;

	TypedData_Get_Struct( orig, rmdbx_db_t, &rmdbx_db_data, orig_db );
	TypedData_Get_Struct( copy, rmdbx_db_t, &rmdbx_db_data, copy_db );

	/* Copy all fields from the original to the copy, and force-close
	   the copy.
	*/
	MEMCPY( copy_db, orig_db, rmdbx_db_t, 1 );
	rmdbx_close_all( copy_db );

	return copy;
}


/*
 * Initialization for the MDBX::Database class.
 */
void
rmdbx_init_database( void )
{
	rmdbx_cDatabase = rb_define_class_under( rmdbx_mMDBX, "Database", rb_cObject );

#ifdef FOR_RDOC
	rmdbx_mMDBX = rb_define_module( "MDBX" );
#endif

	rb_define_alloc_func( rmdbx_cDatabase, rmdbx_alloc );

	rb_define_protected_method( rmdbx_cDatabase, "initialize", rmdbx_database_initialize, -1 );
	rb_define_protected_method( rmdbx_cDatabase, "initialize_copy", rmdbx_init_copy, 1 );

	rb_define_method( rmdbx_cDatabase, "clear", rmdbx_clear, 0 );
	rb_define_method( rmdbx_cDatabase, "close", rmdbx_close, 0 );
	rb_define_method( rmdbx_cDatabase, "closed?", rmdbx_closed_p, 0 );
	rb_define_method( rmdbx_cDatabase, "drop", rmdbx_drop, 1 );
	rb_define_method( rmdbx_cDatabase, "include?", rmdbx_include, 1 );
	rb_define_method( rmdbx_cDatabase, "length", rmdbx_length, 0 );
	rb_define_method( rmdbx_cDatabase, "reopen", rmdbx_open_env, 0 );
	rb_define_method( rmdbx_cDatabase, "[]", rmdbx_get_val, 1 );
	rb_define_method( rmdbx_cDatabase, "[]=", rmdbx_put_val, 2 );

	/* Enumerables */
	rb_define_method( rmdbx_cDatabase, "each_key", rmdbx_each_key, 0 );
	rb_define_method( rmdbx_cDatabase, "each_pair", rmdbx_each_pair, 0 );
	rb_define_method( rmdbx_cDatabase, "each_value", rmdbx_each_value, 0 );

	/* Manually open/close transactions from ruby. */
	rb_define_method( rmdbx_cDatabase, "in_transaction?", rmdbx_in_transaction_p, 0 );
	rb_define_protected_method( rmdbx_cDatabase, "open_transaction",  rmdbx_rb_opentxn, 1 );
	rb_define_protected_method( rmdbx_cDatabase, "close_transaction", rmdbx_rb_closetxn, 1 );

	/* Collection functions */
	rb_define_protected_method( rmdbx_cDatabase, "get_subdb", rmdbx_get_subdb, 0 );
	rb_define_protected_method( rmdbx_cDatabase, "set_subdb", rmdbx_set_subdb, 1 );

	rb_define_protected_method( rmdbx_cDatabase, "raw_stats", rmdbx_stats, 0 );

	rb_require( "mdbx/database" );
}


