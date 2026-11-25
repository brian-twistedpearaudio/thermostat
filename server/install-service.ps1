#Requires -Version 5.1
<#
.SYNOPSIS
Installs the thermostat server as a Windows service, or updates it. Safe to run again.
.PARAMETER CopyFrom
A data folder to copy thermostat.db and token.txt from, such as .\data. Stop that server first.
.PARAMETER NewData
Start with no readings and a new token.
#>
param(
    [string]$CopyFrom,
    [switch]$NewData
)
Set-StrictMode -Version 2
$ErrorActionPreference = 'Stop'

$name = 'ThermostatServer'
$account = "NT SERVICE\$name"
$port = 5080
$root = Join-Path ([Environment]::GetFolderPath('ProgramFiles')) 'Thermostat Server'
$app = Join-Path $root 'app'
$newApp = Join-Path $root 'app.new'
$oldApp = Join-Path $root 'app.old'
$data = Join-Path ([Environment]::GetFolderPath('CommonApplicationData')) $name
$db = Join-Path $data 'thermostat.db'
$token = Join-Path $data 'token.txt'

# No parameters of its own, so that options such as -c reach the program.
function Invoke-Native {
    $program, $rest = $args
    & $program @rest | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "$program failed with exit code $LASTEXITCODE." }
}

# ----- Checks: nothing is changed before these pass -----

