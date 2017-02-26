from conans import ConanFile, CMake
import os

version = os.getenv('CONAN_CONCURRENTQUEUE_VERSION', '1.0.0')
user    = os.getenv('CONAN_CONCURRENTQUEUE_USER', 'Manu343726')
channel = os.getenv('CONAN_CONCURRENTQUEUE_CHANNEL', 'testing')

class TestConcurrentQueue(ConanFile):
    settings = 'os', 'compiler', 'build_type', 'arch'
    requires = (
        'concurrentqueue/{}@{}/{}'.format(version, user, channel)
    )
    generators = 'cmake'

    def build(self):
        cmake = CMake(self.settings)
        self.run('cmake {} {}'.format(self.conanfile_directory, cmake.command_line))
        self.run('cmake --build . {}'.format(cmake.build_config))

    def test(self):
        self.run(os.path.join('.', 'bin', 'example'))
