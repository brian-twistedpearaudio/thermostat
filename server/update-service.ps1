#Requires -Version 5.1
# Rebuilds the installed service from this folder. Data is kept.
Set-StrictMode -Version 2
$ErrorActionPreference = 'Stop'

if (-not (Get-Service 'ThermostatServer' -ErrorAction SilentlyContinue)) {
    throw 'The service is not installed. Run install-service.ps1 first.'
}
& (Join-Path $PSScriptRoot 'install-service.ps1')