$me = New-Object Security.Principal.WindowsPrincipal ([Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $me.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Run this in PowerShell opened with "Run as administrator".'
}
if ($CopyFrom -and $NewData) { throw 'Use -CopyFrom or -NewData, not both.' }

# The service account cannot use a runtime installed for one user only.
$dotnet = Join-Path ([Environment]::GetFolderPath('ProgramFiles')) 'dotnet'
if (-not (Test-Path (Join-Path $dotnet 'shared\Microsoft.AspNetCore.App\10.*'))) {
    throw "ASP.NET Core 10 is not installed for all users in $dotnet."
}

if ((Test-Path $data) -and ((Get-Item $data -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
    throw "$data is a link. Remove it first."
}
$hasDb = Test-Path $db
$hasToken = Test-Path $token
if ($hasDb -ne $hasToken) { throw "$data has only one of thermostat.db and token.txt. Put the other back first." }
if ($hasDb -and ($CopyFrom -or $NewData)) { throw "$data already holds data, which is never overwritten. Run again without -CopyFrom or -NewData." }
if (-not $hasDb -and -not $CopyFrom -and -not $NewData) {
    throw 'There is no data for the service yet. Use -CopyFrom <folder> to keep your readings and token, or -NewData to start empty.'
}
if ($CopyFrom) {
    $CopyFrom = (Resolve-Path -LiteralPath $CopyFrom).ProviderPath
    foreach ($file in 'thermostat.db', 'token.txt') {
        $source = Join-Path $CopyFrom $file
        if (-not (Test-Path -LiteralPath $source)) { throw "$source was not found." }
        try { [IO.File]::Open($source, 'Open', 'ReadWrite', 'None').Dispose() }
        catch { throw "$source is in use. Stop the server that uses it first." }
    }
}

$service = Get-Service $name -ErrorAction SilentlyContinue
if (-not $service -or $service.Status -eq 'Stopped') {
    if (Get-NetTCPConnection -LocalPort $port -State Listen -ErrorAction SilentlyContinue) {
        throw "Port $port is in use. Stop the server running in a terminal first."
    }
}

# ----- Build beside the running copy -----

if (Test-Path $newApp) { Remove-Item $newApp -Recurse -Force }
Invoke-Native dotnet publish (Join-Path $PSScriptRoot 'Thermostat.Server.csproj') -c Release -o $newApp

# ----- Service and account -----

if ($service) {
    Stop-Service $name
    $service.WaitForStatus('Stopped', [TimeSpan]::FromSeconds(60))
}

$exe = Join-Path $app 'Thermostat.Server.exe'
$commandLine = "`"$exe`" --DataFolder `"$data`""
if (-not $service) {
    New-Service -Name $name -DisplayName 'Thermostat Server' -StartupType Manual -BinaryPathName $commandLine `
        -Description 'Stores the readings of the thermostats and shows their charts.' | Out-Null
}
# A virtual account: no password, and no rights beyond an ordinary user's except on the data folder.
Invoke-Native sc.exe config $name obj= $account
$installed = Get-CimInstance Win32_Service -Filter "Name='$name'"
if ($installed.StartName -ne $account) { throw "The service account is $($installed.StartName), not $account." }
if ($installed.PathName -ne $commandLine) { throw "The service runs $($installed.PathName). Uninstall it, then run this again." }

# ----- Data folder: the service, SYSTEM and administrators only -----

New-Item -ItemType Directory -Path $data -Force | Out-Null
$admins = New-Object Security.Principal.SecurityIdentifier 'S-1-5-32-544'
$system = New-Object Security.Principal.SecurityIdentifier 'S-1-5-18'
$acl = New-Object Security.AccessControl.DirectorySecurity
$acl.SetAccessRuleProtection($true, $false)
$acl.SetOwner($admins)
foreach ($entry in @(@($admins, 'FullControl'), @($system, 'FullControl'), @($account, 'Modify'))) {
    $rule = New-Object Security.AccessControl.FileSystemAccessRule($entry[0], $entry[1], 'ContainerInherit,ObjectInherit', 'None', 'Allow')
    $acl.AddAccessRule($rule)
}
Set-Acl -LiteralPath $data -AclObject $acl
if (Get-ChildItem $data -Force) { Invoke-Native icacls.exe (Join-Path $data '*') /reset /T /Q }

# Copied whole, then moved into place without overwriting. The original stays as it is.
if ($CopyFrom) {
    $incoming = Join-Path $data 'incoming'
    if (Test-Path $incoming) { Remove-Item $incoming -Recurse -Force }
    New-Item -ItemType Directory -Path $incoming | Out-Null
    foreach ($file in 'thermostat.db-wal', 'token.txt', 'thermostat.db') {
        $source = Join-Path $CopyFrom $file
        if (Test-Path -LiteralPath $source) { Copy-Item -LiteralPath $source -Destination $incoming }
    }
    Get-ChildItem $incoming | Move-Item -Destination $data
    Remove-Item $incoming
}

# ----- Program files -----

if (Test-Path $oldApp) { Remove-Item $oldApp -Recurse -Force }
if (Test-Path $app) { Move-Item $app $oldApp }
try { Move-Item $newApp $app }
catch {
    if ((Test-Path $oldApp) -and -not (Test-Path $app)) { Move-Item $oldApp $app }
    throw
}

# ----- Start -----

# The service may not make its own event log source.
if (-not [Diagnostics.EventLog]::SourceExists('Thermostat.Server')) {
    [Diagnostics.EventLog]::CreateEventSource('Thermostat.Server', 'Application')
}
Invoke-Native sc.exe config $name start= auto
Invoke-Native sc.exe failure $name reset= 86400 actions= restart/5000/restart/30000/restart/60000
Invoke-Native sc.exe failureflag $name 1

Start-Service $name
$ok = $false
foreach ($try in 1..15) {
    try {
        if ((Invoke-RestMethod "http://127.0.0.1:$port/api/v1/health" -TimeoutSec 2).status -eq 'ok') { $ok = $true; break }
    }
    catch { }
    Start-Sleep 1
}
if (-not $ok) {
    throw "The service did not answer on port $port. See Event Viewer, Windows Logs, Application. The previous program, if any, is in $oldApp."
}
if (Test-Path $oldApp) { Remove-Item $oldApp -Recurse -Force }

Write-Host ''
Write-Host "The thermostat server is running and starts with Windows. Data: $data"
Write-Host "No firewall rule was changed. Thermostats need inbound TCP $port allowed, on Private networks only, for:"
Write-Host "  $exe"
if ($CopyFrom) { Write-Host "The readings and token were copied. $CopyFrom is now an old copy: do not run a server from it." }
