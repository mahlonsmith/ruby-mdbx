/* vim: set noet sta sw=4 ts=4 : */

#include "mdbx_ext.h"

VALUE rmdbx_mMDBX;
VALUE rmdbx_eDatabaseError;
VALUE rmdbx_eRollback;

/*
 * MDBX initialization
 */
void
Init_mdbx_ext()
{
	rmdbx_mMDBX = rb_define_module( "MDBX" );

	VALUE version = rb_str_new_cstr( mdbx_version.git.describe );
	/* The backend MDBX library version. */
	rb_define_const( rmdbx_mMDBX, "LIBRARY_VERSION", version );

	/* A generic exception class for internal Database errors. */
	rmdbx_eDatabaseError = rb_define_class_under( rmdbx_mMDBX, "DatabaseError", rb_eRuntimeError );

	/*
	 * Raising an MDBX::Rollback exception from within a transaction
	 * discards all changes and closes the transaction.
	 */
	rmdbx_eRollback = rb_define_class_under( rmdbx_mMDBX, "Rollback", rb_eRuntimeError );

	rmdbx_init_database();
}

