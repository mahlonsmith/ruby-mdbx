/* vim: set noet sta sw=4 ts=4 :
 *
 * Expose a bunch of mdbx internals to ruby.
 * This is all largely stolen from mdbx_stat.c.
 *
 */

#include "mdbx_ext.h"


/*
 * Metadata specific to the mdbx build.
 */
void
rmdbx_gather_build_stats( VALUE stat )
{
	rb_hash_aset( stat, ID2SYM(rb_intern("build_compiler")),
			rb_str_new_cstr(mdbx_build.compiler) );
	rb_hash_aset( stat, ID2SYM(rb_intern("build_flags")),
			rb_str_new_cstr(mdbx_build.flags) );
	rb_hash_aset( stat, ID2SYM(rb_intern("build_options")),
			rb_str_new_cstr(mdbx_build.options) );
	rb_hash_aset( stat, ID2SYM(rb_intern("build_target")),
			rb_str_new_cstr(mdbx_build.target) );
	return;
}


/*
 * Grab current memory usage.  (Available since MDBX 0.10.0).
 */
void
rmdbx_gather_memory_stats( VALUE stat )
{
	if (! ( MDBX_VERSION_MAJOR >= 0 && MDBX_VERSION_MINOR >= 10 ) )
		return;

	VALUE mem = rb_hash_new();
	rb_hash_aset( stat, ID2SYM(rb_intern("system_memory")), mem );

	intptr_t page_size;
	intptr_t total_pages;
	intptr_t avail_pages;

	mdbx_get_sysraminfo( &page_size, &total_pages, &avail_pages );

	rb_hash_aset( mem, ID2SYM(rb_intern("pagesize")),    LONG2FIX( page_size ) );
	rb_hash_aset( mem, ID2SYM(rb_intern("total_pages")), LONG2FIX( total_pages ) );
	rb_hash_aset( mem, ID2SYM(rb_intern("avail_pages")), LONG2FIX( avail_pages ) );

	return;
}


/*
 * Metadata for the database file.
 */
void
rmdbx_gather_datafile_stats(
	VALUE environ,
	MDBX_stat mstat,
	MDBX_envinfo menvinfo )
{
	VALUE datafile = rb_hash_new();
	rb_hash_aset( environ, ID2SYM(rb_intern("datafile")), datafile );

	rb_hash_aset( datafile, ID2SYM(rb_intern("size_current")),
			INT2NUM(menvinfo.mi_geo.current) );
	rb_hash_aset( datafile, ID2SYM(rb_intern("pages")),
			INT2NUM(menvinfo.mi_geo.current / mstat.ms_psize) );

	if ( menvinfo.mi_geo.lower != menvinfo.mi_geo.upper ) {
		rb_hash_aset( datafile, ID2SYM(rb_intern("type")),
			rb_str_new_cstr("dynamic") );
		rb_hash_aset( datafile, ID2SYM(rb_intern("size_lower")),
			INT2NUM( menvinfo.mi_geo.lower ) );
		rb_hash_aset( datafile, ID2SYM(rb_intern("size_upper")),
			LONG2FIX( menvinfo.mi_geo.upper ) );
		rb_hash_aset( datafile, ID2SYM(rb_intern("growth_step")),
			INT2NUM( menvinfo.mi_geo.grow ) );
		rb_hash_aset( datafile, ID2SYM(rb_intern("shrink_threshold")),
			INT2NUM( menvinfo.mi_geo.shrink ) );
	}
	else {
		rb_hash_aset( datafile, ID2SYM(rb_intern("type")),
			rb_str_new_cstr("fixed") );
	}

	return;
}


/*
 * Metadata for the database environment.
 */
