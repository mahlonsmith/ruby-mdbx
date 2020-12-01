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

end

