
#include <ruby.h>
#include "extconf.h"

#include "mdbx.h"

#ifndef MDBX_EXT_0_9_2
#define MDBX_EXT_0_9_2


/* ------------------------------------------------------------
 * Globals
 * ------------------------------------------------------------ */

extern VALUE rmdbx_mMDBX;
extern VALUE rmdbx_cDatabase;
extern VALUE rmdbx_eDatabaseError;
extern VALUE rmdbx_eRollback;


/* ------------------------------------------------------------
 * Functions
 * ------------------------------------------------------------ */
extern void Init_rmdbx ( void );
extern void rmdbx_init_database ( void );


#endif /* define MDBX_EXT_0_9_2 */

