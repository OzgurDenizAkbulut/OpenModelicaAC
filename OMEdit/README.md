# OMEdit
A Modelica connection editor for OpenModelica.

## Dependencies

  - [OpenModelica Compiler](../OMCompiler)
  - [OMPlot](../OMPlot)
  - [OMSimulator](../../../../OMSimulator)

## Build instructions

Follow the instructions matching your OS:

  - [OMCompiler/README.Linux.md](../OMCompiler/README.Linux.md)
  - [OMCompiler/README-macOS.md](../OMCompiler/README-macOS.md)
  - [OMCompiler/README.Windows.md](../OMCompiler/README.Windows.md)

### Compile/Debug from Qt Creator

Compile OpenModelica once with CMake using the build instructions above so all
dependencies of OMEdit are ready. Then follow these steps,

  - Open the top level `OpenModelica/CMakeLists.txt` as a project in Qt Creator.
  - Configure the project with the same compiler and CMake options you used on the
    command line. Qt Creator picks up `CMAKE_INSTALL_PREFIX` from the CMake configuration,
    so `OMEdit` ends up in the same install tree as the rest of OpenModelica.
  - Build the `install` target, or the `OMEdit` target followed by `install` if you only
    changed OMEdit sources.
  - Change the run settings to run the installed executable, e.g.
    `OpenModelica/build_cmake/install_cmake/bin/OMEdit`, so OMEdit finds `omc` and the
    shared libraries next to it.
  - Compile/debug OMEdit.

### Separate development profile

For the validated Windows 1.28 environment, see the
[developer checklist](../doc/OMEdit-1.28-development.md#developer-checklist),
[lessons learned](../doc/OMEdit-1.28-development.md#lessons-learned), and build,
validation and launch commands in the same guide.

Configure a fresh top-level build with `-DOM_OMEDIT_DEVELOPMENT_PROFILE=ON`
and a separate `CMAKE_INSTALL_PREFIX` to build **OMEdit AC (Development)**.
Its preferences use `openmodelica-ac/omedit-development.ini`, and its temporary
files use a separate `omedit-development` directory under the normal OMEdit
temporary directory. Official OMEdit preferences are not imported.

Local LLM autocomplete is enabled by default in this profile; saved preferences
still take precedence. Start a compatible local completion server separately.
Normal builds keep the stock identity and disabled local autocomplete default.

The defaults can be overridden with `OM_OMEDIT_APPLICATION_NAME`,
`OM_OMEDIT_SETTINGS_ORGANIZATION`, `OM_OMEDIT_SETTINGS_APPLICATION` and
`OM_OMEDIT_LOCAL_AUTOCOMPLETE_DEFAULT_ENABLED`. Empty cache values follow the
selected profile, including when the profile changes in an existing build.
Explicit overrides persist across profile changes. Keep the settings identifiers
stable across development versions.

## Coding Style

  - 2 spaces not tab
  - CamelCase except that first letter should be small.
  - Member variables should start with `m` and member pointers should start with `mp`.
  - Local pointers should start with `p`.
  - Use meaningful name for variables and functions.


## Bug Reports

  - Submit bugs through the [OpenModelica GitHub issues](https://github.com/OpenModelica/OpenModelica/issues/new).
  - [Pull requests](https://github.com/OpenModelica/OpenModelica/pulls) are welcome ❤️
