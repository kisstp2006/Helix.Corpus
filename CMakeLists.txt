cmake_minimum_required(VERSION 3.28)
project(Helix.Corpus LANGUAGES CXX)

if(NOT COMMAND helix_add_library)
    include("${CMAKE_CURRENT_SOURCE_DIR}/External/Helix.CMake/Helix.cmake")
endif()

helix_require(Core)
helix_require(Platform)
helix_require(Profiler)
helix_require(Jobs)
helix_require(Image)
helix_require(Serialization)
helix_require(Font)
helix_require(Audio)
helix_require(Model)

set(corpus_deps Helix::Image Helix::Serialization Helix::Font Helix::Audio Helix::Model Helix::Platform)

# Every corpus file read with the libraries and compared with what other tools read from it.
helix_add_test_suite(helix_corpus_tests DEPS ${corpus_deps})
# Every corpus file a seed of its parser.
helix_add_fuzz_suite(helix_corpus_fuzz DEPS ${corpus_deps} ROUNDS 200)

foreach(target helix_corpus_tests helix_corpus_fuzz)
    if(TARGET ${target})
        target_compile_definitions(${target} PRIVATE HELIX_CORPUS_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
        target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Tests)
        if(HELIX_PLATFORM STREQUAL "web")
            # The corpus is read from the disk of the machine that runs Node.js.
            target_link_options(${target} PRIVATE -sNODERAWFS=1)
        endif()
    endif()
endforeach()