void
rmdbx_gather_environment_stats(
	VALUE stat,
	MDBX_stat mstat,
	MDBX_envinfo menvinfo )
{
	VALUE environ  = rb_hash_new();
	rb_hash_aset( stat, ID2SYM(rb_intern("environment")), environ );

	rb_hash_aset( environ, ID2SYM(rb_intern("pagesize")),
			INT2NUM(mstat.ms_psize) );
	rb_hash_aset( environ, ID2SYM(rb_intern("branch_pages")),
			LONG2NUM(mstat.ms_branch_pages) );
	rb_hash_aset( environ, ID2SYM(rb_intern("leaf_pages")),
			LONG2NUM(mstat.ms_leaf_pages) );
	rb_hash_aset( environ, ID2SYM(rb_intern("overflow_pages")),
			LONG2NUM(mstat.ms_overflow_pages) );
	rb_hash_aset( environ, ID2SYM(rb_intern("btree_depth")),
			INT2NUM(mstat.ms_depth) );
	rb_hash_aset( environ, ID2SYM(rb_intern("entries")),
			LONG2NUM(mstat.ms_entries) );
	rb_hash_aset( environ, ID2SYM(rb_intern("last_txnid")),
			INT2NUM(menvinfo.mi_recent_txnid) );
	rb_hash_aset( environ, ID2SYM(rb_intern("last_reader_txnid")),
			INT2NUM(menvinfo.mi_latter_reader_txnid) );
	rb_hash_aset( environ, ID2SYM(rb_intern("max_readers")),
			INT2NUM(menvinfo.mi_maxreaders) );
	rb_hash_aset( environ, ID2SYM(rb_intern("readers_in_use")),
			INT2NUM(menvinfo.mi_numreaders) );

	rmdbx_gather_datafile_stats( environ, mstat, menvinfo );

	return;
}


/*
 * Callback iterator for pulling each reader's current state.
 * See: https://erthink.github.io/libmdbx/group__c__statinfo.html#gad1ab5cf54d4a9f7d4c2999078920e8b0
 *
 */
int
rmdbx_reader_list_cb(
	void *ctx,
	int num,
	int slot,
	mdbx_pid_t pid,
	mdbx_tid_t thread,
	uint64_t txnid,
	uint64_t lag,
	size_t bytes_used,
	size_t bytes_retired )
{
	VALUE reader = rb_hash_new();

	rb_hash_aset( reader, ID2SYM(rb_intern("slot")),
			INT2NUM( slot ) );
	rb_hash_aset( reader, ID2SYM(rb_intern("pid")),
			LONG2FIX( pid ) );
	rb_hash_aset( reader, ID2SYM(rb_intern("thread")),
			LONG2FIX( thread ) );
	rb_hash_aset( reader, ID2SYM(rb_intern("txnid")),
			LONG2FIX( txnid ) );
	rb_hash_aset( reader, ID2SYM(rb_intern("lag")),
			LONG2FIX( lag ) );
	rb_hash_aset( reader, ID2SYM(rb_intern("bytes_used")),
			LONG2FIX( bytes_used ) );
	rb_hash_aset( reader, ID2SYM(rb_intern("bytes_retired")),
			LONG2FIX( bytes_retired ) );

	rb_ary_push( (VALUE)ctx, reader );

	return 0;
}


/*
 * Metadata for current reader slots.
 * Initialize an array and populate it with each reader's statistics.
*/
void
rmdbx_gather_reader_stats(
	rmdbx_db_t *db,
	VALUE stat,
	MDBX_stat mstat,
	MDBX_envinfo menvinfo )
{
	VALUE readers = rb_ary_new();

	mdbx_reader_list( db->env, rmdbx_reader_list_cb, (void*)readers );
	rb_hash_aset( stat, ID2SYM(rb_intern("readers")), readers );

	return;
}


/*
 * Build and return a hash of various statistic/metadata
 * for the open +db+ handle.
 */
VALUE
rmdbx_gather_stats( rmdbx_db_t *db )
{
	VALUE stat = rb_hash_new();

	int rc;
	MDBX_stat mstat;
	MDBX_envinfo menvinfo;

	rmdbx_gather_memory_stats( stat );
	rmdbx_gather_build_stats( stat );

	rmdbx_open_txn( db, MDBX_TXN_RDONLY );
    rc = mdbx_env_info_ex( db->env, db->txn, &menvinfo, sizeof(menvinfo) );
	if ( rc != MDBX_SUCCESS )
		rb_raise( rmdbx_eDatabaseError, "mdbx_env_info_ex: (%d) %s", rc, mdbx_strerror(rc) );

    rc = mdbx_env_stat_ex( db->env, db->txn, &mstat, sizeof(mstat) );
	if ( rc != MDBX_SUCCESS )
		rb_raise( rmdbx_eDatabaseError, "mdbx_env_stat_ex: (%d) %s", rc, mdbx_strerror(rc) );
	rmdbx_close_txn( db, RMDBX_TXN_ROLLBACK );

	rmdbx_gather_environment_stats( stat, mstat, menvinfo );
	rmdbx_gather_reader_stats( db, stat, mstat, menvinfo );

	return stat;
}

