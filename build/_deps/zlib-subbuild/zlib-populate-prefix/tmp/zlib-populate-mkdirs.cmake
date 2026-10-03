# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file LICENSE.rst or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION ${CMAKE_VERSION}) # this file comes with cmake

# If CMAKE_DISABLE_SOURCE_CHANGES is set to true and the source directory is an
# existing directory in our source tree, calling file(MAKE_DIRECTORY) on it
# would cause a fatal error, even though it would be a no-op.
if(NOT EXISTS "F:/projects/openchrome-6/build/_deps/zlib-src")
  file(MAKE_DIRECTORY "F:/projects/openchrome-6/build/_deps/zlib-src")
endif()
file(MAKE_DIRECTORY
  "F:/projects/openchrome-6/build/_deps/zlib-build"
  "F:/projects/openchrome-6/build/_deps/zlib-subbuild/zlib-populate-prefix"
  "F:/projects/openchrome-6/build/_deps/zlib-subbuild/zlib-populate-prefix/tmp"
  "F:/projects/openchrome-6/build/_deps/zlib-subbuild/zlib-populate-prefix/src/zlib-populate-stamp"
  "F:/projects/openchrome-6/build/_deps/zlib-subbuild/zlib-populate-prefix/src"
  "F:/projects/openchrome-6/build/_deps/zlib-subbuild/zlib-populate-prefix/src/zlib-populate-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "F:/projects/openchrome-6/build/_deps/zlib-subbuild/zlib-populate-prefix/src/zlib-populate-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "F:/projects/openchrome-6/build/_deps/zlib-subbuild/zlib-populate-prefix/src/zlib-populate-stamp${cfgdir}") # cfgdir has leading slash
endif()
