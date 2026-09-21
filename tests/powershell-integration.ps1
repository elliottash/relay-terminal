# SPDX-License-Identifier: AGPL-3.0-or-later
$ErrorActionPreference = 'Stop'
$root = Join-Path ([IO.Path]::GetTempPath()) ('relay-shell-' + [guid]::NewGuid().ToString('N'))
[void][IO.Directory]::CreateDirectory($root)
# Windows TEMP can contain an 8.3 name (RUNNER~1) while PowerShell expands it in PWD.
# Ask the provider for its canonical location before returning to the original directory,
# so the test still proves that integration changes to RELAY_START_DIR.
Push-Location -LiteralPath $root
$expectedCwd = $PWD.Path
Pop-Location
try {
    $env:RELAY_RUNTIME_DIR = $root
    $env:RELAY_SESSION_TOKEN = 'test-token'
    $env:RELAY_CLEAN_SHELL = '1'
    $env:RELAY_START_DIR = $root
    . (Join-Path $PSScriptRoot '../shell/integration.ps1')
    __relay_event 'ready' 7
    $state = Get-Content -Raw -LiteralPath (Join-Path $root 'state.json') | ConvertFrom-Json
    foreach ($field in @{token = 'test-token'; status = 7; cwd = $expectedCwd; shell_pid = $PID}.GetEnumerator()) {
        if ($state.($field.Key) -ne $field.Value) {
            throw "Ready event $($field.Key): expected '$($field.Value)', got '$($state.($field.Key))'"
        }
    }
    if ($state.known_commands -notcontains 'Write-Output') {
        throw "Ready event omitted Write-Output from $($state.known_commands.Count) commands"
    }
    # A module-heavy machine must not crowd core commands out of the prompt event or
    # trigger module initialization just to report a ready prompt.
    $moduleRoot = Join-Path $root 'modules'
    $moduleDir = Join-Path $moduleRoot 'RelayHugeUnloaded'
    [void][IO.Directory]::CreateDirectory($moduleDir)
    [IO.File]::WriteAllText((Join-Path $moduleDir 'RelayHugeUnloaded.psm1'), "throw 'Must not import discovery fixture'")
    $exports = @(1..21000 | ForEach-Object { 'AAA-Export' + $_ })
    New-ModuleManifest -Path (Join-Path $moduleDir 'RelayHugeUnloaded.psd1') -RootModule 'RelayHugeUnloaded.psm1' `
        -ModuleVersion '1.0' -FunctionsToExport $exports -CmdletsToExport @() -AliasesToExport @()
    $oldModulePath = $env:PSModulePath
    try {
        $env:PSModulePath = $moduleRoot + [IO.Path]::PathSeparator + $oldModulePath
        function global:RelayUserFixture { 'user function' }
        __relay_event 'ready'
        $state = Get-Content -Raw -LiteralPath (Join-Path $root 'state.json') | ConvertFrom-Json
        if ($state.known_commands -notcontains 'Write-Output' -or
            $state.known_commands -notcontains 'RelayUserFixture' -or
            $state.known_commands -contains 'AAA-Export1') { throw 'Ready command priority/discovery regression' }
        if ((Get-Item -LiteralPath (Join-Path $root 'state.json')).Length -ge 1MB) { throw 'Ready event exceeds GUI limit' }
    } finally { $env:PSModulePath = $oldModulePath }
    $bytes = [Text.Encoding]::UTF8.GetBytes("Write-Output 'héllo 世界'`n`n")
    __relay_event 'loaded' 0 $bytes
    $state = Get-Content -Raw -LiteralPath (Join-Path $root 'state.json') | ConvertFrom-Json
    $expected = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($bytes)).ToLowerInvariant()
    if ($state.input_sha256 -ne $expected) { throw 'Loaded hash is not exact UTF8 bytes' }
    if ($IsWindows) {
        # Reproduce a GUI reader that briefly holds state.json without delete sharing.
        Add-Type -TypeDefinition @'
public static class RelayReleaseFile {
    public static void Later(System.IDisposable file) {
        System.Threading.Tasks.Task.Run(() => { System.Threading.Thread.Sleep(100); file.Dispose(); });
    }
}
'@
        $locked = [IO.File]::Open((Join-Path $root 'state.json'), [IO.FileMode]::Open,
                                  [IO.FileAccess]::Read, [IO.FileShare]::Read)
        [RelayReleaseFile]::Later($locked)
        __relay_event 'running' 9
        $state = Get-Content -Raw -LiteralPath (Join-Path $root 'state.json') | ConvertFrom-Json
        if ($state.event -ne 'running' -or $state.status -ne 9) { throw 'Reader lock lost shell event' }
    }
    $global:LASTEXITCODE = 13
    __relay_event 'running'
    if ($global:LASTEXITCODE -ne 13) { throw 'Events overwrite native exit status' }
    $handlers = Get-PSReadLineKeyHandler -Chord 'Ctrl+x,Ctrl+r', 'Ctrl+x,Ctrl+p'
    if ($handlers.Count -ne 2) { throw 'Composer handshake key bindings missing' }
    Write-Output 'PowerShell shell events passed'
} finally {
    Set-Location $PSScriptRoot
    [IO.Directory]::Delete($root, $true)
}
