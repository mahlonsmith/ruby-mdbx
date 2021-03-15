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
	###    end # closed!
	###
	### Passing options modify various database behaviors.  See the libmdbx
	### documentation for detailed information.
	###
	### ==== Options
	###
	### Unless otherwise mentioned, option keys are symbols, and values
	### are boolean.
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
	###   Access is restricted to the first opening process. Other attempts
	###   to use this database (even in readonly mode) are denied.
	###
	### [:compat]
	###   Skip compatibility checks when opening an in-use database with
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

	alias_method :namespace, :collection
	alias_method :size, :length
	alias_method :each, :each_pair


	#
	# Transaction methods
	#

	### Open a new mdbx read/write transaction.  In block form,
	### the transaction is automatically committed when the block ends.
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
	### the snapshot is automatically closed when the block ends.
	###
	def snapshot( &block )
		self.transaction( commit: false, &block )
	end


	### Close any open transaction, abandoning all changes.
	###
	def rollback
		return self.close_transaction( false )
	end
	alias_method :abort, :rollback


	### Close any open transaction, writing all changes.
	###
	def commit
		return self.close_transaction( true )
	end
	alias_method :save, :commit


	#
	# Hash-alike methods
	#

	### Return the entirety of database contents as an Array of array
	### pairs.
	###
	def to_a
		in_txn = self.in_transaction?
		self.snapshot unless in_txn

		return self.each_pair.to_a
	ensure
		self.abort unless in_txn
	end


	### Return the entirety of database contents as a Hash.
	###
	def to_h
		in_txn = self.in_transaction?
		self.snapshot unless in_txn

		return self.each_pair.to_h
	ensure
		self.abort unless in_txn
	end


	### Returns +true+ if the current collection has no data.
	###
	def empty?
		return self.size.zero?
	end


	### Returns the value for the given key, if found.
	### If key is not found and no block was given, returns nil.
	### If key is not found and a block was given, yields key to the
	### block and returns the block's return value.
	###
	def fetch( key, &block )
		val = self[ key ]
		if block_given?
			return block.call( key ) if val.nil?
		else
			return val if val
			raise KeyError, "key not found: %p" % [ key ]
		end
	end


	### Deletes the entry for the given key and returns its associated
	### value.  If no block is given and key is found, deletes the entry
	### and returns the associated value.  If no block given and key is
	### not found, returns nil.
	###
	### If a block is given and key is found, ignores the block, deletes
	### the entry, and returns the associated value.  If a block is given
	### and key is not found, calls the block and returns the block's
	### return value.
	###
	def delete( key, &block )
		val = self[ key ]
		return block.call( key ) if block_given? && val.nil?

		self[ key ] = nil
		return val
	end


	### Returns a new Array containing all keys in the collection.
	###
	def keys
		in_txn = self.in_transaction?
		self.snapshot unless in_txn

		return self.each_key.to_a
	ensure
		self.abort unless in_txn
	end


	### Returns a new Hash object containing the entries for the given
	### keys.  Any given keys that are not found are ignored.
	###
	def slice( *keys )
		in_txn = self.in_transaction?
		self.snapshot unless in_txn

		return keys.each_with_object( {} ) do |key, acc|
			val = self[ key ]
			acc[ key ] = val if val
		end
	ensure
		self.abort unless in_txn
	end


	### Returns a new Array containing all values in the collection.
	###
	def values
		in_txn = self.in_transaction?
		self.snapshot unless in_txn

		return self.each_value.to_a
	ensure
		self.abort unless in_txn
	end


	### Returns a new Array containing values for the given +keys+.
	###
	def values_at( *keys )
		in_txn = self.in_transaction?
		self.snapshot unless in_txn

		return keys.each_with_object( [] ) do |key, acc|
			acc << self[ key ]
		end
	ensure
		self.abort unless in_txn
	end


	#
	# Utility methods
	#

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

