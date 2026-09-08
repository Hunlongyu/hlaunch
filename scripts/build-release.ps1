[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [ValidateSet('x64', 'x86', 'arm64')]
    [string]$Architecture,
    [string]$ExpectedVersion,
    [string[]]$CMakeArguments = @()
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$releaseRoot = Split-Path -Parent $PSScriptRoot
$releaseCmake = Get-Content -LiteralPath (Join-Path $releaseRoot 'CMakeLists.txt') -Raw
$versionMatch = [regex]::Match($releaseCmake, '\bproject\(\s*HLaunch\s+VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)')
if (-not $versionMatch.Success) { throw 'Cannot read the CMake project version.' }
$releaseVersion = $versionMatch.Groups[1].Value
if ($ExpectedVersion -and $ExpectedVersion -cne $releaseVersion) { throw 'Tag and CMake versions differ.' }

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$component = if ($Architecture -eq 'arm64') { 'Microsoft.VisualStudio.Component.VC.Tools.ARM64' } else { 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64' }
$vsPath = & $vswhere -latest -prerelease -products '*' -requires $component -property installationPath
if ($LASTEXITCODE -ne 0 -or -not $vsPath) { throw "Visual Studio toolchain missing: $component" }
& (Join-Path $vsPath 'Common7/Tools/Launch-VsDevShell.ps1') -Arch $Architecture -HostArch amd64 -SkipAutomaticLocation -NoLogo
if ($env:VSCMD_ARG_TGT_ARCH -ne $Architecture) { throw 'Developer shell selected the wrong target architecture.' }

$releaseBuild = Join-Path $releaseRoot "out/build/release-$Architecture"
$runTests = $Architecture -ne 'arm64'
$testing = if ($runTests) { 'ON' } else { 'OFF' }
& cmake -S $releaseRoot -B $releaseBuild -G Ninja '-DCMAKE_BUILD_TYPE=Release' "-DBUILD_TESTING=$testing" @CMakeArguments
if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
& cmake --build $releaseBuild --parallel 4
if ($LASTEXITCODE -ne 0) { throw 'Release compilation failed.' }
if ($runTests) {
    # Desktop/session-dependent scenarios remain part of the manual release matrix.
    & ctest --test-dir $releaseBuild --output-on-failure --no-tests=error --timeout 120 -E 'PLAT-SINGLE|ACT-HOTKEY|PLAT-TRAY|PLAT-SHELL'
    if ($LASTEXITCODE -ne 0) { throw 'Release tests failed.' }
}

$executable = Join-Path $releaseBuild 'src/HLaunch.exe'
$fileVersion = [Diagnostics.FileVersionInfo]::GetVersionInfo($executable)
if ($fileVersion.FileVersion -cne $releaseVersion -or $fileVersion.ProductVersion -cne $releaseVersion) {
    throw 'Executable VERSIONINFO does not match CMake.'
}
$peBytes = [IO.File]::ReadAllBytes($executable)
$peOffset = [BitConverter]::ToInt32($peBytes, 0x3C)
$machine = [BitConverter]::ToUInt16($peBytes, $peOffset + 4)
$expectedMachine = @{ x64 = 0x8664; x86 = 0x014C; arm64 = 0xAA64 }[$Architecture]
if ($machine -ne $expectedMachine) { throw 'Executable PE architecture mismatch.' }

$manifestPath = Join-Path $releaseBuild 'embedded.manifest'
& mt.exe -nologo "-inputresource:$executable;#1" "-out:$manifestPath"
if ($LASTEXITCODE -ne 0) { throw 'Cannot extract the embedded application manifest.' }
$manifest = [xml](Get-Content -LiteralPath $manifestPath -Raw)
$manifestArch = @{ x64 = 'amd64'; x86 = 'x86'; arm64 = 'arm64' }[$Architecture]
if ($manifest.assembly.assemblyIdentity.processorArchitecture -cne $manifestArch) { throw 'Embedded manifest architecture mismatch.' }
if ($manifest.assembly.assemblyIdentity.version -cne "$releaseVersion.0") { throw 'Embedded manifest version mismatch.' }
$imports = & dumpbin.exe /nologo /dependents $executable
if ($LASTEXITCODE -ne 0) { throw 'Cannot inspect executable dependencies.' }
if (($imports -join "`n") -match '(?i)(vcruntime|msvcp|ucrtbase|api-ms-win-crt)[^\s]*\.dll') {
    throw 'Release unexpectedly depends on a dynamic C/C++ runtime.'
}

$releaseAssets = Join-Path $releaseRoot 'out/release'
New-Item -ItemType Directory -Path $releaseAssets -Force | Out-Null
$asset = Join-Path $releaseAssets "HLaunch-$releaseVersion-$Architecture.exe"
Copy-Item -LiteralPath $executable -Destination $asset -Force
Write-Output "Verified $Architecture v$releaseVersion -> $asset"
Get-FileHash -LiteralPath $asset -Algorithm SHA256 | Format-List
