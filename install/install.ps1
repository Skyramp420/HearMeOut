# HearMeOut – inštalácia APO na mikrofón
#   install.ps1                       -> vyberieš zariadenie zo zoznamu
#   install.ps1 -Device Focusrite     -> zvolí prvé zariadenie, ktorého názov obsahuje "Focusrite"
#   install.ps1 -Slot Legacy|Stream|Composite   -> ručný výber slotu, ak Auto nefunguje
param(
    [string] $Device,
    [string] $EndpointId,                 # presné ID zariadenia (používa appka)
    [ValidateSet('Auto', 'Legacy', 'Stream', 'Composite')] [string] $Slot = 'Auto',
    [switch] $Quiet                       # bez otázok (appka)
)
. (Join-Path $PSScriptRoot 'common.ps1')
Assert-Admin $PSCommandPath $PSBoundParameters
if ($Quiet) { New-Item -ItemType Directory -Force -Path $DataDir | Out-Null; Start-Transcript -Path (Join-Path $DataDir 'last-run.log') -Force | Out-Null }

Write-Host "`n  HearMeOut – inštalácia`n" -ForegroundColor Cyan

$dll = Join-Path $PSScriptRoot 'HearMeOutApo.dll'
if (-not (Test-Path $dll)) { throw "Chýba $dll" }

# 1) výber zariadenia
$eps = @(Get-CaptureEndpoints)
if ($eps.Count -eq 0) { throw 'Nenašiel som žiadne aktívne vstupné zariadenie.' }
$ep = $null
if ($EndpointId) { $ep = $eps | Where-Object { $_.Id -eq $EndpointId } | Select-Object -First 1; if (-not $ep) { throw "Zariadenie $EndpointId nie je aktívne." } }
if (-not $ep -and $Device) { $ep = $eps | Where-Object { "$($_.Name) $($_.Adapter)" -like "*$Device*" } | Select-Object -First 1 }
if (-not $ep -and -not $Quiet) {
    for ($i = 0; $i -lt $eps.Count; $i++) { Write-Host ("  [{0}] {1}  ({2})" -f ($i + 1), $eps[$i].Name, $eps[$i].Adapter) }
    $sel = Read-Host "`nKtoré zariadenie je tvoj mikrofón? (číslo)"
    $ep = $eps[[int]$sel - 1]
}
if (-not $ep) { throw 'Neplatná voľba.' }
Write-Host "`nZariadenie: $($ep.Name) ($($ep.Adapter))  $($ep.Id)" -ForegroundColor Green

# 2) súbory – audio služba drží DLL zamknuté, preto ju na chvíľu zastavíme
New-Item -ItemType Directory -Force -Path $InstallDir, $DataDir | Out-Null
Stop-Audio
try {
    Copy-Item $dll -Destination $InstallDir -Force
    foreach ($f in 'apply-fx.ps1', 'common.ps1', 'uninstall.ps1') { Copy-Item (Join-Path $PSScriptRoot $f) -Destination $InstallDir -Force }
    $cfgSrc = Join-Path $PSScriptRoot 'default.ini'
    $cfg = Join-Path $DataDir 'config.ini'
    if (-not (Test-Path $cfg) -and (Test-Path $cfgSrc)) { Copy-Item $cfgSrc $cfg }
    # audiodg beží ako LOCAL SERVICE – potrebuje čítať config a zapisovať status
    icacls $DataDir /grant '*S-1-5-19:(OI)(CI)M' '*S-1-5-32-545:(OI)(CI)M' /T /Q | Out-Null

    # 3) COM registrácia
    $p = Start-Process -FilePath "$env:SystemRoot\System32\regsvr32.exe" -ArgumentList '/s', "`"$InstallDir\HearMeOutApo.dll`"" -Wait -PassThru
    if ($p.ExitCode -ne 0) { throw "regsvr32 zlyhal (kód $($p.ExitCode))" }

    # 4) povoliť nepodpísané APO v audiodg (rovnaký krok robí Equalizer APO)
    Set-ItemProperty -Path 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Audio' -Name 'DisableProtectedAudioDG' -Value 1 -Type DWord

    # 5) pripojiť APO k zariadeniu
    if (-not (Invoke-ApplyFx $ep.Id 'install' $Slot)) { throw "Zápis do zariadenia zlyhal – pozri $DataDir\install.log" }
    $known = @(); if (Test-Path $StateFile) { $known = @(Get-Content $StateFile) }
    if ($known -notcontains $ep.Id) { Add-Content -Path $StateFile -Value $ep.Id }
}
finally {
    Start-Audio
}
Restart-Sonar -Quiet:$Quiet

Write-Host @"

  Hotovo!
  - Otvor nahrávanie mikrofónu (Discord test, OBS, Hlasový záznamník) – APO sa načíta, keď mic niekto používa.
  - V Discorde vyber ako vstup priamo Focusrite, NIE Sonar mikrofón (Sonar môže efekty Windows obísť).
  - Stav:   $DataDir\status.ini   (loaded=1, blocks rastie = beží)
  - Ladenie: uprav $DataDir\config.ini a ulož – zmena platí hneď.
  - Nezabudni v Light Hoste/Focusrite vypnúť loopback, nech nepočuješ efekty dvakrát.
"@ -ForegroundColor Cyan
