[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$version = '0.1.0'
$repoRoot = Split-Path -Parent $PSScriptRoot
$exePath = Join-Path $repoRoot 'artifacts/x64/Release/Portable/WardogsPortable.exe'
if (-not (Test-Path -LiteralPath $exePath)) {
    throw 'Build WardogsPortable in Release | x64 in Visual Studio first.'
}

$exe = Get-Item -LiteralPath $exePath
$bytes = [IO.File]::ReadAllBytes($exePath)
if ($bytes.Length -lt 64 -or [BitConverter]::ToUInt16($bytes, 0) -ne 0x5A4D) {
    throw 'The file is not a Windows executable.'
}
$peOffset = [BitConverter]::ToInt32($bytes, 0x3C)
if ($peOffset -lt 64 -or $peOffset -gt $bytes.Length - 26 -or
    [BitConverter]::ToUInt32($bytes, $peOffset) -ne 0x4550 -or
    [BitConverter]::ToUInt16($bytes, $peOffset + 4) -ne 0x8664 -or
    [BitConverter]::ToUInt16($bytes, $peOffset + 24) -ne 0x20B) {
    throw 'The release must be a Windows x64 executable.'
}

# Check the binary itself: .gitignore cannot hide paths embedded by the compiler.
$ascii = [Text.Encoding]::ASCII.GetString($bytes)
$unicode = [Text.Encoding]::Unicode.GetString($bytes)
$privatePaths = @('(?i)[a-z]:\\users\\', [regex]::Escape($repoRoot))
if ($env:USERPROFILE) { $privatePaths += [regex]::Escape($env:USERPROFILE) }
foreach ($pattern in $privatePaths) {
    if ($ascii -match $pattern -or $unicode -match $pattern) {
        throw 'Personal or workspace paths are embedded in the EXE. Rebuild the updated Release project.'
    }
}
if ($exe.VersionInfo.ProductVersion -ne $version) {
    throw "Expected product version $version. Rebuild the updated Release project."
}

$inputDirectories = @('Portable', 'Core', 'Capture', 'OCR', 'Settings', 'Shared', 'GameBarWidget/Assets')
foreach ($directory in $inputDirectories) {
    $path = Join-Path $repoRoot "WardogsGameBarCpp/$directory"
    $newerInput = Get-ChildItem -LiteralPath $path -File -Recurse |
        Where-Object { $_.Extension -in '.cpp', '.h', '.inl', '.manifest', '.rc', '.vcxproj', '.png', '.ico' -and $_.LastWriteTimeUtc -gt $exe.LastWriteTimeUtc } |
        Select-Object -First 1
    if ($newerInput) { throw 'Source files are newer than the EXE. Rebuild Release | x64 first.' }
}

$destination = Join-Path $repoRoot "artifacts/releases/portable-v$version"
New-Item -ItemType Directory -Path $destination -Force | Out-Null
$releaseExe = Join-Path $destination 'WardogsPortable.exe'
Copy-Item -LiteralPath $exePath -Destination $releaseExe -Force
$hash = (Get-FileHash -LiteralPath $releaseExe -Algorithm SHA256).Hash.ToLowerInvariant()
[IO.File]::WriteAllText((Join-Path $destination 'SHA256SUMS.txt'), "$hash  WardogsPortable.exe`n", [Text.Encoding]::ASCII)
# The license accompanies redistribution; it is not a runtime dependency.
Copy-Item -LiteralPath (Join-Path $repoRoot 'LICENSE') -Destination $destination -Force
Write-Output "Portable v$version prepared: $destination"
Write-Output "SHA256: $hash"
