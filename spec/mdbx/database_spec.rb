#!/usr/bin/env rspec -cfd
# vim: set nosta noet ts=4 sw=4 ft=ruby:

require_relative '../lib/helper'


RSpec.describe( MDBX::Database ) do

	before( :all ) do
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

	it "can create a closed clone" do
		db = described_class.open( TEST_DATABASE.to_s )
		db[1] = "doopydoo"

		clone = db.clone

		expect( db.closed? ).to be_falsey
		expect( clone.closed? ).to be_truthy
		expect( db.path ).to eq( clone.path )
		db.close

		clone.reopen
		expect( clone[1] ).to eq( "doopydoo" )
		clone.close
	end


	context 'an opened database' do

		let!( :db ) { described_class.open( TEST_DATABASE.to_s ) }

		before( :each ) do
			db.clear
		end

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
	end


	context 'hash-alike methods' do

		let!( :db ) { described_class.open( TEST_DATABASE.to_s ) }

		before( :each ) do
			db.clear
		end

		after( :each ) do
			db.close
		end


		it "can check for the presence of a key" do
			expect( db.has_key?( 'key1' ) ).to be_falsey
			db[ 'key1' ] = 1
			expect( db.has_key?( 'key1' ) ).to be_truthy
		end

		it "can remove an entry by setting a key's value to nil" do
			db[ 'test' ] = "hi"
			expect( db['test'] ).to eq( 'hi' )

			db[ 'test' ] = nil
			expect( db['test'] ).to be_nil
		end

		it 'can remove an entry via delete()' do
			val = 'hi'
			db[ 'test' ] = val
			expect( db['test'] ).to eq( val )

			oldval = db.delete( 'test' )
			expect( oldval ).to eq( val )
			expect( db['test'] ).to be_nil
		end

		it 'returns a the delete() block if a key is not found' do
			db.clear
			expect( db.delete( 'test' ) ).to be_nil
			rv = db.delete( 'test' ) {|key| "Couldn't find %p key!" % [ key ] }
			expect( rv ).to eq( "Couldn't find \"test\" key!" )
		end

		it "can return an array of its keys" do
			db[ 'key1' ] = true
			db[ 'key2' ] = true
			db[ 'key3' ] = true
			expect( db.keys ).to include( 'key1', 'key2', 'key3' )
		end

		it 'knows when there is data present' do
			expect( db.empty? ).to be_truthy
			db[ 'bloop' ] = 1
			expect( db.empty? ).to be_falsey
		end

		it "can convert to an array" do
			3.times{|i| db[i] = i }
			expect( db.to_a ).to eq([ ["0",0], ["1",1], ["2",2] ])
		end

		it "can convert to a hash" do
			3.times{|i| db[i] = i }
			expect( db.to_h ).to eq({ "0"=>0, "1"=>1, "2"=>2 })
		end

		it "retrieves a value via fetch()" do
			db[ 'test' ] = true
			expect( db.fetch('test') ).to be_truthy
		end

		it "executes a fetch() block if the key was not found" do
			rv = false
			db.fetch( 'nopenopenope' ) { rv = true }
			expect( rv ).to be_truthy
		end

		it "raises KeyError if fetch()ing without a block to a nonexistent key" do
			expect{ db.fetch(:nopenopenope) }.to raise_exception( KeyError, /key not found/ )
		end

		it "can return a sliced hash" do
			( 'a'..'z' ).each{|c| db[c] = c }
			expect( db.slice( 'a', 'f' ) ).to eq( 'a' => 'a', 'f' => 'f' )
		end

		it "can return an array of specific values" do
			( 'a'..'z' ).each{|c| db[c] = c * 3 }
			expect( db.values_at('e', 'nopenopenope', 'g') ).to eq( ['eee', nil, 'ggg'] )
		end

		it "can return an array of all values" do
			( 'a'..'z' ).each{|c| db[c] = c * 2  }
			expect( db.values ).to include( 'aa', 'hh', 'tt' )
		end
	end


	context 'collections' do

		let!( :db ) { described_class.open( TEST_DATABASE.to_s, max_collections: 5 ) }

		before( :each ) do
			db.clear
		end

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

		it "can be used immediately when switched to" do
			db.collection( :bucket )
			expect{ db.length }.to_not raise_exception
		end

		it "knows it's length" do
			db.collection( 'size1' )
			10.times {|i| db[i] = true }
			db.collection( 'size2' )
			25.times {|i| db[i] = true }

			db.collection( 'size1' )
			expect( db.length ).to be( 10 )
			db.collection( 'size2' )
			expect( db.length ).to be( 25 )
		end

		it "disallows regular key/val storage for namespace keys" do
			db.collection( 'bucket' )
			db[ 'okay' ] = 1
			db.main

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

		it "automatically stringifies the collection argument" do
			db.collection( :bucket )
			expect( db.collection ).to eq( 'bucket' )
		end

		it "revert back to the previous collection when used in a block" do
			expect( db.collection ).to be_nil
			db.collection( 'bucket' ) { 'no-op' }
			expect( db.collection ).to be_nil

			db.collection( 'another' )
			db.collection( 'bucket' ) { 'no-op' }
			expect( db.collection ).to eq( 'another' )
		end

		it "reverts back to previous collections within multiple blocks" do
			expect( db.collection ).to be_nil
			db.collection( 'bucket1' ) do
				expect( db.collection ).to eq( 'bucket1' )
				db.collection( 'bucket2' ) do
					expect( db.collection ).to eq( 'bucket2' )
					db.collection( 'bucket3' ) do
						expect( db.collection ).to eq( 'bucket3' )
					end
					expect( db.collection ).to eq( 'bucket2' )
				end
				expect( db.collection ).to eq( 'bucket1' )
			end
			expect( db.collection ).to be_nil
		end

		it "reverts back to previous collection if the block raises an exception" do
			expect( db.collection ).to be_nil
			begin
				db.collection( 'bucket1' ) do
					db.collection( 'bucket2' ) { raise "ka-bloooey!" }
				end
			rescue
			end
			expect( db.collection ).to be_nil
		end

		it "can be cleared of contents" do
			db.collection( 'bucket' )
			10.times {|i| db[i] = true }
			expect( db[2] ).to be_truthy

			db.clear
			db.main
			expect( db ).to include( 'bucket' )
		end

		it "fail if the max_collections option is not enabled when dropping" do
			db.close
			db = described_class.open( TEST_DATABASE.to_s )
			expect{
				db.drop( 'bucket' )
			} .to raise_exception( /not enabled/ )
		end

		it "disallows dropping a collection mid transaction" do
			expect {
				db.transaction { db.drop(:bucket) }
			}.to raise_exception( MDBX::DatabaseError, /transaction open/ )
		end

		it "disallows dropping a collection within a collection" do
			expect {
				db.collection(:bucket) { db.drop(:bucket) }
			}.to raise_exception( MDBX::DatabaseError, /switch to top-level/ )
		end

		it "sets the current collection to 'main' after dropping a collection" do
			db.collection( 'doom' )
			db.main
			db.drop( 'doom' )

			expect( db.collection ).to be_nil
			expect( db['doom'] ).to be_nil
		end

		it "retains the collection environment when clearing data" do
			db.collection( 'doom' )
			db[ 'key' ] = 1
			db.clear

			expect( db.collection ).to eq( 'doom' )
			expect( db ).to_not have_key( 'key' )

			db.main
			expect( db ).to include( 'doom' )
		end
	end


	context 'transactions' do

		let!( :db ) { described_class.open( TEST_DATABASE.to_s, max_collections: 5 ) }

		before( :each ) do
			db.clear
		end

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

		it "doesn't inadvertantly close transactions when using hash-alike methods" do
			expect( db.in_transaction? ).to be_falsey
			db.transaction
			expect( db.in_transaction? ).to be_truthy
			db.to_a
			db.to_h
			db.keys
			db.slice( :woop )
			db.values
			db.values_at( :woop )
			expect( db.in_transaction? ).to be_truthy
			db.abort
		end
	end


	context "iterators" do

		let( :db ) {
			described_class.open( TEST_DATABASE.to_s, max_collections: 5 ).collection( 'iter' )
		}

		before( :each ) do
			3.times {|i| db[i] = "#{i}-val" }
		end

		after( :each ) do
			db.close
		end

		it "raises an exception if the caller didn't open a transaction first" do
			expect{ db.each_key }.to raise_exception( MDBX::DatabaseError, /no .*currently open/i )
			expect{ db.each_value }.to raise_exception( MDBX::DatabaseError, /no .*currently open/i )
			expect{ db.each_pair }.to raise_exception( MDBX::DatabaseError, /no .*currently open/i )
		end

		context "(with a transaction)" do

			before( :each ) { db.snapshot }
			after( :each )  { db.abort }

			it "returns an iterator without a block" do
				iter = db.each_key
				expect( iter ).to be_a( Enumerator )
				expect( iter.to_a.size ).to be( 3 )
			end

			it "can iterate through keys" do
				rv = db.each_key.with_object([]){|k, acc| acc << k }
				expect( db.each_key.to_a ).to eq( rv )
			end

			it "can iterate through values" do
				rv = db.each_value.with_object([]){|v, acc| acc << v }
				expect( rv ).to eq( %w[ 0-val 1-val 2-val ] )
			end

			it "can iterate through key/value pairs" do
				expect( db.each_pair.to_a.first ).to eq([ "0", "0-val" ])
				expect( db.each_pair.to_a.last ).to eq([ "2", "2-val" ])
			end
		end
	end


	context "serialization" do

		let( :db ) {
			described_class.open( TEST_DATABASE.to_s )
		}

		after( :each ) do
			db.close
		end

		it "uses Marshalling as default" do
			db.deserializer = nil
			hash = { a_hash: true }
			db[ 'test' ] = hash
			expect( db['test'] ).to eq( Marshal.dump( hash ) )
		end

		it "can be disabled completely" do
			db.serializer = nil
			db.deserializer = nil

			db[ 'test' ] = "doot"
			db[ 'test2' ] = [1,2,3].to_s
			expect( db['test'] ).to eq( "doot" )
			expect( db['test2'] ).to eq( "[1, 2, 3]" )
		end

		it "can be arbitrarily changed" do
			db.serializer = ->( v ) { JSON.generate(v) }
			db.deserializer = ->( v ) { JSON.parse(v) }

			hash = { "a_hash" => true }
			db[ 'test' ] = hash
			expect( db['test'] ).to eq( hash )
		end
	end
end

