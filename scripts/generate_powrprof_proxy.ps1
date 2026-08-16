param(
    [Parameter(Mandatory = $true)]
    [string]$OutputDir
)

$ErrorActionPreference = 'Stop'

New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null

$systemDll = Join-Path $env:SystemRoot 'System32\powrprof.dll'
if (-not (Test-Path $systemDll)) {
    throw "System PowrProf.dll not found: $systemDll"
}

$dump = & dumpbin /nologo /exports $systemDll
if ($LASTEXITCODE -ne 0) {
    throw 'dumpbin /exports failed'
}

$exports = @()
foreach ($line in $dump) {
    if ($line -match '^\s*(\d+)\s+([0-9A-Fa-f]+)\s+([0-9A-Fa-f]+)\s+(\S+)(?:\s*=.*)?$') {
        $ordinal = [int]$Matches[1]
        $name = $Matches[4]
        if ($name -eq '[NONAME]') { continue }
        $exports += [pscustomobject]@{ Ordinal = $ordinal; Name = $name }
    }
}

if ($exports.Count -lt 20) {
    Write-Host ($dump -join "`n")
    throw "Unexpectedly small PowrProf export set: $($exports.Count)"
}

$exports = $exports | Sort-Object Ordinal, Name

$header = @('#pragma once', "#define POWRPROF_EXPORT_COUNT $($exports.Count)", 'static const char* const kPowrProfExportNames[POWRPROF_EXPORT_COUNT] = {')
foreach ($entry in $exports) { $header += ('    "' + $entry.Name + '",') }
$header += '};'
Set-Content -Path (Join-Path $OutputDir 'powrprof_exports_generated.h') -Value $header -Encoding ASCII

$def = @('LIBRARY powrprof', 'EXPORTS')
for ($i = 0; $i -lt $exports.Count; $i++) {
    $entry = $exports[$i]
    $stub = 'Proxy_{0:D3}' -f $i
    $def += "    $($entry.Name)=$stub @$($entry.Ordinal)"
}
$def += '    TrayFixMonitor'
Set-Content -Path (Join-Path $OutputDir 'powrprof_proxy.def') -Value $def -Encoding ASCII

$asm = @('OPTION CASEMAP:NONE', 'EXTERN TrayFixResolveExport:PROC', 'EXTERN g_powrprof_exports:QWORD', '.code')
for ($i = 0; $i -lt $exports.Count; $i++) {
    $stub = 'Proxy_{0:D3}' -f $i
    $ready = "${stub}_ready"
    $offset = $i * 8

    $asm += "$stub PROC"
    if ($offset -eq 0) { $asm += '    mov rax, QWORD PTR [g_powrprof_exports]' }
    else { $asm += "    mov rax, QWORD PTR [g_powrprof_exports+$offset]" }
    $asm += '    test rax, rax'
    $asm += "    jnz $ready"
    $asm += '    sub rsp, 088h'
    $asm += '    mov QWORD PTR [rsp+020h], rcx'
    $asm += '    mov QWORD PTR [rsp+028h], rdx'
    $asm += '    mov QWORD PTR [rsp+030h], r8'
    $asm += '    mov QWORD PTR [rsp+038h], r9'
    $asm += '    movdqu XMMWORD PTR [rsp+040h], xmm0'
    $asm += '    movdqu XMMWORD PTR [rsp+050h], xmm1'
    $asm += '    movdqu XMMWORD PTR [rsp+060h], xmm2'
    $asm += '    movdqu XMMWORD PTR [rsp+070h], xmm3'
    $asm += "    mov ecx, $i"
    $asm += '    call TrayFixResolveExport'
    $asm += '    mov r11, rax'
    $asm += '    movdqu xmm0, XMMWORD PTR [rsp+040h]'
    $asm += '    movdqu xmm1, XMMWORD PTR [rsp+050h]'
    $asm += '    movdqu xmm2, XMMWORD PTR [rsp+060h]'
    $asm += '    movdqu xmm3, XMMWORD PTR [rsp+070h]'
    $asm += '    mov rcx, QWORD PTR [rsp+020h]'
    $asm += '    mov rdx, QWORD PTR [rsp+028h]'
    $asm += '    mov r8, QWORD PTR [rsp+030h]'
    $asm += '    mov r9, QWORD PTR [rsp+038h]'
    $asm += '    add rsp, 088h'
    $asm += '    mov rax, r11'
    $asm += ($ready + ':')
    $asm += '    jmp rax'
    $asm += "$stub ENDP"
    $asm += ''
}
$asm += 'END'
Set-Content -Path (Join-Path $OutputDir 'powrprof_proxy.asm') -Value $asm -Encoding ASCII

Write-Host "Generated $($exports.Count) PowrProf exports"
$exports | Format-Table Ordinal, Name -AutoSize
