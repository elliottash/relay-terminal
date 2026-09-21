# SPDX-License-Identifier: AGPL-3.0-or-later
# PowerShell 7: -NoLogo -NoProfile -NoExit -ExecutionPolicy Bypass -File integration.ps1
# Profiles are loaded here so clean-shell recovery does not change the user's files.
if ($env:RELAY_CLEAN_SHELL -ne '1') {
    foreach ($profilePath in @($PROFILE.AllUsersAllHosts, $PROFILE.AllUsersCurrentHost,
                               $PROFILE.CurrentUserAllHosts, $PROFILE.CurrentUserCurrentHost)) {
        if (Test-Path -LiteralPath $profilePath) { . $profilePath }
    }
}
if (!$env:RELAY_RUNTIME_DIR -or !$env:RELAY_SESSION_TOKEN) { return }
if ($env:RELAY_START_DIR -and (Test-Path -LiteralPath $env:RELAY_START_DIR -PathType Container)) {
    Set-Location -LiteralPath $env:RELAY_START_DIR
}
Remove-Item Env:RELAY_START_DIR -ErrorAction SilentlyContinue
Import-Module PSReadLine -ErrorAction Stop
# Import only trusted, bundled core modules. Discovery of every installed module's exports
# can enumerate tens of thousands of commands and take seconds on each prompt.
Import-Module "$PSHOME/Modules/Microsoft.PowerShell.Management/Microsoft.PowerShell.Management.psd1"
Import-Module "$PSHOME/Modules/Microsoft.PowerShell.Utility/Microsoft.PowerShell.Utility.psd1"

function global:__relay_known_commands {
    $imported = @(Get-Command -ListImported -CommandType Alias,Function,Cmdlet)
    # Keep built-in cmdlets and user-defined functions ahead of large imported module catalogs.
    $prioritized = @($imported | Where-Object { $_.ModuleName -like 'Microsoft.PowerShell.*' }) +
                   @($imported | Where-Object { !$_.ModuleName }) + $imported
    $seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    $names = [Collections.Generic.List[string]]::new()
    $remainingBytes = 512 * 1024
    foreach ($commandInfo in $prioritized) {
        $name = $commandInfo.Name
        if ($name.StartsWith('__relay_') -or !$seen.Add($name)) { continue }
        # Worst-case JSON escaping is six bytes per UTF16 code unit, plus quotes/comma.
        # Reserve the other half of the GUI's 1MiB message limit for cwd, PATH and metadata.
        $cost = $name.Length * 6 + 3
        if ($cost -gt $remainingBytes) { continue }
        $names.Add($name)
        $remainingBytes -= $cost
        if ($names.Count -ge 20000) { break }
    }
    return $names.ToArray()
}

function global:__relay_event([string] $Stage, [int] $Status = 0, [byte[]] $InputBytes) {
    # Do not start Python for prompt notifications or change LASTEXITCODE.
    $temporary = Join-Path $env:RELAY_RUNTIME_DIR ('state-' + [guid]::NewGuid().ToString('N'))
    try {
        $eventData = @{
            token = $env:RELAY_SESSION_TOKEN
            sequence = [DateTime]::UtcNow.Ticks.ToString()
            event = $Stage; status = $Status; cwd = $PWD.Path; shell_pid = $PID
        }
        if ($Stage -eq 'ready') {
            $eventData.known_commands = @(__relay_known_commands)
            $eventData.path = $env:PATH
        } elseif ($Stage -eq 'loaded') {
            $eventData.input_sha256 = [Convert]::ToHexString(
                [System.Security.Cryptography.SHA256]::HashData($InputBytes)).ToLowerInvariant()
        }
        [IO.File]::WriteAllText($temporary, ($eventData | ConvertTo-Json -Compress -Depth 3),
                               [Text.UTF8Encoding]::new($false))
        # Windows readers can briefly deny FILE_SHARE_DELETE. Keep the atomic file and
        # retry the rename; losing a loaded event would leave the composer waiting forever.
        for ($attempt = 0; ; $attempt++) {
            try {
                [IO.File]::Move($temporary, (Join-Path $env:RELAY_RUNTIME_DIR 'state.json'), $true)
                break
            } catch [IO.IOException], [UnauthorizedAccessException] {
                # File.Move(overwrite) reports a denied exclusive destination handle as
                # UnauthorizedAccessException on Windows, not only as IOException.
                if ($attempt -ge 39 -or ![IO.Directory]::Exists($env:RELAY_RUNTIME_DIR)) { throw }
                [Threading.Thread]::Sleep(10)
            }
        }
    } catch [IO.IOException] {
        # Closing a pane removes its private runtime directory.
    } catch [UnauthorizedAccessException] {
    } finally {
        if ([IO.File]::Exists($temporary)) { [IO.File]::Delete($temporary) }
    }
}

function global:__relay_load {
    $inputPath = Join-Path $env:RELAY_RUNTIME_DIR 'input.txt'
    if (![IO.File]::Exists($inputPath)) { return }
    # Hash the exact bytes loaded, not a second read that could acknowledge newer input.
    $bytes = [IO.File]::ReadAllBytes($inputPath)
    $text = [Text.UTF8Encoding]::new($false, $true).GetString($bytes)
    $line = ''; $cursor = 0
    [Microsoft.PowerShell.PSConsoleReadLine]::GetBufferState([ref]$line, [ref]$cursor)
    [Microsoft.PowerShell.PSConsoleReadLine]::Replace(0, $line.Length, $text)
    __relay_event 'loaded' 0 $bytes
}
Set-PSReadLineKeyHandler -Chord 'Ctrl+x,Ctrl+r' -ScriptBlock { __relay_load }
Set-PSReadLineKeyHandler -Chord 'Ctrl+x,Ctrl+p' -ScriptBlock {
    [Microsoft.PowerShell.PSConsoleReadLine]::InvokePrompt()
}

# Wrap the host's reader, not just Enter: every accepted line (including multiline input
# and other accept-line bindings) emits running, while cancelled/incomplete edits do not.
$global:__relay_readline = (Get-Command PSConsoleHostReadLine).ScriptBlock
function global:PSConsoleHostReadLine {
    $line = & $global:__relay_readline
    if (![string]::IsNullOrWhiteSpace($line)) { __relay_event 'running' }
    return $line
}
$global:__relay_prompt = (Get-Command prompt).ScriptBlock
function global:prompt {
    $succeeded = $?
    $nativeStatus = $global:LASTEXITCODE
    $status = if ($succeeded) { 0 } elseif ($nativeStatus) { $nativeStatus } else { 1 }
    $rendered = & $global:__relay_prompt
    __relay_event 'ready' $status
    return $rendered
}
function global:relay {
    if ($args.Count -ge 1 -and $args[0] -eq 'open') {
        $target = if ($args.Count -gt 1) { $args[1] } else { '.' }
        $previous = $env:RELAY_OPEN_FROM_SHELL
        try {
            $env:RELAY_OPEN_FROM_SHELL = '1'
            & $env:RELAY_PYTHON $env:RELAY_OPEN_HELPER $target
        } finally { $env:RELAY_OPEN_FROM_SHELL = $previous }
    } else { Write-Error 'usage: relay open [PATH]' }
}
