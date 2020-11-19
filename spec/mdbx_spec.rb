#!/usr/bin/env rspec -cfd

require_relative './lib/helper'


RSpec.describe( MDBX ) do

	it "can report the MDBX library version" do
		expect( described_class::LIBRARY_VERSION ).
			to match( /v\d+\.\d+\.\d+\-\d+\-\w+$/ )
	end
end

