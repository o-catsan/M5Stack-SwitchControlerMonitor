param (
    [string]$Port = "",
    [ValidateSet("core", "core2", "cores3se")]
    [string]$Board = "core2",
    [ValidateSet("M5Stack-SwitchController2CoREWirelessSender.ino", "M5Stack-PS5CoREWirelessSender.ino", "M5Stack-PS5CoREWirelessSender_Blue.ino", "M5Stack-PS5CoREWirelessReceiver.ino")]
    [string]$SketchName = "M5Stack-SwitchController2CoREWirelessSender.ino",
    [ValidateRange(1, 3)]
    [int]$SsChannel = 1,
    [ValidateRange(1, 2)]
    [int]$IntChannel = 1,
    [switch]$SkipUpload,
    [switch]$ExportBinaries
)

$ErrorActionPreference = "Stop"

# Load settings from config.json if exists
$ConfigFile = Join-Path $PSScriptRoot "config.json"
$Config = if (Test-Path $ConfigFile) { Get-Content $ConfigFile | ConvertFrom-Json } else { @{} }

# Override Board if not specified in arguments but present in config
if (!$PSBoundParameters.ContainsKey('Board') -and $Config.Board) {
    $Board = $Config.Board
}

# 3.3.7 is the currently installed m5stack:esp32 release in this environment.
# The CoreS3 FQBN is also present in 3.2.5; keep the FQBN independent of this pin.
$CoreVersion = "3.3.7"
$BoardManagerUrl = "https://m5stack.oss-cn-shenzhen.aliyuncs.com/resource/arduino/package_m5stack_index.json"
$BoardMap = @{
    core     = "m5stack:esp32:m5stack_core"
    core2    = "m5stack:esp32:m5stack_core2"
    cores3se = "m5stack:esp32:m5stack_cores3"
}

$SsPinMap = @{
    core     = @(13, 5, 0)
    core2    = @(19, 33, 0)
    # USB Host Shield Library 2.0 PR #843: CoreS3 USB Module v1.2 CH2.
    # Other CoreS3 DIP channel mappings are deliberately not guessed.
    cores3se = @($null, 1, $null)
}
$IntPinMap = @{
    core     = @(35, 34)
    core2    = @(35, 34)
    cores3se = @($null, 14)
}
$MisoPinMap = @{
    core     = 19
    core2    = 38
    cores3se = 35
}
$SpiPinMap = @{
    core     = @(18, 23) # SCK, MOSI
    core2    = @(18, 23)
    cores3se = @(36, 37)
}
$UartPinMap = @{
    core     = @(16, 17) # RX, TX
    core2    = @(13, 14) # RX, TX (Port C)
    cores3se = @(18, 17) # RX, TX (Port C; M5Unified CoreS3 SE definition)
}

# Core/Core2 retain CH1/CH1. USB Host Shield Library 2.0 PR #843 validates
# CoreS3 with USB Module v1.2 set to SS CH2 and INT CH2.
if ($Board -eq "cores3se") {
    if (!$PSBoundParameters.ContainsKey('SsChannel')) { $SsChannel = 2 }
    if (!$PSBoundParameters.ContainsKey('IntChannel')) { $IntChannel = 2 }
}

$FQBN = $BoardMap[$Board]
$SelectedSsGpio = $SsPinMap[$Board][$SsChannel - 1]
$SketchPath = Join-Path $PSScriptRoot $SketchName
if (!(Test-Path $SketchPath)) {
    throw "Sketch file not found: $SketchPath"
}
$SelectedIntGpio = $IntPinMap[$Board][$IntChannel - 1]
$SelectedMisoGpio = $MisoPinMap[$Board]
$SelectedSckGpio = $SpiPinMap[$Board][0]
$SelectedMosiGpio = $SpiPinMap[$Board][1]
$SelectedUartRxGpio = $UartPinMap[$Board][0]
$SelectedUartTxGpio = $UartPinMap[$Board][1]

if ($null -eq $SelectedSsGpio -or $null -eq $SelectedIntGpio) {
    throw "Board '$Board' supports only USB Module v1.2 SS CH2 / INT CH2. The selected channel is not defined by USB Host Shield Library 2.0 PR #843."
}

# Set Arduino directory (priority: config.json > default)
$DefaultArduinoDir = Join-Path $HOME "Documents/Arduino"
$UserArduinoDir = if ($Config.ArduinoDir) { $Config.ArduinoDir } else { $DefaultArduinoDir }

