/* vim: set noet sta sw=4 ts=4 : */

#include "mdbx_ext.h"


/*
 * MDBX initialization
 */
void
Init_mdbx_ext()
{
	mdbx_mMDBX = rb_define_module( "MDBX" );
	rb_define_const( mdbx_mMDBX, "LIBRARY_VERSION", rb_str_new_cstr(mdbx_version.git.describe) );
}

