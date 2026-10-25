cmake_minimum_required(VERSION 3.14)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(OMEditLocalAutocompleteTests LANGUAGES CXX)
  enable_testing()
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network Test)
set(CMAKE_AUTOMOC ON)

set(autocomplete_dir "${CMAKE_CURRENT_SOURCE_DIR}/../../OMEditLIB/Autocomplete")
add_library(OMEditLocalAutocompleteTestSupport STATIC
  "${autocomplete_dir}/LocalCompletionSettings.h"
  "${autocomplete_dir}/LocalCompletionClient.h"
  "${autocomplete_dir}/LocalCompletionClient.cpp"
  "${autocomplete_dir}/InlineCompletionPreview.h"
  "${autocomplete_dir}/InlineCompletionPreview.cpp"
  "${autocomplete_dir}/InlineCompletionController.h"
  "${autocomplete_dir}/InlineCompletionController.cpp")
target_compile_features(OMEditLocalAutocompleteTestSupport PUBLIC cxx_std_17)
target_include_directories(OMEditLocalAutocompleteTestSupport PUBLIC "${autocomplete_dir}/..")
target_link_libraries(OMEditLocalAutocompleteTestSupport PUBLIC Qt6::Widgets Qt6::Network)

foreach(test_name LocalCompletionClientTest InlineCompletionTest)
  add_executable(${test_name} "${test_name}.cpp")
  target_link_libraries(${test_name} PRIVATE OMEditLocalAutocompleteTestSupport Qt6::Test)
  add_test(NAME ${test_name} COMMAND ${test_name})
  set_tests_properties(${test_name} PROPERTIES TIMEOUT 60 LABELS "OMEditTestsuite"
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endforeach()
