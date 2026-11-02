<#
.SYNOPSIS
    Builds a RED4ext plugin once per entry in plugin/builds.json, each against its pinned RED4ext.SDK commit.

.DESCRIPTION
    Canonical: MyMods/_shared/plugin/build.ps1. Copy it byte-identical into plugin/ and edit it there only;
    release-check fails a mod whose copy differs. Everything specific to a mod lives in plugin/builds.json:

      {
        "name": "MyPlugin",
        "cmake": ["-DEXTRA=ON"],
        "builds": [
          { "game": "2.5",  "sdk": "<40-char RED4ext.SDK commit>", "ship": "red4ext/plugins/MyPlugin" },
          { "game": "2.31", "sdk": "<commit>", "ship": "compat/2.31/red4ext/plugins/MyPlugin" }
        ],
        "variants": { "tune": { "cmake": ["-DMY_TUNE=ON"] } }
      }

    "game" is a label ("any" for a runtime-independent plugin). Each build compiles against an export of
    the SDK at its "sdk" commit, made with `git archive` into plugin/build/sdk/<commit>/; the SDK clone
    itself is only read. The DLL is copied to "ship", and the DLL and its PDB to
    symbols/<version>/<game>/, where <version> is the RED4EXT_V1_SEMVER in plugin/src. A PDB matches
    only the exact DLL built with it, so symbols/ is committed with every release.

    -Variant builds a variant from "variants" into plugin/build-<variant>/<game>/ and ships nothing.

.PARAMETER SdkRepo
    A RED4ext.SDK git clone. Defaults to the workspace's _source/RED4ext.SDK.

.EXAMPLE
    pwsh -File plugin\build.ps1
    pwsh -File plugin\build.ps1 -Variant tune
#>
param(
    [string]$Variant,
    [string]$SdkRepo
)
$ErrorActionPreference = 'Stop'

$Plugin = $PSScriptRoot
$Root = Resolve-Path (Join-Path $Plugin '..')
if (-not $SdkRepo) { $SdkRepo = Join-Path $Plugin '..\..\..\_source\RED4ext.SDK' }
$SdkRepo = Resolve-Path $SdkRepo
$Config = Get-Content -Raw (Join-Path $Plugin 'builds.json') | ConvertFrom-Json
$Name = $Config.name

$vsRoots = @(
    "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\BuildTools",
    "${env:ProgramFiles}\Microsoft Visual Studio\2022\Community",
    "${env:ProgramFiles}\Microsoft Visual Studio\2022\Professional",
    "${env:ProgramFiles}\Microsoft Visual Studio\2022\Enterprise"
)
$CMake = $vsRoots | ForEach-Object { "$_\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" } |
    Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $CMake) { $CMake = (Get-Command cmake -ErrorAction SilentlyContinue).Source }
if (-not $CMake) { throw 'cmake not found: install the VS 2022 Build Tools with the C++ workload' }

$semver = Select-String -Path (Join-Path $Plugin 'src\*.cpp') -Pattern 'RED4EXT_V1_SEMVER\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\)' |
    Select-Object -First 1
if (-not $semver) { throw 'no RED4EXT_V1_SEMVER(...) in plugin/src' }
$Version = ($semver.Matches[0].Groups[1..3] | ForEach-Object Value) -join '.'

$extra = @($Config.cmake | Where-Object { $_ })
if ($Variant) {
    $v = $Config.variants.$Variant
    if (-not $v) { throw "builds.json has no variant '$Variant'" }
    $extra += @($v.cmake | Where-Object { $_ })
}

foreach ($b in $Config.builds) {
    if ($b.sdk -notmatch '^[0-9a-f]{40}$') { throw "build '$($b.game)': sdk must be a full 40-character commit" }

    $sdk = Join-Path $Plugin "build\sdk\$($b.sdk)"
    if (-not (Test-Path (Join-Path $sdk 'include\RED4ext\RED4ext.hpp'))) {
        New-Item -ItemType Directory -Force $sdk | Out-Null
        $tar = Join-Path $sdk 'sdk.tar'
        git -C $SdkRepo archive --format=tar -o $tar $b.sdk
        if ($LASTEXITCODE -ne 0) { throw "git archive of RED4ext.SDK $($b.sdk) failed: fetch the SDK clone" }
        # Windows' own tar: Git's GNU tar reads a drive letter as a remote host.
        & (Join-Path $env:SystemRoot 'System32\tar.exe') -xf $tar -C $sdk
        if ($LASTEXITCODE -ne 0) { throw "extracting RED4ext.SDK $($b.sdk) failed" }
        Remove-Item $tar
    }

    $out = if ($Variant) { Join-Path $Plugin "build-$Variant\$($b.game)" } else { Join-Path $Plugin "build\$($b.game)" }
    Write-Host "== $Name $Version, game $($b.game), SDK $($b.sdk.Substring(0, 8))$(if ($Variant) { ", variant $Variant" })" -ForegroundColor Cyan
    & $CMake -S $Plugin -B $out -G 'Visual Studio 17 2022' -A x64 "-DRED4EXT_SDK_DIR=$sdk" @extra | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "configure failed for game $($b.game)" }
    & $CMake --build $out --config Release
    if ($LASTEXITCODE -ne 0) { throw "build failed for game $($b.game)" }

    $dll = Join-Path $out "Release\$Name.dll"
    $pdb = Join-Path $out "Release\$Name.pdb"
    if ($Variant) { Write-Host "   built $dll" -ForegroundColor Green; continue }

    $ship = Join-Path $Root $b.ship
    New-Item -ItemType Directory -Force $ship | Out-Null
    Copy-Item $dll $ship -Force
    $sym = Join-Path $Root "symbols\$Version\$($b.game)"
    New-Item -ItemType Directory -Force $sym | Out-Null
    Copy-Item $dll, $pdb $sym -Force
    Write-Host "   shipped $($b.ship)\$Name.dll; DLL and PDB kept in symbols\$Version\$($b.game)" -ForegroundColor Green
}
