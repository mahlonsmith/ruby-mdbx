#!/usr/bin/env ruby
# vim: set noet sta sw=4 ts=4 :

require 'mkmf'

$CFLAGS << ' -Wno-suggest-attribute=format'

have_library( 'mdbx' )  or abort "No mdbx library!"
have_header( 'mdbx.h' ) or abort "No mdbx.h header!"

create_header()
create_makefile( 'mdbx_ext' )

