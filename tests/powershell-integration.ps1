# SPDX-License-Identifier: AGPL-3.0-or-later
$ErrorActionPreference = 'Stop'
$root = Join-Path ([IO.Path]::GetTempPath()) ('relay-shell-' + [guid]::NewGuid().ToString('N'))
[void][IO.Directory]::CreateDirectory($root)
try {
    $env:RELAY_RUNTIME_DIR = $root
    $env:RELAY_SESSION_TOKEN = 'test-token'
    $env:RELAY_CLEAN_SHELL = '1'
    $env:RELAY_START_DIR = $root
    . (Join-Path $PSScriptRoot '../shell/integration.ps1')
    __relay_event 'ready' 7
    $state = Get-Content -Raw -LiteralPath (Join-Path $root 'state.json') | ConvertFrom-Json
    if ($state.token -ne 'test-token' -or $state.status -ne 7 -or $state.cwd -ne $root -or
        $state.shell_pid -ne $PID -or $state.known_commands -notcontains 'Write-Output') {
        throw 'Ready event lost shell context'
    }
    $bytes = [Text.Encoding]::UTF8.GetBytes("Write-Output 'héllo 世界'`n`n")
    __relay_event 'loaded' 0 $bytes
    $state = Get-Content -Raw -LiteralPath (Join-Path $root 'state.json') | ConvertFrom-Json
    $expected = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($bytes)).ToLowerInvariant()
    if ($state.input_sha256 -ne $expected) { throw 'Loaded hash is not exact UTF8 bytes' }
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