$UserLibrariesDir = Join-Path $UserArduinoDir "libraries"
$UsbHostShieldDir = Join-Path $UserLibrariesDir "USB_Host_Shield_Library_2.0"

Write-Output "--- M5Stack Build Script (Core v$CoreVersion / Board: $Board) ---"
Write-Output "USB Module DIP: SS CH$SsChannel (GPIO$SelectedSsGpio), INT CH$IntChannel (GPIO$SelectedIntGpio)"
Write-Output "FQBN: $FQBN"
Write-Output "SPI: SCK=$SelectedSckGpio MOSI=$SelectedMosiGpio MISO=$SelectedMisoGpio"
Write-Output "UART(Serial2): RX=$SelectedUartRxGpio TX=$SelectedUartTxGpio"
Write-Output "Using user library root: $UserLibrariesDir"
Write-Output "Sketch path: $SketchPath"

function Install-M5StackCore {
    param([string]$Version)
    Write-Output "Checking Core m5stack:esp32@$Version..."
    $CoreList = arduino-cli core list | Out-String
    if ($CoreList -match "m5stack:esp32\s+$Version") {
        Write-Output "Core $Version already installed."
        return
    }

    Write-Output "Installing Core..."
    arduino-cli core update-index --additional-urls "$BoardManagerUrl"
    arduino-cli core install m5stack:esp32@$Version --additional-urls "$BoardManagerUrl"
}

