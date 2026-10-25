# OpenModelica 1.28 development environment

## Agreed outcome

Build the normal C/MetaModelica compiler and the customized OMEdit from one pinned
source revision, retaining local autocomplete. Use this environment for continued
OMEdit development and local modeling. Update upstream deliberately, with validation
before adopting another revision. Produce a Windows installer in the next milestone;
design for a later macOS distribution without implementing packaging now.

The application must coexist with official OpenModelica: separate installation and
preferences, no changes to existing file associations. Initial distribution is for
the owner and a small group of testers.

## Baseline and boundaries

- Initial source baseline: `40d9aae9b8c95e64b9ecad3cdc044189cf2df171`, described by Git
  as `v1.28.0-dev-1489-g40d9aae9b8`, plus the existing local autocomplete changes.
- Keep the existing topic branch and all working changes. Do not update upstream,
  commit, push, change submodule pins, or overwrite the working 1.27 deployment.
- Use the repository's top-level CMake build and GNU/UCRT64 toolchain. Keep the
  experimental Rust compiler disabled; build the Rust support libraries it requires.
- Candidate build and install directories must be fresh children of `build_cmake`.
  Never install through the current `install_cmake` junctions into OpenModelica 1.27.
- Provision missing development dependencies under the workspace when practical.
  Do not alter the original OpenModelica distribution or global tool configuration.
- Compiler DLLs, generated scripting interfaces, builtin definitions and simulation
  runtime must come from the candidate source build. No legacy compiler adapters.
- Record dependency/toolchain provenance, commands, logs and any temporary feature
  exclusions. Animation and optional applications are separate release decisions.

## Implementation sequence

1. **Preserve and inventory.** Capture working-tree state and the current executable
   identity; inspect available toolchain, SDK, disk and memory. Initialize required
   submodules at the repository's pinned commits. Keep downloads and caches bounded.
2. **Establish the supported build.** Provide a reproducible development entry point,
   an explicit dependency/toolchain configuration and a fresh installation prefix.
   Resolve Qt/WebEngine discovery and compiler dependencies. Initially cap build
   concurrency at two jobs; increase only if measured memory permits it. One build
   owner runs builds, and tests run only after compilation stops.
3. **Build the matching compiler.** Bootstrap and install the C compiler, its generated
   Qt scripting API and C/Rust support runtimes. Verify the installed compiler's
   revision and basic model checking, simulation and result reading before adopting it.
4. **Integrate the customized GUI.** Build OMEdit from the same checkout with the
   existing autocomplete sources. Provide a distinct development application/settings
   identity using configurable defaults, retaining stock behavior for normal builds.
   Enable autocomplete in this development profile. Do not import official preferences
   implicitly. Remove dependency on the ignored 1.27 compatibility harness.
5. **Stage and validate.** Install/deploy native dependencies into the fresh candidate
   tree. Check loaded DLL provenance and compiler/data paths, exercise supported
   compiler APIs, run focused autocomplete tests and full-application smoke coverage,
   and check native UI behavior when the available automation surface permits it.
   Verify a real local completion endpoint if available; distinguish deterministic
   transport tests from live model quality and availability.
6. **Deliver a repeatable development workflow.** Provide build, validation and launch
   commands, record the exact revision and dependency versions, and verify an
   incremental OMEdit rebuild. Document actual limitations and the remaining installer
   milestone. Switch the development launcher only after acceptance checks pass.

## Acceptance evidence

- Installed `omc` and OMEdit identify the chosen 1.28 development baseline.
- OMEdit links the matching compiler and generated API without 1.27 stubs.
- No compiler/runtime/header/data junction or DLL resolves to the 1.27 installation.
  Any temporary toolchain reuse is separately disclosed and eliminated from the final
  development launcher where feasible.
- Model editing, loading, diagram/model-instance retrieval, checking, simulation,
  result reading/plotting and a simple FMU export are exercised against the candidate.
- Focused client and inline-completion tests pass. Full-application coverage confirms
  autocomplete configuration and settings isolation; enabled development settings
  are verified independently of backend availability.
- Startup reports no unknown `-d=omedit` flag or missing compiler symbols. Previously
  adapted APIs are exercised where they are relevant to this milestone.
- The original installation, preferences and working deployment remain intact.
- The documented incremental build keeps the compiler unchanged for an OMEdit-only
  edit when its dependencies have not changed.

## Feasibility checkpoint

Roughly one working day is a decision checkpoint, not a completion estimate. Record
actual blockers, disk consumption, memory pressure and progress. If full compilation
requires substantial infrastructure changes, report the concrete alternatives rather
than silently substituting a prebuilt compiler or dropping required capabilities.

## Execution ownership

Implementation uses GPT-6.1 Sol coders at medium reasoning. Separate owners handle
compiler/build orchestration, GUI identity/integration and acceptance tooling. Only
the build owner executes compilation; other agents prepare independent changes and
review evidence. The coordinator reviews integration and reports verified outcomes.
