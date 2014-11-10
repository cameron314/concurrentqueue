#!/usr/bin/python

# A simple script that reads the last run from the benchmark log file,
# and creates two CSV files containing the data required to make pretty
# performance graphs for enqueuing and dequeueing.
# The x-axis of the graph is meant to be the number of threads (first column), with
# the y-axis representing thousands of operations/second/thread (one column per queue).

import sys
import re


def extract(bench, log, data, hasBulk = True):
	# data = { thread_count: [ locked, boost, tbb, moodycamel, moodycamel_tok, moodycamel_bulk ], ... }
	
	def do_extract(bench, queue_header):
		block = re.search(r'^' + bench + r':.*?' + queue_header + r'\s*(.*?)\s*^\s*Operations per second', log, re.S | re.M | re.I).group(1)
		for threads, opsst in re.findall(r'^\s*(\d+)\s+thread.*?([0-9\.]+[kMG]?\s*$)', block, re.M | re.I):
			threads = int(threads)
			multiplier = 1
			if opsst[-1] in 'kMG':
				multiplier = { 'k': 1000, 'M': 1000000, 'G': 1000000000 }[opsst[-1]]
				opsst = opsst[:-1]
			opsst = int(float(opsst) * multiplier)
			if threads not in data:
				data[threads] = []
			data[threads].append(opsst)
	
	do_extract(bench, 'LockBasedQueue')
	do_extract(bench, 'boost::lockfree::queue')
	do_extract(bench, 'tbb::concurrent_queue')
	do_extract(bench, 'Without tokens')
	do_extract(bench, 'With tokens')
	if hasBulk:
		do_extract(bench + ' bulk', 'With tokens')


def write_csv(data, path, hasBulk = True):
	with open(path, 'w') as f:
		f.write('threads,"std::queue + std::mutex","boost::lockfree::queue","tbb::concurrent_queue","moodycamel::ConcurrentQueue (no tokens)","moodycamel::ConcurrentQueue",' + ('"moodycamel::ConcurrentQueue (bulk)"' if hasBulk else '') + '\n')
		for threads in sorted(data.keys()):
			f.write(str(threads))
			for opsst in data[threads]:
				f.write(',' + str(opsst))
			f.write('\n')


try:
	filename = 'benchmarks.log' if len(sys.argv) < 2 else sys.argv[1]
	with open(filename, 'r') as f:
		pieces = f.read().split('--- New run')
		log = pieces[-1]
		
		enq_data = { }
		extract('only enqueue', log, enq_data)
		
		deq_data = { }
		extract('only dequeue', log, deq_data)
		
		heavy_data = { }
		extract('heavy concurrent', log, heavy_data, False)
		
		write_csv(enq_data, 'enqueue.csv')
		write_csv(deq_data, 'dequeue.csv')
		write_csv(heavy_data, 'heavy.csv', False)
except IOError:
	print 'Usage: ' + sys.argv[0] + ' path/to/benchmarks.log'
