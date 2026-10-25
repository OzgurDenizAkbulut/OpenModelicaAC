[CmdletBinding()]
param(
  [Parameter(Mandatory=$true)][string]$Install,
  [Parameter(Mandatory=$true)][string]$Output,
  [ValidatePattern('^[0-9a-fA-F]{10,40}$')][string]$ExpectedRevision,
  [string]$ModelicaLibraryPath,
  [string]$OriginalRoaming,
  [string]$OriginalUserProfile,
  [ValidateRange(5,60)][int]$TimeoutSeconds = 30
)
$ErrorActionPreference = 'Stop'
if (-not $ExpectedRevision) {
  $ExpectedRevision = & git -C (Join-Path $PSScriptRoot '../..') rev-parse HEAD
  if ($LASTEXITCODE -ne 0) { throw 'Cannot determine the source revision; specify -ExpectedRevision explicitly.' }
  $ExpectedRevision = $ExpectedRevision.Trim()
}
Add-Type @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class StartupWindowTitle {
  private delegate bool WindowCallback(IntPtr window, IntPtr data);
  [DllImport("user32.dll")] private static extern bool EnumWindows(WindowCallback callback, IntPtr data);
  [DllImport("user32.dll")] private static extern uint GetWindowThreadProcessId(IntPtr window, out uint processId);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] private static extern int GetWindowText(IntPtr window, StringBuilder text, int count);
  public static string Find(uint processId) {
    string result = "";
    EnumWindows((window, data) => {
      uint owner;
      GetWindowThreadProcessId(window, out owner);
      if (owner == processId) {
        var text = new StringBuilder(1024);
        GetWindowText(window, text, text.Capacity);
        if (text.ToString().Contains("Development")) { result = text.ToString(); return false; }
      }
      return true;
    }, IntPtr.Zero);
    return result;
  }
}
'@
$candidate = (Resolve-Path -LiteralPath $Install).Path
$manifest = Get-Content -LiteralPath (Join-Path (Split-Path $candidate -Parent) 'installed-development.json') -Raw | ConvertFrom-Json
if ($manifest.head -notmatch '^[0-9a-fA-F]{40}$' -or -not $manifest.head.StartsWith($ExpectedRevision, [StringComparison]::OrdinalIgnoreCase)) {
  throw 'Successful install manifest revision does not match the requested baseline.'
}
$nativeRoaming = [Environment]::GetFolderPath('ApplicationData')
$nativeSystem = [Environment]::GetFolderPath('CommonApplicationData')
$configuration = Get-Content -LiteralPath (Join-Path (Split-Path $candidate -Parent) 'OMEdit/omedit_config.h') -Raw
$organization = [regex]::Match($configuration, '#define OMEDIT_SETTINGS_ORGANIZATION "([^"\\]+)"').Groups[1].Value
$application = [regex]::Match($configuration, '#define OMEDIT_SETTINGS_APPLICATION "([^"\\]+)"').Groups[1].Value
if (-not $organization -or -not $application -or $organization -match '[\\/]' -or $application -match '[\\/]' -or
    $organization -in @('.', '..', 'openmodelica') -or $application -in @('.', '..', 'omedit')) {
  throw 'Cannot safely determine a separate development preference identity from the generated config.'
}
$preferencePaths = @()
$preferenceRoots = @(@($nativeRoaming, $nativeSystem, $OriginalRoaming) | Where-Object { $_ } | Select-Object -Unique)
foreach ($identity in @(@{org='openmodelica';app='omedit';official=$true}, @{org=$organization;app=$application;official=$false})) {
  foreach ($root in $preferenceRoots) {
    $preferencePaths += @{path=(Join-Path $root "$($identity.org)/$($identity.app).ini");official=$identity.official;primary=($root -in @($nativeRoaming, $OriginalRoaming) -and -not $identity.official)}
    $preferencePaths += @{path=(Join-Path $root "$($identity.org).ini");official=$identity.official;primary=$false}
  }
}
function Get-PreferenceSnapshot {
  @($preferencePaths | ForEach-Object {
    $exists = Test-Path -LiteralPath $_.path -PathType Leaf
    @{path=$_.path;official=$_.official;primary=$_.primary;existed=$exists;sha256=$(if ($exists) { (Get-FileHash -LiteralPath $_.path -Algorithm SHA256).Hash } else { $null })}
  })
}
if (Test-Path -LiteralPath $Output) { throw 'Startup probe requires a fresh output directory.' }
$evidence = (New-Item -ItemType Directory -Path $Output).FullName
foreach ($key in @('HOME', 'USERPROFILE', 'APPDATA', 'LOCALAPPDATA')) {
  $directory = New-Item -ItemType Directory -Path (Join-Path $evidence "environment/$key")
  [Environment]::SetEnvironmentVariable($key, $directory.FullName, 'Process')
}
$privateTemp = (New-Item -ItemType Directory -Path (Join-Path $evidence 'environment/temp')).FullName
$env:TEMP = $privateTemp
$env:TMP = $privateTemp
$additionalRoots = @($env:APPDATA, (Join-Path $env:USERPROFILE 'AppData/Roaming'))
if ($OriginalUserProfile) { $additionalRoots += Join-Path $OriginalUserProfile 'AppData/Roaming' }
foreach ($root in @($additionalRoots | Select-Object -Unique)) {
  if ($root -notin $preferenceRoots) {
    foreach ($identity in @(@{org='openmodelica';app='omedit';official=$true}, @{org=$organization;app=$application;official=$false})) {
      $preferencePaths += @{path=(Join-Path $root "$($identity.org)/$($identity.app).ini");official=$identity.official;primary=(-not $identity.official)}
      $preferencePaths += @{path=(Join-Path $root "$($identity.org).ini");official=$identity.official;primary=$false}
    }
  }
}
$report = @{ status='failed'; install=$candidate; modules=@(); enumeration_errors=@(); owned_processes=@() }
$report.expected_revision = $ExpectedRevision
$report.manifest_revision = $manifest.head
$report.settings_mode = 'Native Windows Qt known-folder settings; original roaming and probe candidate roots audited. Environment-only isolation is not assumed. Existing development application settings abort the probe; first-launch defaults may be created.'
$report.original_roaming = $OriginalRoaming
$report.original_userprofile = $OriginalUserProfile
$report.preferences_before = Get-PreferenceSnapshot
$ownedProcess = $null
$observed = @{}
$timer = [Diagnostics.Stopwatch]::StartNew()
try {
  if (@($report.preferences_before | Where-Object { $_.primary -and $_.existed }).Count) {
    throw 'Existing development preferences detected; automatic normal startup aborted to preserve them.'
  }
  $ownedProcess = & "$PSScriptRoot/launch-development.ps1" -Install $candidate -ExpectedRevision $manifest.head -ModelicaLibraryPath $ModelicaLibraryPath -PassThru -WindowStyle Hidden
  if (-not ($ownedProcess -is [Diagnostics.Process])) { throw 'Launcher did not return its owned OMEdit process.' }
  $report.process_id = $ownedProcess.Id
  $readyAt = $null
  while ($timer.Elapsed.TotalSeconds -lt $TimeoutSeconds) {
    $ownedProcess.Refresh()
    if ($ownedProcess.HasExited) { throw "Installed OMEdit exited during startup: $($ownedProcess.ExitCode)" }
    try {
      foreach ($module in $ownedProcess.Modules) { $observed[$module.FileName] = $true }
    } catch { $report.enumeration_errors += $_.Exception.Message }
    $windowTitle = [StartupWindowTitle]::Find($ownedProcess.Id)
    if ($windowTitle) {
      if ($null -eq $readyAt) { $readyAt = $timer.Elapsed.TotalSeconds }
      if ($timer.Elapsed.TotalSeconds - $readyAt -ge 2) { break }
    }
    Start-Sleep -Milliseconds 200
  }
  if ($null -eq $readyAt) { throw 'Installed OMEdit did not create its development main window before the deadline.' }
  $report.main_window_title = $windowTitle
  $report.modules = @($observed.Keys | Sort-Object)
  foreach ($module in $report.modules) {
    if ($module -match '1\.27') { throw "Legacy module loaded: $module" }
    if ([IO.Path]::GetFileName($module) -match '^(lib)?(OpenModelicaCompiler|OpenModelicaRuntime.*|SimulationRuntimeC|omcgc|omc_result|omcresult|Qt6Core|qwindows)\.dll$') {
      if (-not $module.StartsWith($candidate + '\', [StringComparison]::OrdinalIgnoreCase)) {
        throw "Compiler/Qt module loaded outside candidate: $module"
      }
    }
  }
  foreach ($name in @('libOpenModelicaCompiler.dll', 'Qt6Core.dll', 'qwindows.dll')) {
    if (-not @($report.modules | Where-Object { [IO.Path]::GetFileName($_) -eq $name }).Count) {
      throw "Missing positive normal-startup module observation: $name"
    }
  }
  $communication = Join-Path $env:TEMP "OpenModelica/OMEdit/$application/omeditcommunication.log"
  if (-not (Test-Path -LiteralPath $communication -PathType Leaf)) { throw 'Normal startup communication log was not created.' }
  $report.communication_log = $communication
  if (-not (Select-String -LiteralPath $communication -SimpleMatch ($manifest.head.Substring(0, 10)) -Quiet)) {
    throw 'Startup communication log does not yet identify the pinned compiler revision.'
  }
  $startupErrors = @(Select-String -LiteralPath $communication -Pattern 'Unknown debug flag|undefined symbol|procedure entry point|Could not load|Failed to load builtin|\bError:\s|ErrorLevel\.(error|internal)\b' | ForEach-Object Line)
  if ($startupErrors.Count) { $report.startup_errors = $startupErrors; throw 'Normal startup communication log contains errors.' }
  $report.status = 'passed'
} catch {
  $report.error = $_.Exception.Message
} finally {
  if ($null -ne $ownedProcess) {
    $ownedIds = @($ownedProcess.Id)
    try {
      $processes = @(Get-CimInstance Win32_Process -OperationTimeoutSec 5)
      do {
        $children = @($processes | Where-Object { $_.ParentProcessId -in $ownedIds -and $_.ProcessId -notin $ownedIds })
        $ownedIds += @($children | ForEach-Object { [int]$_.ProcessId })
      } while ($children.Count)
    } catch { $report.cleanup_inventory_error = $_.Exception.Message }
    $ownedHandles = @($ownedIds | ForEach-Object { Get-Process -Id $_ -ErrorAction SilentlyContinue })
    $report.owned_processes = @($ownedHandles | ForEach-Object { @{id=$_.Id;name=$_.ProcessName} })
    try {
      $killer = Start-Process -FilePath "$env:SystemRoot/System32/taskkill.exe" -ArgumentList @('/PID', $ownedProcess.Id, '/T', '/F') `
        -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $evidence 'cleanup.log') `
        -RedirectStandardError (Join-Path $evidence 'cleanup-error.log')
      if (-not $killer.WaitForExit(15000)) { $killer.Kill(); $killer.WaitForExit(5000) | Out-Null; $report.cleanup_command_timeout = $true }
      $report.tree_cleanup_returncode = $killer.ExitCode
    } catch {
      $report.cleanup_command_error = $_.Exception.Message
      $report.status = 'failed'
    }
    $report.cleanup_verified = $true
    foreach ($process in $ownedHandles) {
      try {
        if (-not $process.WaitForExit(5000)) { $process.Kill(); $process.WaitForExit(5000) | Out-Null }
        if (-not $process.HasExited) { throw 'Owned startup process survived cleanup.' }
        ($report.owned_processes | Where-Object { $_.id -eq $process.Id }).exited = $true
      } catch {
        $report.status = 'failed'
        $report.cleanup_verified = $false
        $report.cleanup_error = $_.Exception.Message
      }
    }
  }
  $report.elapsed_seconds = $timer.Elapsed.TotalSeconds
  $report.preferences_after = Get-PreferenceSnapshot
  for ($index = 0; $index -lt $report.preferences_before.Count; $index++) {
    $before = $report.preferences_before[$index]
    $after = $report.preferences_after[$index]
    if (($before.official -or $before.existed) -and ($before.existed -ne $after.existed -or $before.sha256 -ne $after.sha256)) {
      $report.status = 'failed'
      $report.preference_error = "Existing/official preferences changed: $($before.path)"
    }
  }
  $report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $evidence 'report.json') -Encoding utf8
}
if ($report.status -ne 'passed') { throw "Installed startup probe failed: $($report.error) $($report.cleanup_error) $($report.preference_error)" }
Write-Output "Installed normal startup passed; evidence: $evidence"
