#!/usr/bin/env rspec -cfd
# vim: set nosta noet ts=4 sw=4 ft=ruby:

require_relative '../lib/helper'


RSpec.describe( MDBX::Database ) do

	let!( :db ) { described_class.open( TEST_DATABASE.to_s, max_readers: 500 ) }

	let( :stats ) { db.statistics }

	after( :each ) do
		db.close
	end

	it "returns the configured max_readers" do
		expect( stats.dig(:environment, :max_readers) ).to be >= 500
	end

	it "returns compile time flags and options" do
		build = stats[ :build ]
		expect( build.keys.size ).to be( 4 )
		expect( build.keys ).to include( :compiler, :flags, :options, :target )
		expect( build[:compiler] ).to be_a( String )
		expect( build[:flags] ).to be_a( String )
		expect( build[:target] ).to be_a( String )
		expect( build[:options] ).to be_a( Hash )
	end

	it "returns readers in use" do
		readers = stats[ :readers ]
		expect( stats.dig(:environment, :readers_in_use) ).to eq( readers.size )
		expect( readers.first[:pid] ).to eq( $$ )
	end

	it "returns datafile attributes" do
		expect( stats.dig(:environment, :datafile, :type) ).to eq( "dynamic" )
	end
end

