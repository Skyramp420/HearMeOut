# HearMeOut – odinštalovanie: obnoví pôvodné efekty zariadení a odregistruje APO.
#   uninstall.ps1 [-RemoveConfig]
param([switch] $RemoveConfig, [switch] $Quiet)
. (Join-Path $PSScriptRoot 'common.ps1')
Assert-Admin $PSCommandPath $PSBoundParameters
if ($Quiet) { New-Item -ItemType Directory -Force -Path $DataDir | Out-Null; Start-Transcript -Path (Join-Path $DataDir 'last-run.log') -Force | Out-Null }

Write-Host "`n  HearMeOut – odinštalovanie`n" -ForegroundColor Cyan

# apply-fx musí byť v InstallDir (odtiaľ ho vie spustiť aj SYSTEM)
if (-not (Test-Path (Join-Path $InstallDir 'apply-fx.ps1'))) {
    New-Item -ItemType Directory -Force -Path $InstallDir | Out-Null
    Copy-Item (Join-Path $PSScriptRoot 'apply-fx.ps1') $InstallDir -Force
}

$ids = @(); if (Test-Path $StateFile) { $ids = @(Get-Content $StateFile | Where-Object { $_ }) }
foreach ($id in $ids) {
    if (Invoke-ApplyFx $id 'uninstall') { Write-Host "Obnovené: $id" -ForegroundColor Green }
    else { Write-Host "Nepodarilo sa obnoviť $id – pozri $DataDir\install.log" -ForegroundColor Red }
}

Stop-Audio
try {
    $dll = Join-Path $InstallDir 'HearMeOutApo.dll'
    if (Test-Path $dll) { Start-Process "$env:SystemRoot\System32\regsvr32.exe" -ArgumentList '/u', '/s', "`"$dll`"" -Wait }
    Remove-Item $InstallDir -Recurse -Force -ErrorAction SilentlyContinue
    if (Test-Path $StateFile) { Remove-Item $StateFile }
    if ($RemoveConfig) { Remove-Item $DataDir -Recurse -Force -ErrorAction SilentlyContinue }
}
finally { Start-Audio }
Restart-Sonar -Quiet:$Quiet

Write-Host "`nHearMeOut odstránený. (Config ostal v $DataDir, ak si nepoužil -RemoveConfig)" -ForegroundColor Cyan
