# Agent Instructions

## Build Output

- Always build Creation Engine in the existing `D:\000 Creation Engine\build` directory.
- Do not create alternate or scratch build folders (`build-opengl`, `build-test`, or similar).
- If a different build directory ever seems necessary, stop and discuss it with the user before doing anything.
- Configure with `-DJUCE_DIR=D:\JUCE2\JUCE` (the same JUCE checkout Creation Station uses) so both projects stay on one JUCE version.

## Sibling Project

- Creation Station (`D:\000 Creation Station`) is the sibling JUCE audio app from the same author/company (LagDaemon Software). Match its conventions (JUCE idioms, `Source/` layout, `PascalCase` domain folders, MIT license, company/bundle-id naming) unless there's a concrete reason to diverge.
