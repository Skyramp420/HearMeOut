# HearMeOut – zapíše / obnoví APO na jednom vstupnom zariadení (endpoint).
# Kľúče zariadení vlastní TrustedInstaller, preto ich najprv prevezmeme (ako Equalizer APO)
# a pôvodné oprávnenia (SDDL) si uložíme do zálohy, aby ich uninstall vrátil.
param(
    [Parameter(Mandatory)] [string] $EndpointId,
    [Parameter(Mandatory)] [ValidateSet('install', 'uninstall')] [string] $Mode,
    [ValidateSet('Auto', 'Legacy', 'Stream', 'Composite')] [string] $Slot = 'Auto',
    [string] $LogFile
)
$ErrorActionPreference = 'Stop'
$Clsid    = '{6F3B2A9E-4C1D-4E8B-9A57-2D8E0C4F1B63}'
$FxGuid   = '{d04e05a6-594b-4fb6-a80d-01af5eed7d1d}'
$SysFxOff = '{1da5d803-d492-4edd-8c23-e0c0ffee7f0e},5'      # PKEY_AudioEndpoint_Disable_SysFx
$Pids     = 1, 2, 5, 6, 7, 13, 14, 15
$Names    = $Pids | ForEach-Object { "$FxGuid,$_" }
$DataDir  = Join-Path $env:ProgramData 'HearMeOut'
$BackupDir = Join-Path $DataDir 'backup'
$SubBase  = "SOFTWARE\Microsoft\Windows\CurrentVersion\MMDevices\Audio\Capture\$EndpointId"
$SubFx    = "$SubBase\FxProperties"
$Base     = "HKLM:\$SubBase"
$Fx       = "HKLM:\$SubFx"
$Backup   = Join-Path $BackupDir "$EndpointId.json"

function Log($m) { if ($LogFile) { Add-Content -Path $LogFile -Value "$(Get-Date -Format 'HH:mm:ss') $m" -Encoding UTF8 }; Write-Host $m }

# ---------------------------------------------------------------- oprávnenia
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class HmoPriv {
    [StructLayout(LayoutKind.Sequential, Pack = 1)]
    struct TokPriv { public int Count; public long Luid; public int Attr; }
    [DllImport("advapi32.dll", SetLastError = true)] static extern bool OpenProcessToken(IntPtr h, int acc, out IntPtr tok);
    [DllImport("advapi32.dll", SetLastError = true)] static extern bool LookupPrivilegeValue(string sys, string name, out long luid);
    [DllImport("advapi32.dll", SetLastError = true)] static extern bool AdjustTokenPrivileges(IntPtr tok, bool all, ref TokPriv ns, int len, IntPtr prev, IntPtr rl);
    [DllImport("kernel32.dll")] static extern IntPtr GetCurrentProcess();
    public static bool Enable(string name) {
        IntPtr tok;
        if (!OpenProcessToken(GetCurrentProcess(), 0x28, out tok)) return false;
        TokPriv tp = new TokPriv(); tp.Count = 1; tp.Attr = 2;
        if (!LookupPrivilegeValue(null, name, out tp.Luid)) return false;
        return AdjustTokenPrivileges(tok, false, ref tp, 0, IntPtr.Zero, IntPtr.Zero);
    }
}
'@
[void][HmoPriv]::Enable('SeTakeOwnershipPrivilege')
[void][HmoPriv]::Enable('SeRestorePrivilege')

$RW  = [Microsoft.Win32.RegistryKeyPermissionCheck]::ReadWriteSubTree
$Rights = [System.Security.AccessControl.RegistryRights]
$Admins = New-Object System.Security.Principal.SecurityIdentifier('S-1-5-32-544')

function Get-Sddl([string]$sub) {
    $k = [Microsoft.Win32.Registry]::LocalMachine.OpenSubKey($sub, [Microsoft.Win32.RegistryKeyPermissionCheck]::ReadSubTree, $Rights::ReadPermissions)
    if (-not $k) { return $null }
    try { return $k.GetAccessControl().GetSecurityDescriptorSddlForm([System.Security.AccessControl.AccessControlSections]'Owner, Group, Access') } finally { $k.Close() }
}

function Grant-AdminWrite([string]$sub) {
    # 1) vlastník = Administrators
    $k = [Microsoft.Win32.Registry]::LocalMachine.OpenSubKey($sub, $RW, $Rights::TakeOwnership)
    $acl = $k.GetAccessControl([System.Security.AccessControl.AccessControlSections]::None)
    $acl.SetOwner($Admins); $k.SetAccessControl($acl); $k.Close()
    # 2) Administrators = plný prístup
    $k = [Microsoft.Win32.Registry]::LocalMachine.OpenSubKey($sub, $RW, $Rights::ChangePermissions -bor $Rights::ReadPermissions)
    $acl = $k.GetAccessControl()
    $rule = New-Object System.Security.AccessControl.RegistryAccessRule($Admins, 'FullControl', 'ContainerInherit', 'None', 'Allow')
    $acl.AddAccessRule($rule); $k.SetAccessControl($acl); $k.Close()
}

