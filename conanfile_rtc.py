#!/usr/bin/env python
# -*- coding: utf-8 -*-
from conans import ConanFile


class ConcurrentQueueConan(ConanFile):
    name = "ConcurrentQueue"
    version = "1.0.3"
    description = " A fast multi-producer, multi-consumer lock-free concurrent queue "
    topics = ("conan," "ConcurrentQueue", "lockfree", "queue")
    url = "https://github.com/cameron314/concurrentqueue"
    homepage = url
    author = "Cameron <cameron@moodycamel.com>"
    license = "Simplified BSD"
    exports = ["LICENSE.md"]
    exports_sources = ["*"]
    no_copy_source = True

    def package(self):
        self.copy(pattern="LICENSE.md", dst="licenses")
        self.copy(pattern="*", dst="include", src=".", keep_path=True)

    def package_info(self):
        if not self.in_local_cache:
            self.cpp_info.includedirs = ["."]

    def package_id(self):
        self.info.header_only()
