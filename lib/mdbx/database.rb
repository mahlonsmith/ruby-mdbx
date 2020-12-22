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
	### FIXME: options!
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
	attr_accessor :serializer

	# A Proc for automatically deserializing values.
	attr_accessor :deserializer


	### Switch to the top-level collection.
	###
	def main
		return self.collection( nil )
	end

	# Allow for some common nomenclature.
	alias_method :namespace, :collection

end # class MDBX::Database

