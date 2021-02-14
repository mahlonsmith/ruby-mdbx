# -*- ruby -*-
# vim: set nosta noet ts=4 sw=4 ft=ruby:
# encoding: utf-8

require 'mdbx' unless defined?( MDBX )


# The primary class for interacting with an MDBX database.
#
class MDBX::Database

	### call-seq:
	###    MDBX::Database.open( path ) => db
	###    MDBX::Database.open( path, options ) => db
	###
	### Open an existing (or create a new) mdbx database at filesystem
	### +path+.  In block form, the database is automatically closed
	### when the block exits.
	###
	###    MDBX::Database.open( path, options ) do |db|
	###        db[ 'key' ] = value
	###    end
	###
	### Passing options modify various database behaviors.  See the libmdbx
	### documentation for detailed information.
	###
	### ==== Options
	###
	### [:mode]
	###   Whe creating a new database, set permissions to this 4 digit
	###   octal number.  Defaults to `0644`.  Set to `0` to never automatically
	###   create a new file, only opening existing databases.
	###
	### [:max_collections]
	###   Set the maximum number of "subdatabase" collections allowed. By
	###   default, collection support is disabled.
	###
	### [:max_readers]
	###   Set the maximum number of allocated simultaneous reader slots.
	###
	### [:max_size]
	###   Set an upper boundary (in bytes) for the database map size.
	###   The default is 10485760 bytes.
	###
	### [:nosubdir]
	###   When creating a new database, don't put the data and lock file
	###   under a dedicated subdirectory.
	###
	### [:readonly]
	###   Reject any write attempts while using this database handle.
	###
	### [:exclusive]
	###   Access is restricted to this process handle. Other attempts
	###   to use this database (even in readonly mode) are denied.
	###
	### [:compat]
	###   Avoid incompatibility errors when opening an in-use database with
	###   unknown or mismatched flag values.
	###
	### [:writemap]
	###   Trade safety for speed for databases that fit within available
	###   memory. (See MDBX documentation for details.)
	###
	### [:no_threadlocal]
	###   Parallelize read-only transactions across threads.  Writes are
	###   always thread local. (See MDBX documentatoin for details.)
	###
	### [:no_readahead]
	###   Disable all use of OS readahead.  Potentially useful for
	###   random reads wunder low memory conditions.  Default behavior
	###   is to dynamically choose when to use or omit readahead.
	###
	### [:no_memory_init]
	###   Skip initializing malloc'ed memory to zeroes before writing.
	###
	### [:coalesce]
	###   Attempt to coalesce items for the garbage collector,
	###   potentialy increasing the chance of unallocating storage
	###   earlier.
	###
	### [:lifo_reclaim]
	###   Recycle garbage collected items via LIFO, instead of FIFO.
	###   Depending on underlying hardware (disk write-back cache), this
	###   could increase write performance.
	###
	### [:no_metasync]
	###   A system crash may sacrifice the last commit for a potentially
	###   large write performance increase.  Database integrity is
	###   maintained.
	###
	def self::open( *args, &block )
		db = new( *args )

		db.serializer   = ->( v ) { Marshal.dump( v ) }
		db.deserializer = ->( v ) { Marshal.load( v ) }

		if block_given?
			begin
				yield db
			ensure
				db.close
			end
		end

		return db
	end


	# Only instantiate Database objects via #open.
	private_class_method :new


	# Options used when instantiating this database handle.
	attr_reader :options

	# The path on disk of the database.
	attr_reader :path

	# A Proc for automatically serializing values.
	# Defaults to +Marshal.dump+.
	attr_accessor :serializer

	# A Proc for automatically deserializing values.
	# Defaults to +Marshal.load+.
	attr_accessor :deserializer


	### Switch to the top-level collection.
	###
	def main
		return self.collection( nil )
	end

	# Allow for some common nomenclature.
	alias_method :namespace, :collection


	### Open a new mdbx read/write transaction.  In block form,
	### the transaction is automatically committed.
	###
	### Raising a MDBX::Rollback exception from within the block
	### automatically rolls the transaction back.
	###
	def transaction( commit: true, &block )
		self.open_transaction( commit )
		yield self if block_given?

		return self

	rescue MDBX::Rollback
		commit = false
		self.rollback
	rescue
		commit = false
		self.rollback
		raise
	ensure
		if block_given?
			commit ? self.commit : self.rollback
		end
	end


	### Open a new mdbx read only snapshot.  In block form,
	### the snapshot is automatically closed.
	###
	def snapshot( &block )
		self.transaction( commit: false, &block )
	end


	### Close any open transactions, abandoning all changes.
	###
	def rollback
		return self.close_transaction( false )
	end
	alias_method :abort, :rollback


	### Close any open transactions, writing all changes.
	###
	def commit
		return self.close_transaction( true )
	end
	alias_method :save, :commit


	### Return a hash of various metadata for the current database.
	###
	def statistics
		raw = self.raw_stats

		# Place build options in their own hash.
		#
		build_opts = raw.delete( :build_options ).split.each_with_object( {} ) do |opt, acc|
			key, val = opt.split( '=' )
			acc[ key.to_sym ] = Integer( val ) rescue val
		end

		stats = {
			build: {
				compiler: raw.delete( :build_compiler ),
				flags:    raw.delete( :build_flags ),
				options:  build_opts,
				target:   raw.delete( :build_target )
			}
		}
		stats.merge!( raw )

		return stats
	end

end # class MDBX::Database

