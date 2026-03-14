param (
    [string]$Port = "",
    [ValidateSet("core", "core2")]
    [string]$Board = "core2",
    [ValidateRange(1, 3)]
    [int]$SsChannel = 1,
    [ValidateRange(1, 2)]
    [int]$IntChannel = 1,
    [switch]$SkipUpload
)

$ErrorActionPreference = "Stop"

$CoreVersion = "3.2.5"
$BoardMap = @{
    core  = "m5stack:esp32:m5stack_core"
    core2 = "m5stack:esp32:m5stack_core2"
}

$SsPinMap = @{
    core  = @(13, 5, 0)
    core2 = @(19, 33, 0)
}
$IntPinMap = @{
    core  = @(35, 34)
    core2 = @(35, 34)
}
$MisoPinMap = @{
    core  = 19
    core2 = 38
}
$UartPinMap = @{
    core  = @(16, 17) # RX, TX
    core2 = @(13, 14) # RX, TX (Port C)
}

$FQBN = $BoardMap[$Board]
$SketchName = "M5Stack-SwitchControlerMonitor.ino"
$SelectedSsGpio = $SsPinMap[$Board][$SsChannel - 1]
$SelectedIntGpio = $IntPinMap[$Board][$IntChannel - 1]
$SelectedMisoGpio = $MisoPinMap[$Board]
$SelectedUartRxGpio = $UartPinMap[$Board][0]
$SelectedUartTxGpio = $UartPinMap[$Board][1]

$UserArduinoDir = Join-Path $HOME "Documents/Arduino"
$UserLibrariesDir = Join-Path $UserArduinoDir "libraries"
$UsbHostShieldDir = Join-Path $UserLibrariesDir "USB_Host_Shield_Library_2.0"

Write-Output "--- M5Stack Build Script (Core v$CoreVersion / Board: $Board) ---"
Write-Output "USB Module DIP: SS CH$SsChannel (GPIO$SelectedSsGpio), INT CH$IntChannel (GPIO$SelectedIntGpio)"
Write-Output "SPI: SCK=18 MOSI=23 MISO=$SelectedMisoGpio"
Write-Output "UART(Serial2): RX=$SelectedUartRxGpio TX=$SelectedUartTxGpio"
Write-Output "Using user library root: $UserLibrariesDir"

function Ensure-CoreInstalled {
    param([string]$Version)
    Write-Output "Checking Core m5stack:esp32@$Version..."
    $CoreList = arduino-cli core list | Out-String
    if ($CoreList -match "m5stack:esp32\s+$Version") {
        Write-Output "Core $Version already installed."
        return
    }

    Write-Output "Installing Core..."
    arduino-cli core update-index
    arduino-cli core install m5stack:esp32@$Version
}

function Ensure-LibraryInUserFolder {
    param(
        [string]$LibraryName,
        [string]$ExpectedDir
    )

    if (Test-Path $ExpectedDir) {
        Write-Output "Library $LibraryName found: $ExpectedDir"
        return
    }

    Write-Output "Library $LibraryName not found in Documents/Arduino. Installing..."
    arduino-cli lib install "$LibraryName"

    if (!(Test-Path $ExpectedDir)) {
        throw "Library $LibraryName was installed but not found at: $ExpectedDir"
    }

    Write-Output "Library $LibraryName installed: $ExpectedDir"
}

