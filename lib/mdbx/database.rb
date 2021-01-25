# -*- ruby -*-
# vim: set nosta noet ts=4 sw=4 ft=ruby:
# encoding: utf-8

require 'mdbx' unless defined?( MDBX )


# TODO: rdoc
#
class MDBX::Database

	### Open an existing (or create a new) mdbx database at filesystem
	### +path+.  In block form, the database is automatically closed.
	###
	###    MDBX::Database.open( path ) -> db
	###    MDBX::Database.open( path, options ) -> db
	###    MDBX::Database.open( path, options ) do |db|
	###        db[ 'key' ] #=> value
	###    end
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


	# The options used to instantiate this database.
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

