function(om_development_import_webengine module dependencies)
  if(NOT WIN32)
    message(FATAL_ERROR "The development WebEngine adapter requires the Windows UCRT64 SDK.")
  endif()
  set(root "${OM_DEVELOPMENT_QT_ROOT}")
  string(TOLOWER "qt${module}version.h" version_header)
  foreach(path "bin/Qt6${module}.dll" "lib/libQt6${module}.dll.a"
               "include/qt6/Qt${module}/${version_header}")
    if(NOT EXISTS "${root}/${path}")
      message(FATAL_ERROR "Development Qt SDK is missing ${root}/${path}")
    endif()
  endforeach()
  if(NOT IS_DIRECTORY "${root}/include/qt6/Qt${module}")
    message(FATAL_ERROR "Development Qt SDK is missing Qt${module} headers.")
  endif()
  if(NOT TARGET Qt6::${module})
    string(TOUPPER "${module}" definition)
    add_library(Qt6::${module} SHARED IMPORTED GLOBAL)
    set_target_properties(Qt6::${module} PROPERTIES
      IMPORTED_LOCATION "${root}/bin/Qt6${module}.dll"
      IMPORTED_IMPLIB "${root}/lib/libQt6${module}.dll.a"
      INTERFACE_INCLUDE_DIRECTORIES "${root}/include/qt6;${root}/include/qt6/Qt${module}"
      INTERFACE_COMPILE_DEFINITIONS "QT_${definition}_LIB"
      INTERFACE_QT_MAJOR_VERSION 6
      INTERFACE_LINK_LIBRARIES "${dependencies}")
  endif()
endfunction()
