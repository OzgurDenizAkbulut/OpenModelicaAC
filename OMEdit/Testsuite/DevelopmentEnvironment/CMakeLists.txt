add_executable(OMEditDevelopmentSmoke EXCLUDE_FROM_ALL
  DevelopmentEnvironmentSmoke.cpp EditorCompletionSmoke.h PlotResultSmoke.h DiagramSmoke.h)
target_link_libraries(OMEditDevelopmentSmoke PRIVATE OMEditLib Qt6::Test)
if(WIN32)
  file(GENERATE OUTPUT "$<TARGET_FILE_DIR:OMEditDevelopmentSmoke>/qt.conf"
    CONTENT "[Paths]\nPrefix=${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}\nBinaries=.\nLibraries=.\nLibraryExecutables=.\nPlugins=.\nTranslations=share/qt6/translations\nData=.\nArchData=.\n")
endif()
add_test(NAME OMEditDevelopmentSmoke COMMAND OMEditDevelopmentSmoke)
set_tests_properties(OMEditDevelopmentSmoke PROPERTIES
  TIMEOUT 75 LABELS "OMEditTestsuite;DevelopmentEnvironment"
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;OMEDIT_SMOKE_OUTPUT=${CMAKE_CURRENT_BINARY_DIR}/evidence")
