#!/usr/bin/env rspec -cfd
# vim: set nosta noet ts=4 sw=4 ft=ruby:

require_relative '../lib/helper'


RSpec.fdescribe( MDBX::Database ) do

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

end


__END__
{:environment=>
  {:pagesize=>4096,
     :last_txnid=>125,
     :last_reader_txnid=>125,
     :maximum_readers=>122,
     :readers_in_use=>1,
     :datafile=>
      {:size_current=>65536,
	       :pages=>16,
	       :type=>"dynamic",
	       :size_lower=>12288,
	       :size_upper=>1048576,
	       :growth_step=>65536,
	       :shrink_threshold=>131072}},
 :readers=>
  [{:slot=>0,
      :pid=>45436,
      :thread=>34374651904,
      :txnid=>0,
      :lag=>0,
      :bytes_used=>0,
      :bytes_retired=>0}]}
}