function Install-Library {
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

function Update-UsbHostShieldLibrary {
    param([string]$LibDir)

    $avrPinsPath = Join-Path $LibDir "avrpins.h"
    $usbCorePath = Join-Path $LibDir "UsbCore.h"
    $usbHostPath = Join-Path $LibDir "usbhost.h"

    if (!(Test-Path $avrPinsPath)) {
        throw "avrpins.h not found: $avrPinsPath"
    }
    if (!(Test-Path $usbCorePath)) {
        throw "UsbCore.h not found: $usbCorePath"
    }
    if (!(Test-Path $usbHostPath)) {
        throw "usbhost.h not found: $usbHostPath"
    }

    $avrPinsText = Get-Content -Path $avrPinsPath -Raw
    # Older releases lack these aliases, required to choose Core/Core2 DIP pins.
    # Do not add them when the installed release already provides them.
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

    # USB Host Shield Library 2.0 PR #843 adds M5Stack CoreS3 support. Apply
    # only to releases that do not yet include it; the checks make this idempotent.
    $avrPinsText = Get-Content -Path $avrPinsPath -Raw
    if ($avrPinsText -notmatch "ARDUINO_M5STACK_CORES3") {
        $marker = "#elif defined(ARDUINO_XIAO_ESP32S3)"
        if (!$avrPinsText.Contains($marker)) {
            throw "Cannot apply CoreS3 pin patch: avrpins.h marker '$marker' was not found."
        }
        $coreS3Pins = @"
#elif defined(ARDUINO_M5STACK_CORES3)
// USB Host Shield Library 2.0 PR #843: M5Stack USB Module v1.2 on CoreS3.
// SS/INT CH2 uses GPIO1/GPIO14; SPI uses SCK=36, MOSI=37, MISO=35.
#ifdef pgm_read_word
#undef pgm_read_word
#endif
#ifdef pgm_read_dword
#undef pgm_read_dword
#endif
#ifdef pgm_read_float
#undef pgm_read_float
#endif
#ifdef pgm_read_ptr
#undef pgm_read_ptr
#endif
#define pgm_read_word(addr) ({ typeof(addr) _addr = (addr); *(const unsigned short *)(_addr); })
#define pgm_read_dword(addr) ({ typeof(addr) _addr = (addr); *(const unsigned long *)(_addr); })
#define pgm_read_float(addr) ({ typeof(addr) _addr = (addr); *(const float *)(_addr); })
#define pgm_read_ptr(addr) ({ typeof(addr) _addr = (addr); *(void * const *)(_addr); })
MAKE_PIN(P35, 35); // MISO
MAKE_PIN(P37, 37); // MOSI
MAKE_PIN(P36, 36); // SCK
MAKE_PIN(P1, 1);   // SS (USB Module CH2)
MAKE_PIN(P14, 14); // INT (USB Module CH2)

"@
        $avrPinsText = $avrPinsText.Replace($marker, $coreS3Pins + $marker)
        Set-Content -Path $avrPinsPath -Value $avrPinsText -Encoding UTF8
        Write-Output "Patched avrpins.h with the CoreS3 aliases required by USB Host Shield Library 2.0 PR #843."
    }
    elseif ($avrPinsText -notmatch "pgm_read_word\(addr\)") {
        # Upgrade an earlier local minimal patch to the complete PR #843 form.
        $needle = "// SS/INT CH2 uses GPIO1/GPIO14; SPI uses SCK=36, MOSI=37, MISO=35."
        if (!$avrPinsText.Contains($needle)) {
            throw "Cannot upgrade CoreS3 pin patch: avrpins.h does not contain the expected PR #843 marker."
        }
        $workaround = @"

#ifdef pgm_read_word
#undef pgm_read_word
#endif
#ifdef pgm_read_dword
#undef pgm_read_dword
#endif
#ifdef pgm_read_float
#undef pgm_read_float
#endif
#ifdef pgm_read_ptr
#undef pgm_read_ptr
#endif
#define pgm_read_word(addr) ({ typeof(addr) _addr = (addr); *(const unsigned short *)(_addr); })
#define pgm_read_dword(addr) ({ typeof(addr) _addr = (addr); *(const unsigned long *)(_addr); })
#define pgm_read_float(addr) ({ typeof(addr) _addr = (addr); *(const float *)(_addr); })
#define pgm_read_ptr(addr) ({ typeof(addr) _addr = (addr); *(void * const *)(_addr); })
"@
        $avrPinsText = $avrPinsText.Replace($needle, $needle + $workaround)
        Set-Content -Path $avrPinsPath -Value $avrPinsText -Encoding UTF8
        Write-Output "Upgraded avrpins.h to the complete CoreS3 workaround from USB Host Shield Library 2.0 PR #843."
    }

    $usbCoreText = Get-Content -Path $usbCorePath -Raw
    if ($usbCoreText -notmatch "ARDUINO_M5STACK_CORES3") {
        $needle = "#elif defined(ARDUINO_XIAO_ESP32S3)"
        if (!$usbCoreText.Contains($needle)) {
            throw "Cannot apply CoreS3 pin patch: UsbCore.h marker '$needle' was not found."
        }
        $usbCoreText = $usbCoreText.Replace($needle, "#elif defined(ARDUINO_M5STACK_CORES3)`r`ntypedef MAX3421e<P1, P14> MAX3421E; // USB Module v1.2: SS/INT CH2`r`n" + $needle)
        Set-Content -Path $usbCorePath -Value $usbCoreText -Encoding UTF8
        Write-Output "Patched UsbCore.h with the CoreS3 MAX3421E type required by USB Host Shield Library 2.0 PR #843."
    }

    $usbHostText = Get-Content -Path $usbHostPath -Raw
    if ($usbHostText -notmatch "ARDUINO_M5STACK_CORES3") {
        $needle = "#elif defined(ARDUINO_XIAO_ESP32S3)"
        if (!$usbHostText.Contains($needle)) {
            throw "Cannot apply CoreS3 pin patch: usbhost.h marker '$needle' was not found."
        }
        $usbHostText = $usbHostText.Replace($needle, "#elif defined(ARDUINO_M5STACK_CORES3)`r`ntypedef SPi< P36, P37, P35, P1 > spi; // USB Module v1.2 SPI pins`r`n" + $needle)
        Set-Content -Path $usbHostPath -Value $usbHostText -Encoding UTF8
        Write-Output "Patched usbhost.h with the CoreS3 SPI type required by USB Host Shield Library 2.0 PR #843."
    }
}

# 1. Core
Install-M5StackCore -Version $CoreVersion

# 2. Libraries (Documents/Arduino 配下を必須にする)
if (!(Test-Path $UserLibrariesDir)) {
    New-Item -ItemType Directory -Path $UserLibrariesDir | Out-Null
}

Install-Library -LibraryName "M5Unified" -ExpectedDir (Join-Path $UserLibrariesDir "M5Unified")
Install-Library -LibraryName "USB Host Shield Library 2.0" -ExpectedDir $UsbHostShieldDir
Update-UsbHostShieldLibrary -LibDir $UsbHostShieldDir

# 3. Compile
Write-Output "Compiling $SketchName..."
Write-Output "FQBN: $FQBN"
Write-Output "Sketch path: $SketchPath"

$SsType = "P$SelectedSsGpio"
$IntType = "P$SelectedIntGpio"

$ExtraFlags = @(
    "-DESP32",
    "-DUSB_HOST_SHIELD_SS_TYPE=$SsType",
    "-DUSB_HOST_SHIELD_INT_TYPE=$IntType",
    "-DPIN_SPI_SCK=$SelectedSckGpio",
    "-DPIN_SPI_MOSI=$SelectedMosiGpio",
    "-DPIN_SPI_MISO=$SelectedMisoGpio",
    "-DPIN_SPI_SS=$SelectedSsGpio",
    "-DUSB_MODULE_SS_CH=$SsChannel",
    "-DUSB_MODULE_INT_CH=$IntChannel",
    "-DUSB_HOST_SHIELD_SS_GPIO=$SelectedSsGpio",
    "-DUSB_HOST_SHIELD_INT_GPIO=$SelectedIntGpio",
    "-DSERIAL2_RX_PIN=$SelectedUartRxGpio",
    "-DSERIAL2_TX_PIN=$SelectedUartTxGpio"
)
if ($Board -eq "cores3se") {
    $ExtraFlags += "-DBUILD_TARGET_CORES3SE"
    # m5stack:esp32 3.2.5 uses a board macro spelling different from the
    # upstream PR #843 guard. Define the PR guard explicitly for both cores.
    $ExtraFlags += "-DARDUINO_M5STACK_CORES3"
}
$ExtraFlags = $ExtraFlags -join " "

Write-Output "Build flags: $ExtraFlags"

$SketchBase = [System.IO.Path]::GetFileNameWithoutExtension($SketchName)
$BuildSketchDir = Join-Path $PSScriptRoot "build-temp" | Join-Path -ChildPath $SketchBase
if (Test-Path $BuildSketchDir) {
    Remove-Item -Path $BuildSketchDir -Recurse -Force
}
New-Item -ItemType Directory -Path $BuildSketchDir | Out-Null
Copy-Item -Path $SketchPath -Destination (Join-Path $BuildSketchDir $SketchName) -Force

Write-Output "Build sketch folder: $BuildSketchDir"

$CompileArgs = @(
    "compile",
    "--fqbn", $FQBN,
    "--libraries", $UserLibrariesDir,
    "--build-property", "build.extra_flags=$ExtraFlags",
    (Join-Path $BuildSketchDir $SketchName)
)
if ($ExportBinaries) {
    $CompileArgs += "--export-binaries"
    Write-Output "ExportBinaries: enabled"
}

& arduino-cli @CompileArgs

if ($LASTEXITCODE -ne 0) {
    Write-Output "Build failed!"
    exit 1
}
Write-Output "Build successful!"

# 4. Export binaries (--export-binaries)
if ($ExportBinaries) {
    $FqbnDir = $FQBN -replace ":", "."
    $BuildOutputDir = Join-Path $PSScriptRoot "build" $FqbnDir
    Write-Output ""
    Write-Output "--- Export Binaries ---"
    Write-Output "Output directory: $BuildOutputDir"
    $ExportName = "SwitchSender_${Board}_SS-CH${SsChannel}_INT-CH${IntChannel}.bin"
    # Arduino CLI retains the .ino suffix: <Sketch>.ino.bin. Restrict lookup
    # to this FQBN output directory so a stale binary from another board is not used.
    $SketchBin = Join-Path $BuildOutputDir "$SketchName.bin"
    if (!(Test-Path $SketchBin)) {
        throw "Exported sketch binary was not found: $SketchBin"
    }
    $NamedExport = Join-Path $PSScriptRoot "build" $ExportName
    Copy-Item -LiteralPath $SketchBin -Destination $NamedExport -Force
    Write-Output "Named export: $NamedExport"
    $BinFiles = Get-ChildItem -Path $BuildOutputDir -Filter "*.bin" -ErrorAction SilentlyContinue
    if ($BinFiles) {
        foreach ($f in $BinFiles) {
            Write-Output "  $($f.FullName)"
        }
    }
    Write-Output "ExportBinaries: done. Skipping upload."
    exit 0
}

# 5. Upload
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
    arduino-cli upload -p $Port --fqbn $FQBN $BuildSketchDir

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