function Restore-Sddl([string]$sub, [string]$sddl) {
    $k = [Microsoft.Win32.Registry]::LocalMachine.OpenSubKey($sub, $RW, $Rights::TakeOwnership -bor $Rights::ChangePermissions -bor $Rights::ReadPermissions)
    if (-not $k) { return }
    $acl = New-Object System.Security.AccessControl.RegistrySecurity
    $acl.SetSecurityDescriptorSddlForm($sddl)
    $k.SetAccessControl($acl); $k.Close()
}

# ---------------------------------------------------------------- hlavná časť
try {
    if (-not (Test-Path $Base)) { throw "Endpoint $EndpointId neexistuje." }
    New-Item -ItemType Directory -Force -Path $BackupDir | Out-Null

    if ($Mode -eq 'install') {
        $fxExists = Test-Path $Fx
        # kľúč, ktorému meníme oprávnenia: FxProperties, alebo endpoint (ak FxProperties treba vytvoriť)
        $aclKey = if ($fxExists) { $SubFx } else { $SubBase }

        # 1) záloha hodnôt + pôvodných oprávnení (len raz – pri preinštalovaní ostane pôvodná)
        if (-not (Test-Path $Backup)) {
            $vals = [ordered]@{}
            if ($fxExists) {
                $key = Get-Item $Fx; $cur = Get-ItemProperty $Fx
                foreach ($n in $Names) { if ($null -ne $cur.$n) { $vals[$n] = @{ kind = "$($key.GetValueKind($n))"; value = $cur.$n } } }
            }
            $o = [ordered]@{ values = $vals; aclKey = $aclKey; sddl = (Get-Sddl $aclKey); createdFx = (-not $fxExists) }
            $o | ConvertTo-Json -Depth 6 | Set-Content -Path $Backup -Encoding UTF8
            Log "Záloha: $Backup"
        }

        # 2) prevziať kľúč
        Grant-AdminWrite $aclKey
        if (-not $fxExists) { New-Item -Path $Fx -Force | Out-Null }
        Log "Oprávnenia upravené: $aclKey"

        # 3) výber slotu a zápis
        $cur = Get-ItemProperty $Fx
        $has = { param($p) $null -ne $cur."$FxGuid,$p" }
        if ($Slot -eq 'Auto') {
            if (& $has 13) { $Slot = 'Composite' }
            elseif ((& $has 5) -or (& $has 6) -or (& $has 7)) { $Slot = 'Stream' }
            else { $Slot = 'Legacy' }
        }
        switch ($Slot) {
            'Composite' {
                $list = @($Clsid) + @($cur."$FxGuid,13" | Where-Object { $_ -and $_ -ne $Clsid })
                Set-ItemProperty -Path $Fx -Name "$FxGuid,13" -Value ([string[]]$list) -Type MultiString
            }
            'Stream' { Set-ItemProperty -Path $Fx -Name "$FxGuid,5" -Value $Clsid -Type String }
            'Legacy' { Set-ItemProperty -Path $Fx -Name "$FxGuid,1" -Value $Clsid -Type String }
        }
        Log "APO zapísané do slotu: $Slot"

        # 4) zapni „vylepšenia zvuku“, ak boli vypnuté (nepovinné – chyba tu nič nezastaví)
        foreach ($p in @($Fx, "$Base\Properties")) {
            try {
                if ((Test-Path $p) -and ((Get-ItemProperty $p).$SysFxOff -ne $null) -and ((Get-ItemProperty $p).$SysFxOff -ne 0)) {
                    Set-ItemProperty -Path $p -Name $SysFxOff -Value 0 -Type DWord
                    Log "Vylepšenia zvuku zapnuté ($p)"
                }
            } catch { Log "Upozornenie: neviem zapnúť vylepšenia zvuku v $p – zapni ich ručne v Nastaveniach zvuku." }
        }
    }
    else {
        if (-not (Test-Path $Backup)) { throw "Chýba záloha $Backup – nie je čo obnoviť." }
        $o = Get-Content $Backup -Raw -Encoding UTF8 | ConvertFrom-Json
        $aclKey = if ($o.aclKey) { $o.aclKey } else { $SubFx }
        if (Test-Path "HKLM:\$aclKey") { Grant-AdminWrite $aclKey }

        if ($o.createdFx) {
            if (Test-Path $Fx) { Remove-Item -Path $Fx -Recurse -Force }
        }
        elseif (Test-Path $Fx) {
            $cur = Get-ItemProperty $Fx
            $vals = if ($o.values) { $o.values } else { $o }    # kompatibilita so starou zálohou
            foreach ($n in $Names) {
                if ($null -ne $cur.$n) { Remove-ItemProperty -Path $Fx -Name $n }
                $b = $vals.$n
                if ($null -ne $b) {
                    $val = $b.value
                    if ($b.kind -eq 'MultiString') { $val = [string[]]@($val) }
                    elseif ($b.kind -eq 'Binary') { $val = [byte[]]@($val) }
                    Set-ItemProperty -Path $Fx -Name $n -Value $val -Type $b.kind
                }
            }
        }
        if ($o.sddl) { Restore-Sddl $aclKey $o.sddl; Log "Pôvodné oprávnenia vrátené: $aclKey" }
        Remove-Item $Backup
        Log "Pôvodné efekty obnovené pre $EndpointId"
    }
    exit 0
}
catch {
    Log "CHYBA: $($_.Exception.Message)  [riadok $($_.InvocationInfo.ScriptLineNumber)]"
    exit 1
}
