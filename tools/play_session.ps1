<#
.SYNOPSIS
Plays Prey once with PreySpeedBuff installed for this session only.

.DESCRIPTION
Installs the built PreySpeedBuff.asi and an INI made from the parameters into
the game folder, starts prey.exe, and waits until the game exits. Afterwards it
restores the game folder exactly as it was: the plugin files are removed (or
earlier ones put back), PreyHFR is re-enabled if it was switched off, and a
key bound for this session is returned to its previous binding.

What the session changed is recorded in research\runs\active-session.json
before anything is touched. If a session is killed before it can clean up, the
next session (or -RestoreOnly) undoes those changes first.

Evidence goes to research\runs\<time>-<name>\: the plugin log, the INI used,
PreyHFR.log, and session.log.

Requires Windows PowerShell 5.1. Steam must be running for the Steam build.
#>
[CmdletBinding()]
param(
    [string]$Walk = '270',
    [string]$Run = '330',
    [string]$Crouch = '120',
    # Installs the plugin with enabled = false, for a retail comparison run.
    [switch]$Retail,
    # Renames PreyHFR.asi for this session so the game runs without it.
    [switch]$WithoutHfr,
    # A key bound to _speed (run) for this session; empty for none.
    [string]$RunKey = '',
    # Starts the game with com_allowConsole 1 so ~ opens the console.
    [switch]$Console,
    [ValidateRange(1, 1440)]
    [int]$Minutes = 60,
    [string]$Name = 'play',
    [string]$GameDir = 'D:\Program Files\Games\Steam\steamapps\common\Prey 2006',
    [string]$Plugin = '',
    # Checks everything and prints the plan without changing anything.
    [switch]$DryRun,
    # Only undoes an interrupted session.
    [switch]$RestoreOnly,
    # For tests of this script against a fake game folder.
    [switch]$NoSteamCheck
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'

$Repo = Split-Path -Parent $PSScriptRoot
$RunsDir = Join-Path $Repo 'research\runs'
$StateFile = Join-Path $RunsDir 'active-session.json'
if (-not $Plugin) { $Plugin = Join-Path $Repo 'build-x86\bin\PreySpeedBuff.asi' }
$RetailGameDllSha256 = '74D436D376BA144762A28C940D0243135B4F9DB8FDD7EE597B9CB5E4277B43C6'
$PinnedLoaderSha256 = 'EC2F4824ECA58DD40F425756A4A7CEC77B8E381F21D11D7C846EC4B339B617AB'
$Latin1 = [Text.Encoding]::GetEncoding(28591)
$SessionLog = $null

function Say([string]$Text) {
    $line = '{0:HH:mm:ss} {1}' -f (Get-Date), $Text
    # Straight to stdout, so functions that return values stay clean.
    [Console]::Out.WriteLine($line)
    if ($SessionLog) { Add-Content -LiteralPath $SessionLog -Value $line -Encoding UTF8 }
}

function Get-GameProcesses([string]$Dir) {
    $prefix = (Join-Path $Dir 'prey.exe').ToLowerInvariant()
    # Callers wrap the result in @() so one process still has a .Count.
    @(Get-CimInstance Win32_Process -Filter "Name = 'prey.exe'" |
        Where-Object { $_.ExecutablePath -and $_.ExecutablePath.ToLowerInvariant() -eq $prefix })
}

function Test-SpeedValue([string]$Label, [string]$Value) {
    if ($Value -ieq 'game') { return }
    $number = 0.0
    if (-not [double]::TryParse($Value, [Globalization.NumberStyles]::Float,
            [Globalization.CultureInfo]::InvariantCulture, [ref]$number) -or
        $number -lt 0 -or $number -gt 100000) {
        throw "$Label must be 'game' or a number from 0 to 100000, not '$Value'"
    }
}

# Reads a text file keeping its exact line endings and bytes.
function Read-Lines([string]$Path) {
    $text = [IO.File]::ReadAllText($Path, $Latin1)
    $newline = if ($text.Contains("`r`n")) { "`r`n" } else { "`n" }
    $lines = New-Object System.Collections.Generic.List[string]
    foreach ($line in ($text -split "`r?`n")) { $lines.Add($line) }
    [pscustomobject]@{ Lines = $lines; NewLine = $newline }
}

function Write-Lines([string]$Path, $File) {
    [IO.File]::WriteAllText($Path, ($File.Lines -join $File.NewLine), $Latin1)
}

function Get-BindPattern([string]$Key) {
    '^\s*bind\s+"?' + [regex]::Escape($Key) + '"?\s'
}

# Puts the key's bind line back to what it was before the session: the game
# rewrites its config on exit, so the first bind line for the key takes the
# original's place and any others are dropped.
function Restore-Bind([string]$ConfigPath, [string]$Key, $OriginalLine) {
    if (-not (Test-Path -LiteralPath $ConfigPath)) { return }
    $file = Read-Lines $ConfigPath
    $pattern = Get-BindPattern $Key
    $restored = New-Object System.Collections.Generic.List[string]
    $matched = $false
    foreach ($line in $file.Lines) {
        if ($line -imatch $pattern) {
            if (-not $matched -and $OriginalLine -is [string] -and $OriginalLine) { $restored.Add($OriginalLine) }
            $matched = $true
            continue
        }
        $restored.Add($line)
    }
    if (-not $matched) { return }
    Write-Lines $ConfigPath ([pscustomobject]@{ Lines = $restored; NewLine = $file.NewLine })
    Say "restored the binding of $Key in preyconfig.cfg"
}

# Undoes what the state file says this script changed.
function Restore-Session {
    if (-not (Test-Path -LiteralPath $StateFile)) { return $false }
    $state = Get-Content -LiteralPath $StateFile -Raw | ConvertFrom-Json
    $game = $state.gameDir
    if (@(Get-GameProcesses $game).Count -gt 0) {
        throw "Prey is still running; quit it before the session can be restored"
    }
    Say "restoring the game folder ($game)"

    $sessionDir = $state.sessionDir
    if ($sessionDir -and (Test-Path -LiteralPath $sessionDir)) {
        foreach ($log in 'PreySpeedBuff.log', 'PreyHFR.log') {
            $source = Join-Path $game $log
            if (Test-Path -LiteralPath $source) {
                Copy-Item -LiteralPath $source -Destination $sessionDir -Force
            }
        }
    }

    $created = @('PreySpeedBuff.asi', 'PreySpeedBuff.ini', 'PreySpeedBuff.log')
    if ($state.PSObject.Properties['autoexecWritten'] -and $state.autoexecWritten) {
        $created += 'base\autoexec.cfg'
    }
    foreach ($file in $created) {
        $path = Join-Path $game $file
        if (Test-Path -LiteralPath $path) { Remove-Item -LiteralPath $path -Force }
    }
    foreach ($backup in @($state.backups)) {
        if ($backup -and (Test-Path -LiteralPath $backup.saved)) {
            Copy-Item -LiteralPath $backup.saved -Destination $backup.original -Force
            Say "put back the earlier $(Split-Path -Leaf $backup.original)"
        }
    }
    if ($state.hfrDisabled) {
        $off = Join-Path $game 'PreyHFR.asi.speedbuff-off'
        $on = Join-Path $game 'PreyHFR.asi'
        if ((Test-Path -LiteralPath $off) -and -not (Test-Path -LiteralPath $on)) {
            Rename-Item -LiteralPath $off -NewName 'PreyHFR.asi'
            Say 're-enabled PreyHFR.asi'
        }
    }
    if ($state.runKey) {
        Restore-Bind (Join-Path $game 'base\preyconfig.cfg') $state.runKey $state.originalBind
    }
    Remove-Item -LiteralPath $StateFile -Force
    Say 'game folder restored'
    return $true
}

function Write-Summary([string]$PluginLog) {
    if (-not (Test-Path -LiteralPath $PluginLog)) {
        Say 'RESULT: no PreySpeedBuff.log; the ASI loader never loaded the plugin'
        return 2
    }
    $lines = Get-Content -LiteralPath $PluginLog
    Say '--- PreySpeedBuff.log ---'
    foreach ($line in $lines) { Say "  $line" }
    $text = $lines -join "`n"
    if ($text -match 'error:') {
        Say 'RESULT: the plugin reported an error (see above); speed was retail'
        return 3
    }
    if ($Retail) {
        Say 'RESULT: retail comparison run; the plugin stayed inactive as intended'
        return 0
    }
    if ($text -notmatch 'hook installed') {
        Say 'RESULT: the hook was never installed (gamex86.dll not seen)'
        return 4
    }
    if ($text -notmatch 'first override applied') {
        Say 'RESULT: hook installed, but no map was played, so the speed never applied'
        return 0
    }
    Say 'RESULT: speed override was active in-game'
    return 0
}

if (-not (Test-Path -LiteralPath $RunsDir)) {
    New-Item -ItemType Directory -Path $RunsDir | Out-Null
}

if ($RestoreOnly) {
    if (-not (Restore-Session)) { Say 'nothing to restore' }
    exit 0
}

# --- checks: nothing is changed until all pass -------------------------------
Test-SpeedValue 'walk' $Walk
Test-SpeedValue 'run' $Run
Test-SpeedValue 'crouch' $Crouch
if ($RunKey -and $RunKey -notmatch '^[A-Za-z0-9]+$') {
    throw "RunKey must be a single Doom 3 key name such as CTRL or r, not '$RunKey'"
}
$exe = Join-Path $GameDir 'prey.exe'
if (-not (Test-Path -LiteralPath $exe)) { throw "prey.exe not found in $GameDir" }
if (-not (Test-Path -LiteralPath $Plugin)) {
    throw "the plugin is not built: $Plugin (build the project first)"
}
$loader = Join-Path $GameDir 'dinput.dll'
if (-not (Test-Path -LiteralPath $loader)) {
    throw "no ASI loader: $loader is missing (install the with-loader package first)"
}
if (-not $DryRun -and (Test-Path -LiteralPath $StateFile)) {
    Say 'an earlier session did not finish; undoing its changes first'
    [void](Restore-Session)
}
if (@(Get-GameProcesses $GameDir).Count -gt 0) { throw 'Prey is already running; quit it first' }
if (-not $NoSteamCheck -and -not (Get-Process -Name steam -ErrorAction SilentlyContinue)) {
    throw 'Steam is not running; start Steam, then run this again'
}

$gameDll = Join-Path $GameDir 'base\gamex86.dll'
$dllHash = if (Test-Path -LiteralPath $gameDll) { (Get-FileHash -LiteralPath $gameDll).Hash } else { 'missing' }
$loaderHash = (Get-FileHash -LiteralPath $loader).Hash
$hfr = Join-Path $GameDir 'PreyHFR.asi'
$hfrPresent = Test-Path -LiteralPath $hfr

$enabled = if ($Retail) { 'false' } else { 'true' }
$ini = @(
    '; Written by tools\play_session.ps1 for one session.',
    '[speed]',
    "enabled = $enabled",
    "walk = $Walk",
    "run = $Run",
    "crouch = $Crouch",
    ''
) -join "`r`n"

# Only set commands go on the command line: any other startup command, such as
# +bind, makes the engine skip the main menu and leaves just the console. The
# run key is bound from base\autoexec.cfg, which the game runs after its config.
$arguments = New-Object System.Collections.Generic.List[string]
if ($Console) { $arguments.AddRange([string[]]@('+set', 'com_allowConsole', '1')) }
$autoexec = Join-Path $GameDir 'base\autoexec.cfg'

$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$safeName = ($Name -replace '[^A-Za-z0-9_.-]', '-')
$sessionDir = Join-Path $RunsDir "$stamp-$safeName"

Write-Output "game:        $GameDir"
Write-Output "gamex86.dll: $dllHash$(if ($dllHash -ne $RetailGameDllSha256) { '  (not the tested retail build; the plugin will check it)' })"
Write-Output "loader:      dinput.dll$(if ($loaderHash -ne $PinnedLoaderSha256) { ' (not the pinned Ultimate ASI Loader 9.7.4)' } else { ' (pinned Ultimate ASI Loader 9.7.4)' })"
Write-Output "plugin:      $Plugin ($((Get-Item -LiteralPath $Plugin).LastWriteTime))"
Write-Output "PreyHFR:     $(if (-not $hfrPresent) { 'not installed' } elseif ($WithoutHfr) { 'switched off for this session' } else { 'kept' })"
Write-Output "speeds:      walk=$Walk run=$Run crouch=$Crouch enabled=$enabled"
Write-Output "arguments:   $(if ($arguments.Count) { $arguments -join ' ' } else { '(none)' })"
Write-Output "run key:     $(if ($RunKey) { "$RunKey, bound from base\autoexec.cfg for this session" } else { '(none)' })"
Write-Output "limit:       $Minutes min"
Write-Output "evidence:    $sessionDir"
if ($DryRun) {
    Write-Output 'dry run: nothing was changed'
    exit 0
}

# --- record, then change ------------------------------------------------------
New-Item -ItemType Directory -Path $sessionDir | Out-Null
$SessionLog = Join-Path $sessionDir 'session.log'
Set-Content -LiteralPath (Join-Path $sessionDir 'PreySpeedBuff.ini') -Value $ini -Encoding ASCII -NoNewline

$config = Join-Path $GameDir 'base\preyconfig.cfg'
$originalBind = $null
if ($RunKey -and (Test-Path -LiteralPath $config)) {
    $pattern = Get-BindPattern $RunKey
    $found = @((Read-Lines $config).Lines | Where-Object { $_ -imatch $pattern })
    # A real $null: an empty pipeline result would be saved as {} in the JSON.
    if ($found.Count -gt 0) { $originalBind = [string]$found[0] }
}

$backups = @()
$owned = @('PreySpeedBuff.asi', 'PreySpeedBuff.ini')
if ($RunKey) { $owned += 'base\autoexec.cfg' }
foreach ($file in $owned) {
    $path = Join-Path $GameDir $file
    if (Test-Path -LiteralPath $path) {
        $saved = Join-Path $sessionDir "previous-$(Split-Path -Leaf $file)"
        Copy-Item -LiteralPath $path -Destination $saved
        $backups += @{ original = $path; saved = $saved }
    }
}
$state = @{
    gameDir = $GameDir
    sessionDir = $sessionDir
    backups = $backups
    hfrDisabled = [bool]($WithoutHfr -and $hfrPresent)
    runKey = $RunKey
    originalBind = $originalBind
    autoexecWritten = [bool]$RunKey
}
$state | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $StateFile -Encoding UTF8

$exitCode = 1
try {
    Copy-Item -LiteralPath $Plugin -Destination (Join-Path $GameDir 'PreySpeedBuff.asi') -Force
    Set-Content -LiteralPath (Join-Path $GameDir 'PreySpeedBuff.ini') -Value $ini -Encoding ASCII -NoNewline
    $oldLog = Join-Path $GameDir 'PreySpeedBuff.log'
    if (Test-Path -LiteralPath $oldLog) { Remove-Item -LiteralPath $oldLog -Force }
    Say "installed PreySpeedBuff for this session: walk=$Walk run=$Run crouch=$Crouch enabled=$enabled"
    if ($RunKey) {
        # Keeps an existing autoexec.cfg's commands and adds the binding last.
        $commands = if (Test-Path -LiteralPath $autoexec) {
            [IO.File]::ReadAllText($autoexec, $Latin1).TrimEnd() + "`r`n"
        } else { '' }
        [IO.File]::WriteAllText($autoexec, $commands + "bind `"$RunKey`" `"_speed`"`r`n", $Latin1)
        Say "bound $RunKey to run in base\autoexec.cfg for this session"
    }
    if ($state.hfrDisabled) {
        Rename-Item -LiteralPath $hfr -NewName 'PreyHFR.asi.speedbuff-off'
        Say 'switched PreyHFR off for this session'
    }

    $startArgs = @{ FilePath = $exe; WorkingDirectory = $GameDir; PassThru = $true }
    if ($arguments.Count) { $startArgs.ArgumentList = $arguments.ToArray() }
    $process = Start-Process @startArgs
    Say "started prey.exe (pid $($process.Id)) $($arguments -join ' ')"

    # The protected executable may hand over to another prey.exe, so wait for
    # every prey.exe from this folder, not only the one started here.
    $deadline = (Get-Date).AddMinutes($Minutes)
    $quietSince = $null
    while ($true) {
        Start-Sleep -Seconds 1
        $running = @(Get-GameProcesses $GameDir)
        if ($running.Count -gt 0) {
            $quietSince = $null
            if ((Get-Date) -gt $deadline) {
                Say "session limit of $Minutes min reached; closing the game"
                foreach ($p in $running) { Stop-Process -Id $p.ProcessId -Force -ErrorAction SilentlyContinue }
            }
            continue
        }
        if ($null -eq $quietSince) { $quietSince = Get-Date }
        if (((Get-Date) - $quietSince).TotalSeconds -ge 5) { break }
    }
    $process.Refresh()
    Say "the game exited (exit code $(if ($process.HasExited) { $process.ExitCode } else { 'unknown' }))"
    $exitCode = 0
} finally {
    [void](Restore-Session)
}

$summary = Write-Summary (Join-Path $sessionDir 'PreySpeedBuff.log')
Say "evidence: $sessionDir"
if ($exitCode -ne 0) { exit $exitCode }
exit $summary
