cmake_minimum_required (VERSION 3.14)

project(OMEdit)
## Anything installed from this directory and its subdirectories belongs
## to the install component 'omedit' if it is not explicitly changed.
set(CMAKE_INSTALL_DEFAULT_COMPONENT_NAME omedit)

omc_option(OM_OMEDIT_INSTALL_RUNTIME_DLLS "Install the required runtime dll dependency DLLs to the binary directory. Valid only for Windows builds." ON)
set(OM_OMEDIT_ANALYSIS_RUNTIME_DIR "" CACHE PATH "Pre-provisioned, validated optional analysis Python runtime directory")
omc_option(OM_OMEDIT_ENABLE_TESTS "Enable building of OMEdit Testsuite tests." OFF)
omc_option(OM_OMEDIT_ENABLE_ANIMATION "Enable the 3D animation windows. Needs Qt Quick 3D." ON)
omc_option(OM_OMEDIT_DEVELOPMENT_PROFILE "Use a separate OMEdit development identity and enable local autocomplete by default." OFF)

if(OM_OMEDIT_DEVELOPMENT_PROFILE)
  set(OMEDIT_DEFAULT_APPLICATION_NAME "OMEdit AC (Development)")
  set(OMEDIT_DEFAULT_SETTINGS_ORGANIZATION "openmodelica-ac")
  set(OMEDIT_DEFAULT_SETTINGS_APPLICATION "omedit-development")
else()
  set(OMEDIT_DEFAULT_APPLICATION_NAME "OMEdit")
  set(OMEDIT_DEFAULT_SETTINGS_ORGANIZATION "openmodelica")
  set(OMEDIT_DEFAULT_SETTINGS_APPLICATION "omedit")
endif()
foreach(identity APPLICATION_NAME SETTINGS_ORGANIZATION SETTINGS_APPLICATION)
  set(OM_OMEDIT_${identity} "" CACHE STRING "OMEdit ${identity} override; empty selects the profile default.")
  if("${OM_OMEDIT_${identity}}" STREQUAL "")
    set(OM_OMEDIT_${identity} "${OMEDIT_DEFAULT_${identity}}")
  endif()
  if("${OM_OMEDIT_${identity}}" MATCHES "[\r\n]")
    message(FATAL_ERROR "OM_OMEDIT_${identity} must not contain line breaks.")
  endif()
  string(REPLACE "\\" "\\\\" OMEDIT_${identity}_ESCAPED "${OM_OMEDIT_${identity}}")
  string(REPLACE "\"" "\\\"" OMEDIT_${identity}_ESCAPED "${OMEDIT_${identity}_ESCAPED}")
endforeach()
set(OM_OMEDIT_LOCAL_AUTOCOMPLETE_DEFAULT_ENABLED "" CACHE STRING "Local autocomplete default override: empty selects the profile default, or set ON/OFF.")
set_property(CACHE OM_OMEDIT_LOCAL_AUTOCOMPLETE_DEFAULT_ENABLED PROPERTY STRINGS "" ON OFF)
if("${OM_OMEDIT_LOCAL_AUTOCOMPLETE_DEFAULT_ENABLED}" STREQUAL "")
  set(OM_OMEDIT_LOCAL_AUTOCOMPLETE_DEFAULT_ENABLED ${OM_OMEDIT_DEVELOPMENT_PROFILE})
endif()

find_package(Qt6 COMPONENTS Widgets PrintSupport Xml OpenGL Network Svg REQUIRED)

find_package(Qt6 COMPONENTS WebEngineWidgets REQUIRED)

find_package(Qt6 COMPONENTS OpenGLWidgets Core5Compat REQUIRED)

if (Qt6_VERSION_MINOR GREATER_EQUAL 4)
  find_package(Qt6 OPTIONAL_COMPONENTS HttpServer)
endif ()

if(OM_OMEDIT_ENABLE_ANIMATION)
  find_package(Qt6 COMPONENTS Quick Quick3D Qml QuickWidgets REQUIRED)
endif()
find_package(OpenGL REQUIRED)

# Configure omedit_config.h. This will be generated in the build directory
configure_file(omedit_config.h.in omedit_config.h)
## Add a config library for OMEdit. It will provide access to common config headres such as
## config.h. So by linking to this library you get the include directories.
add_library(omedit_config INTERFACE)
add_library(omedit::config ALIAS omedit_config)
target_include_directories(omedit_config INTERFACE ${CMAKE_CURRENT_BINARY_DIR})

omc_add_subdirectory(OMEditLIB)
omc_add_subdirectory(OMEditGUI)

if(OM_OMEDIT_ENABLE_TESTS)
  omc_add_subdirectory(Testsuite)
endif()

install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/Resources/analysis/"
  DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/omedit/analysis"
  FILES_MATCHING PATTERN "*.py" PATTERN "test_*" EXCLUDE PATTERN "benchmark*" EXCLUDE PATTERN "__pycache__" EXCLUDE)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/Resources/analysis/requirements.txt"
  DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/omedit/analysis")
if(OM_OMEDIT_ANALYSIS_RUNTIME_DIR)
  find_package(Python3 COMPONENTS Interpreter REQUIRED)
  execute_process(COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_SOURCE_DIR}/tools/development/stage-analysis-runtime.py"
    --validate-runtime "${OM_OMEDIT_ANALYSIS_RUNTIME_DIR}"
    RESULT_VARIABLE _analysis_runtime_status OUTPUT_VARIABLE _analysis_runtime_output ERROR_VARIABLE _analysis_runtime_error
    TIMEOUT 90)
  if(NOT _analysis_runtime_status EQUAL 0)
    message(FATAL_ERROR "Invalid analysis runtime: ${_analysis_runtime_output}${_analysis_runtime_error}")
  endif()
  install(DIRECTORY "${OM_OMEDIT_ANALYSIS_RUNTIME_DIR}/"
    DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/omedit/analysis/runtime" USE_SOURCE_PERMISSIONS
    PATTERN "__pycache__" EXCLUDE)
endif()
