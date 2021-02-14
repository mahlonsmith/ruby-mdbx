#!/usr/bin/env rspec -cfd
# vim: set nosta noet ts=4 sw=4 ft=ruby:

require_relative '../lib/helper'


RSpec.describe( MDBX::Database ) do

	after( :all ) do
		db = described_class.open( TEST_DATABASE.to_s )
		db.clear
		db.close
	end

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
			expect{
				db.collection( 'bucket' )
			} .to raise_exception( /not enabled/ )
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

		it "revert back to the previous collection when used in a block" do
			expect( db.collection ).to be_nil
			db.collection( 'bucket' ) { 'no-op' }
			expect( db.collection ).to be_nil
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


	context 'transactions' do

		let!( :db ) { described_class.open( TEST_DATABASE.to_s, max_collections: 5 ) }

		after( :each ) do
			db.close
		end

		it "knows when a transaction is currently open" do
			expect( db.in_transaction? ).to be_falsey
			db.snapshot
			expect( db.in_transaction? ).to be_truthy
			db.abort
			expect( db.in_transaction? ).to be_falsey

			db.snapshot do
				expect( db.in_transaction? ).to be_truthy
			end
			expect( db.in_transaction? ).to be_falsey
		end

		it "throws an error if changing collection mid-transaction" do
			db.snapshot do
				expect{ db.collection('nope') }.
					to raise_exception( MDBX::DatabaseError, /transaction open/ )
			end
		end

		it "are re-entrant" do
			3.times { db.snapshot }
			expect( db.in_transaction? ).to be_truthy
		end

		it "refuse to allow writes for read-only snapshots" do
			db.snapshot
			expect{ db[1] = true }.
				to raise_exception( MDBX::DatabaseError, /permission denied/i )
		end

		it "revert changes via explicit rollbacks" do
			db[ 1 ] = true
			db.transaction
			db[ 1 ] = false
			expect( db[ 1 ] ).to be_falsey
			db.rollback
			expect( db[ 1 ] ).to be_truthy
		end

		it "revert changes via uncaught exceptions" do
			db[ 1 ] = true
			expect {
				db.transaction do
					db[ 1 ] = false
					raise "boom"
				end
			}.to raise_exception( RuntimeError, "boom" )
			expect( db[ 1 ] ).to be_truthy
		end

		it "revert changes via explicit exceptions" do
			db[ 1 ] = true
			expect {
				db.transaction do
					db[ 1 ] = false
					raise MDBX::Rollback, "boom!"
				end
			}.to_not raise_exception
			expect( db[ 1 ] ).to be_truthy
		end

		it "write changes after commit" do
			db[ 1 ] = true
			db.transaction
			db[ 1 ] = false
			expect( db[ 1 ] ).to be_falsey
			db.commit
			expect( db[ 1 ] ).to be_falsey
		end

		it "automatically write changes after block" do
			db[ 1 ] = true
			db.transaction do
				db[ 1 ] = false
			end
			expect( db[ 1 ] ).to be_falsey
		end
	end
end

