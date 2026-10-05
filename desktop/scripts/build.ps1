param([string]$BuildDirectory = '', [string]$Configuration = 'Release')
$ErrorActionPreference = 'Stop'
$project = Split-Path $PSScriptRoot -Parent
if (!$BuildDirectory) { $BuildDirectory = Join-Path $project 'build' }
& cmake -S $project -B $BuildDirectory
if ($LASTEXITCODE -ne 0) { throw 'configure failed' }
& cmake --build $BuildDirectory --config $Configuration
if ($LASTEXITCODE -ne 0) { throw 'build failed' }
& ctest --test-dir $BuildDirectory -C $Configuration --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'checks failed' }
