#Requires -Version 5.1
<#
.SYNOPSIS
  Runs a numbered Oracle migration (e.g. 273.sql) with SQL*Plus — no copy/paste of SQL into a client.

.DESCRIPTION
  Set $env:SWG_ORACLE_CONNECT to "user/password@TNS" (or pass -Connect).
  Optionally set $env:ORACLE_HOME so sqlplus.exe is found under ORACLE_HOME\bin.

.EXAMPLE
  $env:SWG_ORACLE_CONNECT = 'swg/swg@localhost/XE'
  .\Apply-OracleUpdate.ps1 -Version 273
#>
param(
    [Parameter(Mandatory = $false)]
    [int] $Version = 273,

    [Parameter(Mandatory = $false)]
    [string] $Connect = $env:SWG_ORACLE_CONNECT,

    [Parameter(Mandatory = $false)]
    [string] $OracleHome = $env:ORACLE_HOME
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($Connect)) {
    Write-Error "Missing connection string. Set SWG_ORACLE_CONNECT to user/password@TNS or pass -Connect."
}

$sqlFile = Join-Path $PSScriptRoot ("..\updates\{0}.sql" -f $Version)
$sqlFile = [System.IO.Path]::GetFullPath($sqlFile)
if (-not (Test-Path -LiteralPath $sqlFile)) {
    Write-Error "Migration file not found: $sqlFile"
}

$sqlplus = $null
if (-not [string]::IsNullOrWhiteSpace($OracleHome)) {
    $cand = Join-Path $OracleHome 'bin\sqlplus.exe'
    if (Test-Path -LiteralPath $cand) { $sqlplus = $cand }
}
if (-not $sqlplus) {
    $cmd = Get-Command sqlplus.exe -ErrorAction SilentlyContinue
    if ($cmd) { $sqlplus = $cmd.Source }
}
if (-not $sqlplus) {
    Write-Error "sqlplus.exe not found. Install Oracle SQL*Plus / Instant Client or set ORACLE_HOME."
}

Write-Host "Using: $sqlplus"
Write-Host "Applying migration $Version : $sqlFile"

& $sqlplus $Connect "@$sqlFile"
if ($LASTEXITCODE -ne 0) {
    Write-Error "sqlplus exited with code $LASTEXITCODE"
}
Write-Host "Done."
