#!/usr/bin/env rspec -cfd

require_relative '../lib/helper'


RSpec.describe( MDBX::Database ) do

	it "disallows direct calls to #new" do
		expect{ described_class.new }.
			to raise_exception( NoMethodError, /private/ )
	end

	it "knows the env handle open/close state" do
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

		it "can be reopened" do
			db.close
			expect( db ).to be_closed
			db.reopen
			expect( db ).to_not be_closed
		end

		it "knows its own path" do
			expect( db.path ).to match( %r|tmp/testdb$| )
		end

		it "fails if opened again within the same process" do
			# This is a function of libmdbx internals, just testing
			# here for behavior.
			expect {
				described_class.open( TEST_DATABASE.to_s )
			}.
			to raise_exception( MDBX::DatabaseError, /environment is already used/ )
		end

		it "can remove a key by setting its value to nil" do
			db[ 'test' ] = "hi"
			expect( db['test'] ).to eq( 'hi' )

			db[ 'test' ] = nil
			expect( db['test'] ).to be_nil

			expect { db[ 'test' ] = nil }.to_not raise_exception
		end

		it "can return an array of its keys" do
			db[ 'key1' ] = true
			db[ 'key2' ] = true
			db[ 'key3' ] = true
			expect( db.keys ).to include( 'key1', 'key2', 'key3' )
		end
	end


	context 'collections' do

		let!( :db ) { described_class.open( TEST_DATABASE.to_s, max_collections: 5 ) }

		after( :each ) do
			db.close
		end

		it "fail if the max_collections option is not specified when opening" do
			db.close
			db = described_class.open( TEST_DATABASE.to_s )
			db.collection( 'bucket' )

			expect{ db['key'] = true }.to raise_exception( /MDBX_DBS_FULL/ )
		end

		it "disallows regular key/val storage for namespace keys" do
			db.collection( 'bucket' )
			db[ 'okay' ] = 1
			db.collection( nil )

			expect{ db['bucket'] = 1  }.to raise_exception( /MDBX_INCOMPATIBLE/ )
		end

		it "defaults to the top-level namespace" do
			expect( db.collection ).to be_nil
		end

		it "can set a collection namespace" do
			expect( db.collection ).to be_nil
			db[ 'key' ] = true

			db.collection( 'bucket' )
			expect( db.collection ).to eq( 'bucket' )
			db[ 'key' ] = false

			db.main
			expect( db.collection ).to be_nil
			expect( db['key'] ).to be_truthy
		end

		it "can be cleared of contents" do
			db.collection( 'bucket' )
			10.times {|i| db[i] = true }
			expect( db[2] ).to be_truthy

			db.clear
			db.main
			expect( db['bucket'] ).to be_nil
		end
	end
end

