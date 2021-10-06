/* vim: set noet sta sw=4 ts=4 : */

#include "mdbx_ext.h"

VALUE rmdbx_mMDBX;
VALUE rmdbx_eDatabaseError;
VALUE rmdbx_eRollback;

/*
 * Log a message to the given +context+ object's logger.
 */
void
#ifdef HAVE_STDARG_PROTOTYPES
rmdbx_log_obj( VALUE context, const char *level, const char *fmt, ... )
#else
rmdbx_log_obj( VALUE context, const char *level, const char *fmt, va_dcl )
#endif
{
	char buf[BUFSIZ];
	va_list	args;
	VALUE logger = Qnil;
	VALUE message = Qnil;

	va_init_list( args, fmt );
	vsnprintf( buf, BUFSIZ, fmt, args );
	message = rb_str_new2( buf );

	logger = rb_funcall( context, rb_intern("log"), 0 );
	rb_funcall( logger, rb_intern(level), 1, message );

	va_end( args );
}


/*
 * Log a message to the global logger.
 */
void
#ifdef HAVE_STDARG_PROTOTYPES
rmdbx_log( const char *level, const char *fmt, ... )
#else
rmdbx_log( const char *level, const char *fmt, va_dcl )
#endif
{
	char buf[BUFSIZ];
	va_list	args;
	VALUE logger = Qnil;
	VALUE message = Qnil;

	va_init_list( args, fmt );
	vsnprintf( buf, BUFSIZ, fmt, args );
	message = rb_str_new2( buf );

	logger = rb_funcall( rmdbx_mMDBX, rb_intern("logger"), 0 );
	rb_funcall( logger, rb_intern(level), 1, message );

	va_end( args );
}




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

