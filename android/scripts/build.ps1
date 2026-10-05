$ErrorActionPreference = 'Stop'
$project = Split-Path $PSScriptRoot -Parent
$tools = Get-Content (Join-Path $project '.toolchain/paths.json') | ConvertFrom-Json
$env:JAVA_HOME = $tools.java_home
$env:ANDROID_HOME = $tools.sdk
$env:PATH = "$($tools.java_home)\bin;$env:PATH"
Push-Location $project
try {
    & $tools.gradle --no-daemon :app:testDebugUnitTest :app:lintRelease :app:assembleRelease :app:bundleRelease
    if ($LASTEXITCODE -ne 0) { throw 'build failed' }
} finally { Pop-Location }
