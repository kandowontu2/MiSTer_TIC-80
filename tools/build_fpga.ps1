param([string]$QuartusBin = "$env:USERPROFILE/intelFPGA_lite/17.0/quartus/bin64", [switch]$EnableYC, [string]$BuildDirectory = '')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$compiler = Join-Path $QuartusBin 'quartus_sh.exe'
if (-not (Test-Path -LiteralPath $compiler -PathType Leaf)) {
    throw 'Quartus executable missing; specify -QuartusBin with the installation directory.'
}
& $compiler --version
if ($LASTEXITCODE -ne 0) { throw 'Unable to start Quartus.' }
if (-not $BuildDirectory) { $BuildDirectory = Join-Path $projectRoot 'build' }
$BuildDirectory = [System.IO.Path]::GetFullPath($BuildDirectory)
$stage = Join-Path $BuildDirectory 'fpga'
$log = Join-Path $BuildDirectory 'quartus-build.log'
if (Test-Path -LiteralPath $log) { throw 'Build evidence already exists; choose a fresh -BuildDirectory.' }
$nativeRunner = Join-Path $PSScriptRoot 'run_quartus.py'
& python (Join-Path $PSScriptRoot 'prepare_fpga.py') --output $stage
if ($LASTEXITCODE -ne 0) { throw 'Project staging failed.' }
$temporaryFiles = Join-Path $BuildDirectory 'tmp/quartus'
New-Item -ItemType Directory -Path $temporaryFiles -Force | Out-Null
$previousTemp = $env:TEMP
$previousTmp = $env:TMP
Push-Location $stage
$previousYC = $env:TM_ENABLE_YC
$env:TM_ENABLE_YC = if ($EnableYC) { '1' } else { '0' }
try {
    $env:TEMP = $temporaryFiles
    $env:TMP = $temporaryFiles
    & python $nativeRunner --log $log -- $compiler --flow compile TIC80.qpf
    if ($LASTEXITCODE -ne 0) { throw "Quartus compilation failed; inspect $log" }
    & python (Join-Path $PSScriptRoot 'check_timing.py') 'output_files/TIC80.sta.summary'
    if ($LASTEXITCODE -ne 0) { throw "Bitstream has timing violations; inspect $log" }
    $cdcLog = Join-Path $BuildDirectory 'audio-cdc-audit.log'
    & python $nativeRunner --log $cdcLog -- (Join-Path $QuartusBin 'quartus_sta.exe') -t (Join-Path $PSScriptRoot 'audit_audio_cdc.tcl')
    if ($LASTEXITCODE -ne 0) { throw "Audio/video CDC routing check failed; inspect $cdcLog" }
    $videoClockLog = Join-Path $BuildDirectory 'video-clock-audit.log'
    & python $nativeRunner --log $videoClockLog -- (Join-Path $QuartusBin 'quartus_sta.exe') -t (Join-Path $PSScriptRoot 'audit_video_clock.tcl')
    if ($LASTEXITCODE -ne 0) { throw "Shared video/playback clock check failed; inspect $videoClockLog" }
    $resetLog = Join-Path $BuildDirectory 'ddr-reset-postfit-audit.log'
    & python $nativeRunner --log $resetLog -- (Join-Path $QuartusBin 'quartus_sta.exe') -t (Join-Path $PSScriptRoot 'audit_ddr_reset.tcl')
    if ($LASTEXITCODE -ne 0) { throw "Fitted DDR reset handoff check failed; inspect $resetLog" }
    $sharedResetLog = Join-Path $BuildDirectory 'shared-reset-postfit-audit.log'
    & python $nativeRunner --log $sharedResetLog -- (Join-Path $QuartusBin 'quartus_sta.exe') -t (Join-Path $PSScriptRoot 'audit_shared_reset.tcl')
    if ($LASTEXITCODE -ne 0) { throw "Fitted shared reset routing check failed; inspect $sharedResetLog" }
    $syncLog = Join-Path $BuildDirectory 'core-synchronizers-postfit-audit.log'
    if (Test-Path -LiteralPath $syncLog) { throw 'Synchronizer audit evidence already exists.' }
    foreach ($syncCorner in @(@{Model='slow'; Temperature='-40'}, @{Model='slow'; Temperature='100'}, @{Model='fast'; Temperature='-40'}, @{Model='fast'; Temperature='100'})) {
        & python $nativeRunner --log $syncLog --append -- (Join-Path $QuartusBin 'quartus_sta.exe') -t (Join-Path $PSScriptRoot 'audit_core_synchronizers.tcl') $BuildDirectory $syncCorner.Model $syncCorner.Temperature
        if ($LASTEXITCODE -ne 0) { throw "Fitted synchronizer topology/timing check failed; inspect $syncLog" }
    }
    & python $nativeRunner --log $syncLog --append -- python (Join-Path $PSScriptRoot 'check_core_synchronizers.py') $BuildDirectory
    if ($LASTEXITCODE -ne 0) { throw "Custom synchronizer MTBF check failed; inspect $syncLog" }
    if (Test-Path -LiteralPath (Join-Path $stage 'sys/audio_out.v')) {
        $audioResetSource = Get-Content -LiteralPath (Join-Path $stage 'sys/audio_out.v') -Raw
        if ($audioResetSource.Contains('module tic80_audio_reset')) {
            $audioResetLog = Join-Path $BuildDirectory 'platform-audio-reset-postfit-audit.log'
            & python $nativeRunner --log $audioResetLog -- (Join-Path $QuartusBin 'quartus_sta.exe') -t (Join-Path $PSScriptRoot 'audit_platform_audio_reset.tcl')
            if ($LASTEXITCODE -ne 0) { throw "Fitted audio reset release check failed; inspect $audioResetLog" }
        }
    }
    $recordArgs = @('--stage', $stage, '--evidence-directory', $BuildDirectory, '--output', (Join-Path $BuildDirectory 'fpga-build.json'))
    if ($EnableYC) { $recordArgs += '--enable-yc' }
    & python (Join-Path $PSScriptRoot 'record_fpga_build.py') @recordArgs
    if ($LASTEXITCODE -ne 0) { throw 'FPGA artifact/source provenance check failed' }
    Write-Output "Development RBF: $(Join-Path $stage 'output_files/TIC80.rbf')"
} finally {
    $env:TEMP = $previousTemp
    $env:TMP = $previousTmp
    $env:TM_ENABLE_YC = $previousYC
    Pop-Location
}
