# - Try to find Jaejer includes dirs and libraries
#
# Usage of this module as follows:
#
#     find_package(Jaejer)
#
# Variables used by this module, they can change the default behaviour and need
# to be set before calling find_package:
#
# Variables defined by this module:
#
#  Jaejer_FOUND            System has Jaejer, include and lib dirs found
#  Jaejer_INCLUDE_DIR      The Jaejer includes directories.
#  Jaejer_LIBRARY          The Jaejer library.

find_path(Jaejer_INCLUDE_DIR NAMES jaegertracing)
find_library(Jaejer_LIBRARY NAMES libjaegertracing.a)

# opentracing
find_path(Opentracing_INCLUDE_DIR NAMES opentracing)
find_library(Opentracing_LIBRARY NAMES libopentracing.a)

# yaml
find_path(Yaml_INCLUDE_DIR NAMES yaml-cpp)
find_library(Yaml_LIBRARY NAMES libyaml-cpp.a)

if(Jaejer_INCLUDE_DIR AND Jaejer_LIBRARY)
    set(Jaejer_FOUND TRUE)
    mark_as_advanced(
        Jaejer_INCLUDE_DIR
        Jaejer_LIBRARY
    )
endif()

if(NOT Jaejer_FOUND)
    message(FATAL_ERROR "Jaejer doesn't exist")
endif()

