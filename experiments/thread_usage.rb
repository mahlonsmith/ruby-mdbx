#!/usr/bin/env ruby
#

require 'mdbx'
require 'benchmark'
require 'fileutils'

include FileUtils


db = MDBX::Database.open( 'tmpdb' )
mtx = Mutex.new

at_exit do
	rm_r 'tmpdb'
end

THREAD_COUNT = 10
WRITES_PER   = 1000

puts "#{THREAD_COUNT} simultaneous threads, #{WRITES_PER} writes each:"

Benchmark.bm( 10 ) do |x|
	x.report( " txn per write:" ) do
		threads = []
		THREAD_COUNT.times do |i|
			threads << Thread.new do
				mtx.synchronize do
					WRITES_PER.times do
						key = "%02d-%d" % [ i, rand(1000) ]
						db[ key ] = rand(1000)
					end
				end
			end
		end
		threads.map( &:join )
	end


	# Long running transactions require a mutex across threads.
	#
	x.report( "txn per thread:" ) do
		threads = []
		THREAD_COUNT.times do |i|
			threads << Thread.new do
				mtx.synchronize do
					db.transaction do
						WRITES_PER.times do
							key = "%02d-%d" % [ i, rand(1000) ]
							db[ key ] = rand(1000)
						end
					end
				end
			end
		end
		threads.map( &:join )
	end

	x.report( "    unthreaded:" ) do
		db.transaction do
			( THREAD_COUNT * WRITES_PER ).times do
				key = "000-%d" % [ rand(1000) ]
				db[ key ] = rand(1000)
			end
		end
	end
end


