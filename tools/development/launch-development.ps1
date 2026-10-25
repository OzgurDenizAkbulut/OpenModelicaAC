[CmdletBinding()]
param(
  [string]$Install = (Join-Path $PSScriptRoot '../../build_cmake/development-128/install_cmake'),
  [Alias('Provenance')][string]$Manifest,
  [string]$ExpectedRevision,
  [string]$Omdev,
  [string]$ModelicaLibraryPath,
  [string[]]$ToolchainBin = @(),
  [switch]$PassThru,
  [ValidateSet('Normal','Hidden')][string]$WindowStyle = 'Normal',
  [string[]]$Model = @()
)
$ErrorActionPreference = 'Stop'
if (-not $ExpectedRevision) {
  $ExpectedRevision = & git -C (Join-Path $PSScriptRoot '../..') rev-parse HEAD
  if ($LASTEXITCODE -ne 0) { throw 'Cannot determine the source revision; specify -ExpectedRevision explicitly.' }
  $ExpectedRevision = $ExpectedRevision.Trim()
}
$candidate = (Resolve-Path -LiteralPath $Install).Path
$executable = Join-Path $candidate 'bin/OMEdit.exe'
$webEngineProcess = Join-Path $candidate 'bin/QtWebEngineProcess.exe'
if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
  throw "Candidate OMEdit is missing: $executable"
}
if (-not (Test-Path -LiteralPath $webEngineProcess -PathType Leaf)) {
  throw "Candidate QtWebEngine helper is missing: $webEngineProcess"
}
if ($candidate -match '1\.27') { throw 'Select the isolated source-built development installation.' }
for ($ancestor = Get-Item -LiteralPath $candidate; $null -ne $ancestor; $ancestor = $ancestor.Parent) {
  if ($ancestor.Attributes -band [IO.FileAttributes]::ReparsePoint) {
    throw "Candidate installation ancestors must not be reparse points: $($ancestor.FullName)"
  }
}
foreach ($path in @((Join-Path $candidate 'bin'), $executable, $webEngineProcess)) {
  if ((Get-Item -LiteralPath $path).Attributes -band [IO.FileAttributes]::ReparsePoint) {
    throw "Candidate installation paths must not be reparse points: $path"
  }
}
if (-not $Manifest) { $Manifest = Join-Path (Split-Path $candidate -Parent) 'installed-development.json' }
if (-not (Test-Path -LiteralPath $Manifest -PathType Leaf)) { throw "Successful install manifest is missing: $Manifest" }
$configuration = Get-Content -LiteralPath $Manifest -Raw | ConvertFrom-Json
if ($configuration.schema_version -ne 1 -or [IO.Path]::GetFullPath($configuration.prefix) -ne $candidate) {
  throw 'Successful install manifest does not match the selected installation.'
}
if (-not $configuration.gui_enabled -or -not $configuration.development_profile -or $configuration.head -ne $ExpectedRevision) {
  throw 'Successful install manifest must identify the pinned revision with the development GUI profile.'
}
foreach ($name in @('omc', 'OMEdit')) {
  $artifact = $configuration.artifacts.$name
  $expectedPath = Join-Path $candidate "bin/$name.exe"
  if (-not $artifact -or [IO.Path]::GetFullPath($artifact.path) -ne $expectedPath -or
      ((Get-Item -LiteralPath $expectedPath).Attributes -band [IO.FileAttributes]::ReparsePoint) -or
      (Get-FileHash -LiteralPath $expectedPath -Algorithm SHA256).Hash -ne $artifact.sha256) {
    throw "Installed $name does not match the successful install manifest."
  }
}
if (-not $ToolchainBin.Count) {
  $bundledTools = Join-Path $candidate 'tools/msys/ucrt64/bin'
  if (Test-Path -LiteralPath $bundledTools -PathType Container) {
    $ToolchainBin = @($bundledTools)
  } else {
    $ToolchainBin = @((Join-Path $configuration.msys 'ucrt64/bin'), (Join-Path $configuration.msys 'usr/bin'))
    if (-not $Omdev) { $Omdev = Split-Path (Split-Path $configuration.msys -Parent) -Parent }
  }
}
if (-not $Omdev) {
  foreach ($directory in $ToolchainBin) {
    if ($directory -match '[\\/]tools[\\/]msys[\\/]ucrt64[\\/]bin[\\/]?$') {
      $Omdev = $directory -replace '[\\/]tools[\\/]msys[\\/]ucrt64[\\/]bin[\\/]?$', ''
      break
    }
  }
}
$oldEnvironment = @{}
$keys = @('PATH', 'OPENMODELICAHOME', 'OPENMODELICALIBRARY', 'OMDEV', 'QT_PLUGIN_PATH',
          'QT_QPA_PLATFORM_PLUGIN_PATH', 'QML2_IMPORT_PATH', 'QML_IMPORT_PATH', 'QTDIR',
          'QTWEBENGINEPROCESS_PATH', 'QT_QPA_PLATFORM', 'QTWEBENGINE_DISABLE_SANDBOX',
          'QTWEBENGINE_CHROMIUM_FLAGS', 'QTWEBENGINE_REMOTE_DEBUGGING', 'OMEDIT_INLINE_PREVIEW_SCREENSHOT',
          'QTWEBENGINE_RESOURCES_PATH', 'QTWEBENGINE_LOCALES_PATH', 'QTWEBENGINE_DICTIONARIES_PATH')
