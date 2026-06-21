param (
    [string]$Port = "",
    [ValidateSet("core", "core2", "cores3se")]
    [string]$Board,
    [string]$BinDir = ""
)

$ErrorActionPreference = "Stop"

$BoardMap = @{
    core     = "m5stack:esp32:m5stack_core"
    core2    = "m5stack:esp32:m5stack_core2"
    cores3se = "m5stack:esp32:m5stack_cores3"
}

$BuildDirMap = @{
    core     = "m5stack.esp32.m5stack_core"
    core2    = "m5stack.esp32.m5stack_core2"
    cores3se = "m5stack.esp32.m5stack_cores3"
}

$BuildRoot = Join-Path $PSScriptRoot "build"

function Get-BuildArtifactBoards {
    $artifactBoards = @()
    foreach ($candidateBoard in $BoardMap.Keys) {
        $boardBuildDir = Join-Path $BuildRoot $BuildDirMap[$candidateBoard]
        $namedExport = Join-Path $BuildRoot "SwitchSender_${candidateBoard}_*.bin"
        if ((Test-Path -LiteralPath $boardBuildDir -PathType Container) -or
            (Get-ChildItem -Path $namedExport -File -ErrorAction SilentlyContinue)) {
            $artifactBoards += $candidateBoard
        }
    }
    return $artifactBoards
}

function Test-BinDirBoardMismatch {
    param(
        [string]$SelectedBoard,
        [string]$SelectedBinDir
    )

    $actualPath = [System.IO.Path]::GetFullPath($SelectedBinDir)
    foreach ($candidateBoard in $BuildDirMap.Keys) {
        if ($candidateBoard -eq $SelectedBoard) { continue }

        $otherBuildDir = [System.IO.Path]::GetFullPath((Join-Path $BuildRoot $BuildDirMap[$candidateBoard]))
        if ($actualPath.TrimEnd('\\') -eq $otherBuildDir.TrimEnd('\\')) {
            return $true
        }
    }

    # A directory containing a board-specific named export is also unambiguous.
    foreach ($candidateBoard in $BoardMap.Keys) {
        if ($candidateBoard -ne $SelectedBoard -and
            (Get-ChildItem -Path (Join-Path $actualPath "SwitchSender_${candidateBoard}_*.bin") -File -ErrorAction SilentlyContinue)) {
            return $true
        }
    }
    return $false
}

$Artifacts = Get-BuildArtifactBoards
$BoardExplicitlySpecified = $PSBoundParameters.ContainsKey('Board')

if (!$BoardExplicitlySpecified) {
    $ConfigFile = Join-Path $PSScriptRoot "config.json"
    $Config = if (Test-Path -LiteralPath $ConfigFile) { Get-Content -LiteralPath $ConfigFile -Raw | ConvertFrom-Json } else { $null }

    if ($Artifacts.Count -gt 1) {
        throw @"
Multiple board build artifacts were found.
Please specify the target board explicitly.

Examples:
  .\flash.ps1 -Board core -Port COMx
  .\flash.ps1 -Board core2 -Port COMx
  .\flash.ps1 -Board cores3se -Port COM9
"@
    }
    elseif ($Artifacts.Count -eq 1) {
        if ($Config -and $Config.Board -and ([string]$Config.Board -ne $Artifacts[0])) {
            throw @"
config.json Board '$($Config.Board)' does not match the only available build artifact '$($Artifacts[0])'.
Please specify the target board explicitly.

Examples:
  .\flash.ps1 -Board core -Port COMx
  .\flash.ps1 -Board core2 -Port COMx
  .\flash.ps1 -Board cores3se -Port COM9
"@
        }
        $Board = $Artifacts[0]
    }
    elseif ($Config -and $Config.Board) {
        $Board = [string]$Config.Board
    }
    else {
        throw "No board was selected. Specify -Board core, -Board core2, or -Board cores3se."
    }
}

if (!$BoardMap.ContainsKey($Board)) {
    throw "Invalid board '$Board'. Specify -Board core, -Board core2, or -Board cores3se."
}

$FQBN = $BoardMap[$Board]
$ExpectedBinDir = Join-Path $BuildRoot $BuildDirMap[$Board]
$ExpectedBinDirDisplay = Join-Path "build" $BuildDirMap[$Board]

if ([string]::IsNullOrWhiteSpace($BinDir)) {
    $BinDir = $ExpectedBinDir
}

if (!(Test-Path -LiteralPath $BinDir -PathType Container)) {
    throw "Binary directory was not found: $BinDir`nRun '.\build.ps1 -Board $Board -ExportBinaries' first, or specify -BinDir."
}

if (Test-BinDirBoardMismatch -SelectedBoard $Board -SelectedBinDir $BinDir) {
    throw @"
Board/BinDir mismatch.
Board '$Board' expects: $ExpectedBinDirDisplay
Actual BinDir: $BinDir
"@
}

$BinDirFullPath = [System.IO.Path]::GetFullPath($BinDir)
$ExpectedBinDirFullPath = [System.IO.Path]::GetFullPath($ExpectedBinDir)
$BinDirDisplay = if ($BinDirFullPath.TrimEnd('\\') -eq $ExpectedBinDirFullPath.TrimEnd('\\')) {
    $ExpectedBinDirDisplay
} else {
    $BinDir
}

# --input-dir uploads the complete Arduino CLI export (bootloader,
# partitions, and application), rather than selecting an arbitrary .bin.
if (!(Get-ChildItem -LiteralPath $BinDir -Filter "*.bin" -File -Recurse -ErrorAction SilentlyContinue)) {
    throw "No .bin files found in binary directory: $BinDir"
}

if (!(Get-Command "arduino-cli" -ErrorAction SilentlyContinue)) {
    throw "arduino-cli not found. Install it from https://arduino.github.io/arduino-cli/installation/"
}

if ([string]::IsNullOrWhiteSpace($Port)) {
    Write-Output "Detecting COM port..."
    $BoardListOutput = arduino-cli board list | Out-String
    Write-Output $BoardListOutput
    foreach ($line in ($BoardListOutput -split "`r?`n")) {
        if ($line -match "(COM\d+)") {
            $Port = $matches[1]
            break
        }
    }
}

if ([string]::IsNullOrWhiteSpace($Port)) {
    throw "No COM port found. Connect M5Stack and retry, or specify -Port."
}

Write-Output "Board: $Board"
Write-Output "FQBN: $FQBN"
Write-Output "Binary directory: $BinDirDisplay"
Write-Output "Port: $Port"
Write-Output "Uploading pre-built binaries..."

arduino-cli upload -p $Port --fqbn $FQBN --input-dir $BinDir

if ($LASTEXITCODE -eq 0) {
    Write-Output "Upload successful!"
}
else {
    throw "Upload failed."
}
