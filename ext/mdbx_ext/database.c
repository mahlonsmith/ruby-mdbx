/* vim: set noet sta sw=4 ts=4 : */

#include "mdbx_ext.h"

/* Shortcut for fetching current DB variables.
 */
#define UNWRAP_DB( val, db ) \
	rmdbx_db_t *db; \
	TypedData_Get_Struct( val, rmdbx_db_t, &rmdbx_db_data, db );


VALUE rmdbx_cDatabase;

/*
 * Ruby allocation hook.
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
	rmdbx_db_t *new = RB_ALLOC( rmdbx_db_t );
	return TypedData_Make_Struct( klass, rmdbx_db_t, &rmdbx_db_data, new );
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
	db->state.open = 1;

	return Qtrue;
}


/*
 * Open a cursor for iteration.
 */
void
rmdbx_open_cursor( rmdbx_db_t *db )
{
	if ( ! db->state.open ) rb_raise( rmdbx_eDatabaseError, "Closed database." );
	if ( ! db->txn ) rb_raise( rmdbx_eDatabaseError, "No snapshot or transaction currently open." );

	int rc = mdbx_cursor_open( db->txn, db->dbi, &db->cursor );
	if ( rc != MDBX_SUCCESS ) {
		rmdbx_close_all( db );
		rb_raise( rmdbx_eDatabaseError, "Unable to open cursor: (%d) %s", rc, mdbx_strerror(rc) );
	}

	return;
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

	int rc = mdbx_txn_begin( db->env, NULL, rwflag, &db->txn );
	if ( rc != MDBX_SUCCESS ) {
		rmdbx_close_all( db );
		rb_raise( rmdbx_eDatabaseError, "mdbx_txn_begin: (%d) %s", rc, mdbx_strerror(rc) );
	}

	if ( db->dbi == 0 ) {
		// FIXME: dbi_flags
		rc = mdbx_dbi_open( db->txn, db->subdb, MDBX_CREATE, &db->dbi );
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

	switch ( txnflag ) {
		case RMDBX_TXN_COMMIT:
			mdbx_txn_commit( db->txn );
		default:
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
 * call-seq:
 *    db.clear
 *
 * Empty the current collection on disk.  If collections are not enabled
 * or the database handle is set to the top-level (main) db - this
 * deletes *all records* from the database.  This is not recoverable!
 */
VALUE
rmdbx_clear( VALUE self )
{
	UNWRAP_DB( self, db );

	rmdbx_open_txn( db, MDBX_TXN_READWRITE );
	int rc = mdbx_drop( db->txn, db->dbi, true );

	if ( rc != MDBX_SUCCESS )
		rb_raise( rmdbx_eDatabaseError, "mdbx_drop: (%d) %s", rc, mdbx_strerror(rc) );

	rmdbx_close_txn( db, RMDBX_TXN_COMMIT );

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


/*
 * Deserialize and return a value.
 */
VALUE
rmdbx_deserialize( VALUE self, VALUE val )
{
	VALUE deserialize_proc = rb_iv_get( self, "@deserializer" );
	if ( ! NIL_P( deserialize_proc ) )
		val = rb_funcall( deserialize_proc, rb_intern("call"), 1, val );

	return val;
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
	MDBX_val key, data;

	rmdbx_open_cursor( db );
	RETURN_ENUMERATOR( self, 0, 0 );

	if ( mdbx_cursor_get( db->cursor, &key, &data, MDBX_FIRST ) == MDBX_SUCCESS ) {
		rb_yield( rb_str_new( key.iov_base, key.iov_len ) );
		while ( mdbx_cursor_get( db->cursor, &key, &data, MDBX_NEXT ) == MDBX_SUCCESS ) {
			rb_yield( rb_str_new( key.iov_base, key.iov_len ) );
		}
	}

	mdbx_cursor_close( db->cursor );
	db->cursor = NULL;
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
	MDBX_val key, data;

	rmdbx_open_cursor( db );
	RETURN_ENUMERATOR( self, 0, 0 );

	if ( mdbx_cursor_get( db->cursor, &key, &data, MDBX_FIRST ) == MDBX_SUCCESS ) {
		VALUE rv = rb_str_new( data.iov_base, data.iov_len );
		rb_yield( rmdbx_deserialize( self, rv ) );

		while ( mdbx_cursor_get( db->cursor, &key, &data, MDBX_NEXT ) == MDBX_SUCCESS ) {
			rv = rb_str_new( data.iov_base, data.iov_len );
			rb_yield( rmdbx_deserialize( self, rv ) );
		}
	}

	mdbx_cursor_close( db->cursor );
	db->cursor = NULL;
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
	MDBX_val key, data;

	rmdbx_open_cursor( db );
	RETURN_ENUMERATOR( self, 0, 0 );

	if ( mdbx_cursor_get( db->cursor, &key, &data, MDBX_FIRST ) == MDBX_SUCCESS ) {
		VALUE rkey = rb_str_new( key.iov_base, key.iov_len );
		VALUE rval = rb_str_new( data.iov_base, data.iov_len );
		rb_yield( rb_assoc_new( rkey, rmdbx_deserialize( self, rval ) ) );

		while ( mdbx_cursor_get( db->cursor, &key, &data, MDBX_NEXT ) == MDBX_SUCCESS ) {
			rkey = rb_str_new( key.iov_base, key.iov_len );
			rval = rb_str_new( data.iov_base, data.iov_len );
			rb_yield( rb_assoc_new( rkey, rmdbx_deserialize( self, rval ) ) );
		}
	}

	mdbx_cursor_close( db->cursor );
	db->cursor = NULL;
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

	if ( ! db->state.open ) rb_raise( rmdbx_eDatabaseError, "Closed database." );
	rmdbx_open_txn( db, MDBX_TXN_RDONLY );

	int rc = mdbx_dbi_stat( db->txn, db->dbi, &mstat, sizeof(mstat) );
	if ( rc != MDBX_SUCCESS )
		rb_raise( rmdbx_eDatabaseError, "mdbx_dbi_stat: (%d) %s", rc, mdbx_strerror(rc) );

	VALUE rv = LONG2FIX( mstat.ms_entries );
	rmdbx_close_txn( db, RMDBX_TXN_ROLLBACK );

	return rv;
}


/* call-seq:
 *    db[ 'key' ] => value
 *
 * Return a single value for +key+ immediately.
 */
VALUE
rmdbx_get_val( VALUE self, VALUE key )
{
	int rc;
	UNWRAP_DB( self, db );

	if ( ! db->state.open ) rb_raise( rmdbx_eDatabaseError, "Closed database." );
	rmdbx_open_txn( db, MDBX_TXN_RDONLY );

	MDBX_val ckey = rmdbx_key_for( key );
	MDBX_val data;
	VALUE rv;
	rc = mdbx_get( db->txn, db->dbi, &ckey, &data );
	rmdbx_close_txn( db, RMDBX_TXN_ROLLBACK );

	switch ( rc ) {
		case MDBX_SUCCESS:
			rv = rb_str_new( data.iov_base, data.iov_len );
			return rmdbx_deserialize( self, rv );

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
 * Set a single value for +key+.
 */
VALUE
rmdbx_put_val( VALUE self, VALUE key, VALUE val )
{
	int rc;
	UNWRAP_DB( self, db );

	if ( ! db->state.open ) rb_raise( rmdbx_eDatabaseError, "Closed database." );
	rmdbx_open_txn( db, MDBX_TXN_READWRITE );

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

	rmdbx_close_txn( db, RMDBX_TXN_COMMIT );

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
	if ( ! db->state.open ) rb_raise( rmdbx_eDatabaseError, "Closed database." );

	return rmdbx_gather_stats( db );
}


/*
 * call-seq:
 *    db.collection -> (collection name, or nil if in main)
 *    db.collection( 'collection_name' ) -> db
 *    db.collection( nil ) -> db (main)
 *
 * Gets or sets the sub-database "collection" that read/write
 * operations apply to.
 * Passing +nil+ sets the database to the main, top-level namespace.
 * If a block is passed, the collection automatically reverts to the
 * prior collection when it exits.
 *
 *    db.collection( 'collection_name' ) do
 *        [ ... ]
 *    end # reverts to the previous collection name
 *
 */
VALUE
rmdbx_set_subdb( int argc, VALUE *argv, VALUE self )
{
	UNWRAP_DB( self, db );
	VALUE subdb, block;
	char *prev_db = NULL;

	rb_scan_args( argc, argv, "01&", &subdb, &block );
	if ( argc == 0 ) {
		if ( db->subdb == NULL ) return Qnil;
		return rb_str_new_cstr( db->subdb );
	}

	/* Provide a friendlier error message if max_collections is 0. */
	if ( db->settings.max_collections == 0 )
		rb_raise( rmdbx_eDatabaseError, "Unable to change collection: collections are not enabled." );

	/* All transactions must be closed when switching database handles. */
	if ( db->txn )
		rb_raise( rmdbx_eDatabaseError, "Unable to change collection: transaction open" );

	/* Retain the prior database collection if a block was passed.
	 */
	if ( rb_block_given_p() && db->subdb != NULL ) {
		prev_db = (char *) malloc( strlen(db->subdb) + 1 );
		strcpy( prev_db, db->subdb );
	}

	db->subdb = NIL_P( subdb ) ? NULL : StringValueCStr( subdb );
	rmdbx_close_dbi( db );

	/*
	  FIXME:  Immediate transaction write to auto-create new env?
	  Fetching from here at the moment causes an error if you
	  haven't written anything to the new collection yet.
	 */

	/* Revert to the previous collection after the block is done.
	 */
	if ( rb_block_given_p() ) {
		rb_yield( self );
		if ( db->subdb != prev_db ) {
			db->subdb = prev_db;
			rmdbx_close_dbi( db );
		}
		xfree( prev_db );
	}

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
	db->settings.mode            = 0644;
	db->settings.max_collections = 0;
	db->settings.max_readers     = 0;
	db->settings.max_size        = 0;

	/* Options setup, overrides.
	 */
	opt = rb_hash_aref( opts, ID2SYM( rb_intern("mode") ) );
	if ( ! NIL_P(opt) ) db->settings.mode = FIX2INT( opt );
	opt = rb_hash_aref( opts, ID2SYM( rb_intern("max_collections") ) );
	if ( ! NIL_P(opt) ) db->settings.max_collections = FIX2INT( opt );
	opt = rb_hash_aref( opts, ID2SYM( rb_intern("max_readers") ) );
	if ( ! NIL_P(opt) ) db->settings.max_readers = FIX2INT( opt );
	opt = rb_hash_aref( opts, ID2SYM( rb_intern("max_size") ) );
	if ( ! NIL_P(opt) ) db->settings.max_size = NUM2LONG( opt );
	opt = rb_hash_aref( opts, ID2SYM( rb_intern("nosubdir") ) );
	if ( RTEST(opt) ) db->settings.env_flags = db->settings.env_flags | MDBX_NOSUBDIR;
	opt = rb_hash_aref( opts, ID2SYM( rb_intern("readonly") ) );
	if ( RTEST(opt) ) db->settings.env_flags = db->settings.env_flags | MDBX_RDONLY;
	opt = rb_hash_aref( opts, ID2SYM( rb_intern("exclusive") ) );
	if ( RTEST(opt) ) db->settings.env_flags = db->settings.env_flags | MDBX_EXCLUSIVE;
	opt = rb_hash_aref( opts, ID2SYM( rb_intern("compat") ) );
	if ( RTEST(opt) ) db->settings.env_flags = db->settings.env_flags | MDBX_ACCEDE;
	opt = rb_hash_aref( opts, ID2SYM( rb_intern("writemap") ) );
	if ( RTEST(opt) ) db->settings.env_flags = db->settings.env_flags | MDBX_WRITEMAP;
	opt = rb_hash_aref( opts, ID2SYM( rb_intern("no_threadlocal") ) );
	if ( RTEST(opt) ) db->settings.env_flags = db->settings.env_flags | MDBX_NOTLS;
	opt = rb_hash_aref( opts, ID2SYM( rb_intern("no_readahead") ) );
	if ( RTEST(opt) ) db->settings.env_flags = db->settings.env_flags | MDBX_NORDAHEAD;
	opt = rb_hash_aref( opts, ID2SYM( rb_intern("no_memory_init") ) );
	if ( RTEST(opt) ) db->settings.env_flags = db->settings.env_flags | MDBX_NOMEMINIT;
	opt = rb_hash_aref( opts, ID2SYM( rb_intern("coalesce") ) );
	if ( RTEST(opt) ) db->settings.env_flags = db->settings.env_flags | MDBX_COALESCE;
	opt = rb_hash_aref( opts, ID2SYM( rb_intern("lifo_reclaim") ) );
	if ( RTEST(opt) ) db->settings.env_flags = db->settings.env_flags | MDBX_LIFORECLAIM;
	opt = rb_hash_aref( opts, ID2SYM( rb_intern("no_metasync") ) );
	if ( RTEST(opt) ) db->settings.env_flags = db->settings.env_flags | MDBX_NOMETASYNC;

	/* Duplicate keys, on mdbx_dbi_open, maybe set here? */
	/* MDBX_DUPSORT = UINT32_C(0x04), */

	/* Set instance variables.
	 */
	rb_iv_set( self, "@path", path );
	rb_iv_set( self, "@options", opts );

	rmdbx_open_env( self );
	return self;
}


/*
 * call-seq:
 *    db.clone => [copy of db]
 *
 * Copy the object (clone/dup).  The returned copy is closed and needs
 * to be reopened before use.  This function likely  has limited use,
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
rmdbx_init_database()
{
	rmdbx_cDatabase = rb_define_class_under( rmdbx_mMDBX, "Database", rb_cData );

#ifdef FOR_RDOC
	rmdbx_mMDBX = rb_define_module( "MDBX" );
#endif

	rb_define_alloc_func( rmdbx_cDatabase, rmdbx_alloc );

	rb_define_protected_method( rmdbx_cDatabase, "initialize", rmdbx_database_initialize, -1 );
	rb_define_protected_method( rmdbx_cDatabase, "initialize_copy", rmdbx_init_copy, 1 );
	rb_define_method( rmdbx_cDatabase, "collection", rmdbx_set_subdb, -1 );
	rb_define_method( rmdbx_cDatabase, "close", rmdbx_close, 0 );
	rb_define_method( rmdbx_cDatabase, "reopen", rmdbx_open_env, 0 );
	rb_define_method( rmdbx_cDatabase, "closed?", rmdbx_closed_p, 0 );
	rb_define_method( rmdbx_cDatabase, "in_transaction?", rmdbx_in_transaction_p, 0 );
	rb_define_method( rmdbx_cDatabase, "clear", rmdbx_clear, 0 );
	rb_define_method( rmdbx_cDatabase, "each_key", rmdbx_each_key, 0 );
	rb_define_method( rmdbx_cDatabase, "each_value", rmdbx_each_value, 0 );
	rb_define_method( rmdbx_cDatabase, "each_pair", rmdbx_each_pair, 0 );
	rb_define_method( rmdbx_cDatabase, "length", rmdbx_length, 0 );
	rb_define_method( rmdbx_cDatabase, "[]", rmdbx_get_val, 1 );
	rb_define_method( rmdbx_cDatabase, "[]=", rmdbx_put_val, 2 );

	/* Manually open/close transactions from ruby. */
	rb_define_protected_method( rmdbx_cDatabase, "open_transaction",  rmdbx_rb_opentxn, 1 );
	rb_define_protected_method( rmdbx_cDatabase, "close_transaction", rmdbx_rb_closetxn, 1 );

	rb_define_protected_method( rmdbx_cDatabase, "raw_stats", rmdbx_stats, 0 );

	rb_require( "mdbx/database" );
}

