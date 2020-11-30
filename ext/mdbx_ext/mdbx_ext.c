/* vim: set noet sta sw=4 ts=4 : */

#include "mdbx_ext.h"


/*
 * MDBX initialization
 */
void
Init_mdbx_ext()
{
	rmdbx_mMDBX = rb_define_module( "MDBX" );

	/* The backend library version. */
	VALUE version = rb_str_new_cstr( mdbx_version.git.describe );
	rb_define_const( rmdbx_mMDBX, "LIBRARY_VERSION", version );

	rmdbx_eDatabaseError = rb_define_class_under( rmdbx_mMDBX, "DatabaseError", rb_eRuntimeError );

	rmdbx_init_database();
}

