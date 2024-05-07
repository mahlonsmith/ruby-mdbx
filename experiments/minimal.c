
/*
* clang -L/usr/local/lib -I/usr/local/include -lmdbx minimal.c -o minimal-tset
*/

#include "mdbx.h"
#include "assert.h"


int
main()
{
	MDBX_env *env;
	MDBX_txn *txn;
	MDBX_dbi dbi;
	int rc;

	rc = mdbx_env_create( &env );
	assert( rc == MDBX_SUCCESS );

	rc = mdbx_env_open( env, "./testdb", MDBX_ENV_DEFAULTS, 0644 );
	assert( rc == MDBX_SUCCESS );

	/*
	 * Using a read/write transaction handle for mdbx_dbi_open()
	 * works in all cases.
	 *
	 */
	// rc = mdbx_txn_begin( env, NULL, MDBX_TXN_READWRITE, &txn );
	
	/*
	 * Using a readonly transaction handle seemingly fails for
	 * use with mdbx_dbi_open().
	 *
	 */
	rc = mdbx_txn_begin( env, NULL, MDBX_TXN_RDONLY, &txn );
	assert( rc == MDBX_SUCCESS );

	rc = mdbx_dbi_open( txn, NULL, MDBX_DB_DEFAULTS|MDBX_CREATE, &dbi );
	assert( rc == MDBX_SUCCESS );

	mdbx_txn_abort( txn );



	mdbx_dbi_close( env, dbi );
	mdbx_env_close( env );
	mdbx_txn_abort( txn );

	return( 0 );
}
