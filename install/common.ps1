# Spoločné funkcie pre install / uninstall
$ErrorActionPreference = 'Stop'
$InstallDir = Join-Path $env:ProgramFiles 'HearMeOut'
$DataDir    = Join-Path $env:ProgramData 'HearMeOut'
$CaptureKey = 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\MMDevices\Audio\Capture'
$StateFile  = Join-Path $DataDir 'installed-endpoints.txt'

function Assert-Admin([string]$ScriptPath, [hashtable]$Bound) {
    $id = [Security.Principal.WindowsIdentity]::GetCurrent()
    if (([Security.Principal.WindowsPrincipal]$id).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) { return }
    $argList = @('-NoProfile', '-ExecutionPolicy', 'Bypass')
    if (-not $Bound.ContainsKey('Quiet')) { $argList += '-NoExit' }
    $argList += @('-File', "`"$ScriptPath`"")
    foreach ($k in $Bound.Keys) {
        $v = $Bound[$k]
        if ($v -is [switch]) { if ($v) { $argList += "-$k" } } else { $argList += "-$k"; $argList += "`"$v`"" }
    }
    Start-Process powershell.exe -Verb RunAs -ArgumentList $argList
    exit
}

function Get-CaptureEndpoints {
    Get-ChildItem $CaptureKey | ForEach-Object {
        $state = (Get-ItemProperty $_.PSPath).DeviceState
        if ($state -ne 1) { return }                       # 1 = aktívne
        $props = Get-ItemProperty "$($_.PSPath)\Properties" -ErrorAction SilentlyContinue
        [pscustomobject]@{
            Id      = $_.PSChildName
            Name    = $props.'{a45c254e-df1c-4efd-8020-67d146a850e0},2'     # napr. "Mikrofón" / "Analogue 1 + 2"
            Adapter = $props.'{b3f8fa53-0004-438e-9003-51a46e139bfc},6'     # napr. "Focusrite USB Audio"
        }
    }
}

function Invoke-ApplyFx([string]$EndpointId, [string]$Mode, [string]$Slot = 'Auto') {
    $script = Join-Path $InstallDir 'apply-fx.ps1'
    $log = Join-Path $DataDir 'install.log'
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $script -EndpointId $EndpointId -Mode $Mode -Slot $Slot -LogFile $log
    return ($LASTEXITCODE -eq 0)
}

$script:AudioDeps = @()
function Stop-Audio {
    # zapamätaj si bežiace závislé služby, aby sme ich po štarte vrátili
    $script:AudioDeps = @((Get-Service audiosrv).DependentServices | Where-Object Status -eq 'Running' | ForEach-Object Name)
    Stop-Service -Name audiosrv -Force
}

function Start-Audio {
    # spusti, over, pri neúspechu skús znova – zvuk nesmie ostať vypnutý
    for ($i = 1; $i -le 5; $i++) {
        try {
            Start-Service -Name AudioEndpointBuilder -ErrorAction SilentlyContinue
            Start-Service -Name audiosrv -ErrorAction Stop
        } catch { Start-Sleep -Seconds 1 }
        if ((Get-Service audiosrv).Status -eq 'Running') {
            foreach ($d in $script:AudioDeps) { Start-Service -Name $d -ErrorAction SilentlyContinue }
            Write-Host 'Windows Audio beží.' -ForegroundColor Green
            return
        }
        Start-Sleep -Seconds 1
    }
    Write-Host 'POZOR: Windows Audio sa nepodarilo spustiť! Reštartuj PC alebo spusti: Start-Service AudioEndpointBuilder, Audiosrv' -ForegroundColor Red
}

function Restart-Sonar([switch]$Quiet) {
    # reštart audio služby odpojí SteelSeries Sonar – jeho virtuálne zariadenia sa samé neobnovia
    $procs = @(Get-Process -Name 'SteelSeries*' -ErrorAction SilentlyContinue)
    if ($procs.Count -eq 0) { return }
    $gg = $procs | Where-Object { $_.Name -like 'SteelSeriesGG*' -and $_.Path } | Select-Object -First 1
    $path = if ($gg) { $gg.Path } else { Join-Path $env:ProgramFiles 'SteelSeries\GG\SteelSeriesGG.exe' }
    if (-not $Quiet) {
        $ans = Read-Host "`nBeží SteelSeries GG/Sonar – po reštarte audia ho treba reštartovať. Urobiť to teraz? [A/n]"
        if ($ans -match '^(n|nie)$') { Write-Host 'OK – reštartuj Sonar ručne, inak nepôjde zvuk cez Novu.' -ForegroundColor Yellow; return }
    }
    Write-Host 'Reštartujem SteelSeries GG...'
    $procs | Stop-Process -Force -ErrorAction SilentlyContinue
    # počkaj, kým naozaj skončí (inak nový proces narazí na starý zámok a hneď sa zavrie)
    $deadline = (Get-Date).AddSeconds(10)
    while ((Get-Process -Name 'SteelSeries*' -ErrorAction SilentlyContinue) -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 300 }
    Start-Sleep -Seconds 1
    if (-not (Test-Path $path)) { Write-Host "Nenašiel som $path – spusti SteelSeries GG ručne." -ForegroundColor Yellow; return }
    for ($try = 1; $try -le 2; $try++) {
        # ShellExecute cez Prieskumníka = spustí sa ako bežný používateľ, nie ako admin
        try { (New-Object -ComObject Shell.Application).ShellExecute($path, '', (Split-Path $path), 'open', 1) }
        catch { Start-Process -FilePath $path -WorkingDirectory (Split-Path $path) }
        $deadline = (Get-Date).AddSeconds(12)
        while ((Get-Date) -lt $deadline) {
            if (Get-Process -Name 'SteelSeriesGG*' -ErrorAction SilentlyContinue) { Write-Host 'SteelSeries GG beží.' -ForegroundColor Green; return }
            Start-Sleep -Milliseconds 500
        }
    }
    Write-Host 'SteelSeries GG sa nepodarilo spustiť – spusti ho ručne.' -ForegroundColor Yellow
}

# ---- odložené operácie pri reštarte PC (súbor drží audiodg zamknutý) ----
Add-Type -Namespace HmoNative -Name Kernel -MemberDefinition @'
[System.Runtime.InteropServices.DllImport("kernel32.dll", SetLastError = true, CharSet = System.Runtime.InteropServices.CharSet.Unicode)]
public static extern bool MoveFileEx(string from, string to, int flags);
'@
function Copy-OrDefer([string]$Src, [string]$Dst) {
    # skús hneď; ak je súbor zamknutý, nasaď ho pri najbližšom reštarte
    try { Copy-Item $Src -Destination $Dst -Force -ErrorAction Stop; return $false }
    catch {
        $staged = "$Dst.new"
        Copy-Item $Src -Destination $staged -Force
        if (-not [HmoNative.Kernel]::MoveFileEx($staged, $Dst, 5)) { throw "Nepodarilo sa naplánovať nahradenie $Dst" }   # REPLACE_EXISTING | DELAY_UNTIL_REBOOT
        Write-Host "Súbor je práve používaný – nová verzia sa nasadí pri reštarte PC." -ForegroundColor Yellow
        return $true
    }
}
function Remove-OrDefer([string]$Path) {
    if (-not (Test-Path $Path)) { return }
    try { Remove-Item $Path -Force -ErrorAction Stop }
    catch { [void][HmoNative.Kernel]::MoveFileEx($Path, $null, 4) }   # DELAY_UNTIL_REBOOT
}
