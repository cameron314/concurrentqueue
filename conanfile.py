from conans import ConanFile
import os

class ConcurrentQueue(ConanFile):
    name = "concurrentqueue"
    url = 'https://github.com/Manu343726/concurrentqueue'
    description='A fast multi-producer, multi-consumer lock-free queue for C++11'
    license = "Simplified BSD/Boost Software License"
    version = "1.0.0"
    exports = "*.h"
    build_policy = "missing"
    generators = 'cmake'

    def package(self):
        include_dir  = os.path.join('include', 'concurrentqueue')
        include_dir2 = os.path.join('include', 'moodycamel')

        self.copy('concurrentqueue.h', dst=include_dir)
        self.copy('blockingconcurrentqueue.h', dst=include_dir)
        self.copy('concurrentqueue.h', dst=include_dir2)
        self.copy('blockingconcurrentqueue.h', dst=include_dir2)
