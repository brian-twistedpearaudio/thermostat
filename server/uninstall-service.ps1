#Requires -Version 5.1
# Removes the service and its program files. The data folder is kept.
Set-StrictMode -Version 2
$ErrorActionPreference = 'Stop'

$name = 'ThermostatServer'
$root = Join-Path ([Environment]::GetFolderPath('ProgramFiles')) 'Thermostat Server'
$data = Join-Path ([Environment]::GetFolderPath('CommonApplicationData')) $name

$me = New-Object Security.Principal.WindowsPrincipal ([Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $me.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Run this in PowerShell opened with "Run as administrator".'
}

$service = Get-Service $name -ErrorAction SilentlyContinue
if ($service) {
    Stop-Service $name
    $service.WaitForStatus('Stopped', [TimeSpan]::FromSeconds(60))
    $service.Dispose()
    sc.exe delete $name | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "sc.exe delete failed with exit code $LASTEXITCODE." }
}
if (Test-Path $root) { Remove-Item $root -Recurse -Force }
if ([Diagnostics.EventLog]::SourceExists('Thermostat.Server')) { [Diagnostics.EventLog]::DeleteEventSource('Thermostat.Server') }

Write-Host "The service is removed. Readings and token are kept in $data"
Write-Host 'No firewall rule was changed. Remove any rule you added for it.'
