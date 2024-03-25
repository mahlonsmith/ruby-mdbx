# -*- ruby -*-
# vim: set nosta noet ts=4 sw=4 ft=ruby:
# encoding: utf-8

require 'loggability'

require 'mdbx_ext'


# Top level namespace for MDBX.
#
module MDBX
	extend Loggability

	# The version of this gem.
	VERSION = '0.3.5'


	log_as :mdbx


end # module MDBX

