add_executable(AnalysisFigureDataTest AnalysisFigureDataTest.cpp)
target_link_libraries(AnalysisFigureDataTest PRIVATE OMEditLib Qt6::Test)
add_test(NAME AnalysisFigureDataTest COMMAND AnalysisFigureDataTest)
set_tests_properties(AnalysisFigureDataTest PROPERTIES TIMEOUT 60
  LABELS "OMEditTestsuite;AnalysisFigures" ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(AnalysisFigureSmoke EXCLUDE_FROM_ALL AnalysisFigureSmoke.cpp)
target_link_libraries(AnalysisFigureSmoke PRIVATE OMEditLib Qt6::Test)
if(WIN32)
  file(GENERATE OUTPUT "$<TARGET_FILE_DIR:AnalysisFigureSmoke>/qt.conf"
    CONTENT "[Paths]\nPrefix=${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}\nBinaries=.\nLibraries=.\nLibraryExecutables=.\nPlugins=.\nTranslations=share/qt6/translations\nData=.\nArchData=.\n")
endif()
add_test(NAME AnalysisFigureSmoke COMMAND AnalysisFigureSmoke)
set_tests_properties(AnalysisFigureSmoke PROPERTIES TIMEOUT 120
  LABELS "OMEditTestsuite;AnalysisFigures" ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

find_package(Python3 COMPONENTS Interpreter QUIET)
if(OM_OMEDIT_ANALYSIS_RUNTIME_DIR)
  if(WIN32)
    set(_analysis_test_python "${OM_OMEDIT_ANALYSIS_RUNTIME_DIR}/python.exe")
  else()
    set(_analysis_test_python "${OM_OMEDIT_ANALYSIS_RUNTIME_DIR}/bin/python3")
  endif()
elseif(Python3_Interpreter_FOUND)
  set(_analysis_test_python "${Python3_EXECUTABLE}")
endif()
if(_analysis_test_python)
  add_test(NAME AnalysisFigureRendererTest
    COMMAND "${_analysis_test_python}" -m unittest discover
      -s "${CMAKE_CURRENT_SOURCE_DIR}/../../Resources/analysis" -p "test_*.py")
  set_tests_properties(AnalysisFigureRendererTest PROPERTIES TIMEOUT 120 LABELS "OMEditTestsuite;AnalysisFigures")
endif()
