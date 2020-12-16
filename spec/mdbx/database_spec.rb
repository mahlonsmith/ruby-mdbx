#!/usr/bin/env rspec -cfd

require_relative '../lib/helper'


RSpec.describe( MDBX::Database ) do

	it "disallows direct calls to #new" do
		expect{ described_class.new }.
			to raise_exception( NoMethodError, /private/ )
	end

	it "knows the db handle open/close state" do
		db = described_class.open( TEST_DATABASE.to_s )
		expect( db.closed? ).to be_falsey
		db.close
		expect( db.closed? ).to be_truthy
	end

	it "closes itself automatically when used in block form" do
		db = described_class.open( TEST_DATABASE.to_s ) do |db|
			expect( db.closed? ).to be_falsey
		end
		expect( db.closed? ).to be_truthy
	end


	context 'an opened database' do

		let!( :db ) { described_class.open( TEST_DATABASE.to_s ) }

		after( :each ) do
			db.close
		end

		it "knows its own path" do
			expect( db.path ).to match( %r|data/testdb$| )
		end

		it "fails if opened again within the same process" do
			# This is a function of libmdbx internals, just testing
			# here for behaviorals.
			expect {
				described_class.open( TEST_DATABASE.to_s )
			}.
			to raise_exception( MDBX::DatabaseError, /environment is already used/ )
		end

		it "defaults to the top-level namespace" do
			expect( db.collection ).to be_nil
		end

		it "can set a collection namespace" do
			db.collection( 'bucket' )
			expect( db.collection ).to eq( 'bucket' )
			# TODO: set/retrieve data
		end

	end
end

