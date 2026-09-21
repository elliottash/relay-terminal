# SPDX-License-Identifier: AGPL-3.0-or-later
param([string]$Version = '0.1.0-beta.3', [string]$Build = 'build-app', [string]$Stage = 'stage')
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $true
cmake --install $Build --config Release --prefix $Stage
$Stage = (Resolve-Path $Stage).Path
windeployqt --release --no-translations --compiler-runtime "$Stage/bin/relay.exe"
New-Item -ItemType Directory -Force "$Stage/runtime/python", "$Stage/runtime/powershell", 'dist' | Out-Null
function Get-PinnedArchive($Url, $Sha, $Target) {
    $zip = Join-Path $env:RUNNER_TEMP ([guid]::NewGuid().ToString() + '.zip')
    Invoke-WebRequest $Url -OutFile $zip
    if ((Get-FileHash $zip -Algorithm SHA256).Hash.ToLower() -ne $Sha) { throw "Checksum mismatch: $Url" }
    Expand-Archive $zip $Target -Force
    Remove-Item $zip
}
Get-PinnedArchive 'https://www.python.org/ftp/python/3.13.13/python-3.13.13-embed-amd64.zip' '8766a8775746235e23cf5aee5027ab1060bb981d93110577adcf3508aa0cbd55' "$Stage/runtime/python"
Get-PinnedArchive 'https://github.com/PowerShell/PowerShell/releases/download/v7.6.6/PowerShell-7.6.6-win-x64.zip' '02fe458be20493fbdf43f61ea20610b811ee6c738ab1676c61b9cfcd1a33c860' "$Stage/runtime/powershell"
# Explicit private paths work even with the worker's -S; no user site-packages or Store alias.
@'
python313.zip
.
Lib/site-packages
../../share/relay/backend
../../share/relay
'@ | Set-Content "$Stage/runtime/python/python313._pth" -Encoding ascii
python -m pip install --only-binary=:all: --platform win_amd64 --python-version 3.13 --implementation cp --target "$Stage/runtime/python/Lib/site-packages" 'cryptography==46.0.5'
& "$Stage/runtime/python/python.exe" -S -c 'import cryptography; from relay_core import agent, board, keystore; import remote.gui_host'
$compiler = Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 6/ISCC.exe'
& $compiler "/DRelayVersion=$Version" "/DRelayStage=$Stage" 'packaging/windows/relay.iss'
