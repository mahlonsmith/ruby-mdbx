#!/usr/bin/env rspec -cfd

require_relative '../lib/helper'


RSpec.describe( MDBX::Database ) do

	it "disallows direct calls to #new" do
		expect{ described_class.new }.
			to raise_exception( NoMethodError, /private/ )
	end
end

