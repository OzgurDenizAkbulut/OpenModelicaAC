

set(OMPLOTLIB_SOURCES Plot.cpp
                      PlotZoomer.cpp
                      Legend.cpp
                      PlotPanner.cpp
                      PlotPicker.cpp
                      PlotGrid.cpp
                      PlotCurve.cpp
                      PlotWindow.cpp
                      PlotApplication.cpp
                      PlotWindowContainer.cpp
                      PlotMainWindow.cpp
                      ScaleDraw.cpp
                      LogScaleEngine.cpp
                      LinearScaleEngine.cpp
                      resource_omplot.qrc)

set(OMPLOTLIB_HEADERS OMPlot.h
                      PlotZoomer.h
                      Legend.h
                      PlotPanner.h
                      PlotPicker.h
                      PlotGrid.h
                      PlotCurve.h
                      PlotWindow.h
                      PlotApplication.h
                      PlotWindowContainer.h
                      PlotMainWindow.h
                      ScaleDraw.h
                      LogScaleEngine.h
                      LinearScaleEngine.h)

add_library(OMPlotLib SHARED ${OMPLOTLIB_SOURCES} ${OMPLOTLIB_HEADERS})
target_compile_definitions(OMPlotLib PRIVATE OMPLOTLIB_MOC_INCLUDE OMPLOTLIB_LIBRARY)

if(MSVC)
  set_target_properties(OMPlotLib PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS true)
endif()

target_include_directories(OMPlotLib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(OMPlotLib PUBLIC Qt6::Widgets)
target_link_libraries(OMPlotLib PUBLIC Qt6::PrintSupport)
target_link_libraries(OMPlotLib PUBLIC Qt6::Svg)

target_link_libraries(OMPlotLib PUBLIC Qt6::Core5Compat)

target_link_libraries(OMPlotLib PUBLIC omqwt)
target_link_libraries(OMPlotLib PUBLIC omc::result)

if(APPLE)
  set(MACOSX_BUNDLE_ICON_FILE omplot.ico)

  # The following tells CMake where to find and install the file itself.
  set(app_icon_macos "${CMAKE_CURRENT_SOURCE_DIR}/Resources/icons/omplot.ico")
  set_source_files_properties(${app_icon_macos} PROPERTIES
       MACOSX_PACKAGE_LOCATION "Resources")
else()
  set(app_icon_macos "")
endif()

add_executable(OMPlot WIN32 main.cpp rc_omplot.rc ${app_icon_macos})
target_link_libraries(OMPlot PRIVATE OMPlotLib)

install(TARGETS OMPlotLib)
omc_install_gui_client(OMPlot)

if(MINGW)
  omc_get_msys_prefix(MSYSTEM_PREFIX_ESCAPED)
  set(OM_QT_DEPLOYMENT_PREFIX "${MSYSTEM_PREFIX_ESCAPED}" CACHE PATH "Qt SDK prefix used to deploy Windows GUI dependencies.")
  file(TO_CMAKE_PATH "${OM_QT_DEPLOYMENT_PREFIX}" OM_QT_DEPLOYMENT_PREFIX)

  # Find windeployqt6
  find_program(_omplot_windeployqt
    NAMES windeployqt windeployqt6
    PATHS "${OM_QT_DEPLOYMENT_PREFIX}/bin"
    NO_DEFAULT_PATH
    REQUIRED
  )
  set(WINDEPLOYQT_EXECUTABLE "${_omplot_windeployqt}")
  unset(_omplot_windeployqt CACHE)

  install(CODE "
    set(ENV{PATH} \"${OM_QT_DEPLOYMENT_PREFIX}/bin;${OM_QT_DEPLOYMENT_PREFIX}/share/qt6/bin;\$ENV{PATH}\")
    message(STATUS \"Running windeployqt6 on OMPlot...\")
    execute_process(
      COMMAND \"${WINDEPLOYQT_EXECUTABLE}\"
        --no-compiler-runtime
        --no-system-d3d-compiler
        --no-quick-import
        --libdir \"\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}\"
        --translationdir \"\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}/share/qt6/translations\"
        \"\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}/OMPlot.exe\"
      RESULT_VARIABLE WINDEPLOYQT_RESULT
      OUTPUT_VARIABLE WINDEPLOYQT_OUTPUT
      ERROR_VARIABLE  WINDEPLOYQT_ERROR
    )
    if(NOT WINDEPLOYQT_RESULT EQUAL 0)
      message(FATAL_ERROR \"windeployqt6 failed:\n\${WINDEPLOYQT_OUTPUT}\n\${WINDEPLOYQT_ERROR}\")
    endif()
    message(STATUS \"windeployqt6 output: \${WINDEPLOYQT_OUTPUT}\")
  ")
endif()
