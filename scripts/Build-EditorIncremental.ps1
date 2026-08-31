[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string] $Configuration = 'Debug'
)

$ErrorActionPreference = 'Stop'

# Deliberately builds only the editor target with CMake's normal MSVC Debug
# linker settings. Do not add /p:LinkIncremental=false here: that converts a
# one-file edit into a full, memory-heavy link of the entire editor.
cmake --build build --config $Configuration --target CreationEngineEditor --parallel 1
if ($LASTEXITCODE -ne 0) {
    throw "Creation Engine incremental build failed with exit code $LASTEXITCODE."
}