function Ensure-UsbHostShieldLibraryPatched {
    param([string]$LibDir)

    $avrPinsPath = Join-Path $LibDir "avrpins.h"
    $usbCorePath = Join-Path $LibDir "UsbCore.h"

    if (!(Test-Path $avrPinsPath)) {
        throw "avrpins.h not found: $avrPinsPath"
    }
    if (!(Test-Path $usbCorePath)) {
        throw "UsbCore.h not found: $usbCorePath"
    }

    $avrPinsText = Get-Content -Path $avrPinsPath -Raw
    $requiredPinLines = @(
        "MAKE_PIN(P13, 13); // Extra SS for M5Stack Core",
        "MAKE_PIN(P33, 33); // Extra SS for M5Stack Core2",
        "MAKE_PIN(P34, 34); // Extra INT for M5Stack Core/Core2",
        "MAKE_PIN(P35, 35); // Extra INT for M5Stack Core/Core2",
        "MAKE_PIN(P38, 38); // Core2 MISO"
    )

    $missingPins = @()
    foreach ($line in $requiredPinLines) {
        if ($avrPinsText -notmatch [regex]::Escape($line)) {
            $missingPins += $line
        }
    }

    if ($missingPins.Count -gt 0) {
        $markerPattern = 'MAKE_PIN\(P17,\s*17\);\s*// INT'
        $regex = [regex]$markerPattern
        if (!$regex.IsMatch($avrPinsText)) {
            throw "Could not find insertion marker in avrpins.h: $markerPattern"
        }

        $insertion = ($missingPins -join "`r`n")
        $avrPinsText = $regex.Replace($avrPinsText, { param($m) $m.Value + "`r`n" + $insertion }, 1)
        Set-Content -Path $avrPinsPath -Value $avrPinsText -Encoding UTF8
        Write-Output "Patched avrpins.h with missing ESP32 pin aliases."
    }

    $usbCoreText = Get-Content -Path $usbCorePath -Raw
    if ($usbCoreText -notmatch "USB_HOST_SHIELD_SS_TYPE") {
        $needle = "typedef MAX3421e<P5, P17> MAX3421E; // ESP32 boards"
        $replacement = @"
#ifndef USB_HOST_SHIELD_SS_TYPE
#define USB_HOST_SHIELD_SS_TYPE P5
#endif
#ifndef USB_HOST_SHIELD_INT_TYPE
#define USB_HOST_SHIELD_INT_TYPE P17
#endif
typedef MAX3421e<USB_HOST_SHIELD_SS_TYPE, USB_HOST_SHIELD_INT_TYPE> MAX3421E; // ESP32 boards (customizable)
"@

        if ($usbCoreText.Contains($needle)) {
            $usbCoreText = $usbCoreText.Replace($needle, $replacement.Trim())
            Set-Content -Path $usbCorePath -Value $usbCoreText -Encoding UTF8
            Write-Output "Patched UsbCore.h for customizable ESP32 SS/INT pins."
        }
        else {
            throw "Expected ESP32 typedef was not found in UsbCore.h"
        }
    }
}

# 1. Core
Ensure-CoreInstalled -Version $CoreVersion

# 2. Libraries (Documents/Arduino 配下を必須にする)
if (!(Test-Path $UserLibrariesDir)) {
    New-Item -ItemType Directory -Path $UserLibrariesDir | Out-Null
}

Ensure-LibraryInUserFolder -LibraryName "M5Unified" -ExpectedDir (Join-Path $UserLibrariesDir "M5Unified")
Ensure-LibraryInUserFolder -LibraryName "USB Host Shield Library 2.0" -ExpectedDir $UsbHostShieldDir
Ensure-UsbHostShieldLibraryPatched -LibDir $UsbHostShieldDir

# 3. Compile
Write-Output "Compiling $SketchName..."
Write-Output "FQBN: $FQBN"

$SsType = "P$SelectedSsGpio"
$IntType = "P$SelectedIntGpio"

$ExtraFlags = @(
    "-DESP32",
    "-DUSB_HOST_SHIELD_SS_TYPE=$SsType",
    "-DUSB_HOST_SHIELD_INT_TYPE=$IntType",
    "-DPIN_SPI_SCK=18",
    "-DPIN_SPI_MOSI=23",
    "-DPIN_SPI_MISO=$SelectedMisoGpio",
    "-DPIN_SPI_SS=$SelectedSsGpio",
    "-DUSB_MODULE_SS_CH=$SsChannel",
    "-DUSB_MODULE_INT_CH=$IntChannel",
    "-DUSB_HOST_SHIELD_SS_GPIO=$SelectedSsGpio",
    "-DUSB_HOST_SHIELD_INT_GPIO=$SelectedIntGpio",
    "-DSERIAL2_RX_PIN=$SelectedUartRxGpio",
    "-DSERIAL2_TX_PIN=$SelectedUartTxGpio"
) -join " "

Write-Output "Build flags: $ExtraFlags"
arduino-cli compile --fqbn $FQBN --libraries $UserLibrariesDir --build-property "build.extra_flags=$ExtraFlags" .

if ($LASTEXITCODE -ne 0) {
    Write-Output "Build failed!"
    exit 1
}
Write-Output "Build successful!"

# 4. Upload
if ($SkipUpload) {
    Write-Output "SkipUpload specified. Build only."
    exit 0
}

if ($Port -eq "") {
    Write-Output "Detecting COM port..."
    $BoardListOutput = arduino-cli board list | Out-String
    Write-Output $BoardListOutput

    $Lines = $BoardListOutput -split "`r`n"
    foreach ($Line in $Lines) {
        if ($Line -match "(COM\d+)") {
            $Port = $matches[1]
            break
        }
    }
}
else {
    Write-Output "Using specified port: $Port"
}

if ($Port) {
    Write-Output "Found port: $Port"
    Write-Output "Uploading to $Port..."
    arduino-cli upload -p $Port --fqbn $FQBN .

    if ($LASTEXITCODE -eq 0) {
        Write-Output "Upload successful!"
    }
    else {
        Write-Output "Upload failed!"
        exit 1
    }
}
else {
    Write-Output "No COM port found. Connect M5Stack and retry."
    exit 1
}
