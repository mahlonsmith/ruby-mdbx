
#include <ruby.h>
#include "extconf.h"

#include "mdbx.h"

#ifndef MDBX_EXT_0_9_2
#define MDBX_EXT_0_9_2


/* ------------------------------------------------------------
 * Globals
 * ------------------------------------------------------------ */

VALUE rmdbx_mMDBX;
VALUE rmdbx_cDatabase;
VALUE rmdbx_eDatabaseError;


/* ------------------------------------------------------------
 * Functions
 * ------------------------------------------------------------ */
extern void Init_rmdbx ( void );
extern void rmdbx_init_database ( void );


#endif /* define MDBX_EXT_0_9_2 */

