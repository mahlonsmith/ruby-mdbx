# -*- ruby -*-
# vim: set noet sta sw=4 ts=4 :
# frozen_string_literal: true


if ENV[ 'COVERAGE' ]
	require 'simplecov'
	SimpleCov.start
end

require 'pathname'
require 'rspec'
require 'mdbx'


module MDBX::Testing
	TEST_DATABASE = Pathname( __FILE__ ).parent.parent.parent + 'tmp' + 'testdb'
end


RSpec.configure do |config|
	include MDBX::Testing

	config.expect_with :rspec do |expectations|
		expectations.include_chain_clauses_in_custom_matcher_descriptions = true
		expectations.syntax = :expect
	end

	config.mock_with( :rspec ) do |mock|
		mock.syntax = :expect
		mock.verify_partial_doubles = true
	end

	config.disable_monkey_patching!
	config.example_status_persistence_file_path = "spec/.status"
	config.filter_run :focus
	config.filter_run_when_matching :focus
	config.order = :random
	config.profile_examples = 5
	config.run_all_when_everything_filtered = true
	# config.warnings = true

	config.include( MDBX::Testing )
	# config.include( Loggability::SpecHelpers )
end