$keys += 'QT_FORCE_STDERR_LOGGING'
$keys += @(Get-ChildItem Env: | Where-Object Name -Like 'OMEDIT_SMOKE_*' | Select-Object -ExpandProperty Name)
foreach ($key in $keys) { $oldEnvironment[$key] = [Environment]::GetEnvironmentVariable($key, 'Process') }
try {
  $explicitTools = @($ToolchainBin | ForEach-Object { (Resolve-Path -LiteralPath $_).Path })
  $env:PATH = (@((Join-Path $candidate 'bin')) + $explicitTools + @((Join-Path $env:SystemRoot 'System32'), $env:SystemRoot)) -join ';'
  $env:OPENMODELICAHOME = $candidate
  [Environment]::SetEnvironmentVariable('OPENMODELICALIBRARY', $ModelicaLibraryPath, 'Process')
  [Environment]::SetEnvironmentVariable('OMDEV', $Omdev, 'Process')
  $env:QTWEBENGINEPROCESS_PATH = $webEngineProcess
  foreach ($key in @('QT_PLUGIN_PATH', 'QT_QPA_PLATFORM_PLUGIN_PATH', 'QML2_IMPORT_PATH', 'QML_IMPORT_PATH', 'QTDIR',
                    'QT_QPA_PLATFORM', 'QTWEBENGINE_DISABLE_SANDBOX', 'QTWEBENGINE_CHROMIUM_FLAGS',
                    'QTWEBENGINE_REMOTE_DEBUGGING', 'OMEDIT_INLINE_PREVIEW_SCREENSHOT',
                    'QTWEBENGINE_RESOURCES_PATH', 'QTWEBENGINE_LOCALES_PATH', 'QTWEBENGINE_DICTIONARIES_PATH') +
                    @($keys | Where-Object { $_ -like 'OMEDIT_SMOKE_*' -or $_ -eq 'QT_FORCE_STDERR_LOGGING' })) {
    [Environment]::SetEnvironmentVariable($key, $null, 'Process')
  }
  # The development build supplies its own settings identity and temporary directory.
  $quotedModels = @($Model | ForEach-Object { '"' + (Resolve-Path -LiteralPath $_).Path + '"' })
  if ($quotedModels.Count) {
    $launched = Start-Process -FilePath $executable -WorkingDirectory $candidate -ArgumentList $quotedModels -PassThru -WindowStyle $WindowStyle
  } else {
    $launched = Start-Process -FilePath $executable -WorkingDirectory $candidate -PassThru -WindowStyle $WindowStyle
  }
  if ($PassThru) { $launched }
} finally {
  foreach ($key in $keys) { [Environment]::SetEnvironmentVariable($key, $oldEnvironment[$key], 'Process') }
}
