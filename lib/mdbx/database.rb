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
		return db unless block_given?

		begin
			yield db
		ensure
			db.close
		end
	end

	# Only instantiate Database objects via #open.
	private_class_method :new

	# The options used to instantiate this database.
	attr_reader :options

	# The path on disk of the database.
	attr_reader :path

end # class MDBX::Database

