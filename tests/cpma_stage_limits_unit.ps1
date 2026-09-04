$ErrorActionPreference = 'Stop'
$project = Split-Path $PSScriptRoot -Parent
$output = Join-Path $project 'build-ap-reuse\cpma-limits-unit'
New-Item -ItemType Directory -Force -Path $output | Out-Null
$source = Get-Content (Join-Path $project 'code\server\sv_ap_cpma.c') -Raw
$function = [regex]::Match($source, '(?s)qboolean SVAP_CPMA_ApplyStageLimits\(.*?\r?\n\}').Value
if (-not $function) { throw 'Stage-limit implementation not found' }
$harness = @'
#include <assert.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
typedef int qboolean;
#define qfalse 0
#define SS_GAME 2
#define SVAP_CPMA153_DEDICATED_INTEGER 0x34ea8u
static unsigned char data[0x34eacu];
static struct { unsigned char *dataBase; } vm = { data }, *gvm = &vm;
static struct { int state; } sv = { SS_GAME };
static int enabled = 1, layout = 1, calls, fail;
static char command[100];
static int SVAP_CPMA_IsEnabled(void) { return enabled; }
static int SVAP_CPMA153_HasLayout(void) { return layout; }
static int SVAP_CPMA153_VMInt(unsigned address, int *value) {
    memcpy(value, data + address, sizeof(*value)); return 1;
}
static char *va(const char *format, ...) {
    static char buffer[100]; va_list args; va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args); va_end(args); return buffer;
}
static void Cmd_TokenizeString(const char *text) { strcpy(command, text); }
static int SV_GameCommand(void) {
    int dedicated; SVAP_CPMA153_VMInt(SVAP_CPMA153_DEDICATED_INTEGER, &dedicated);
    assert(dedicated == 1);
    assert(!strcmp(command, calls++ ? "callvote timelimit 0 0" : "callvote limit 10 0"));
    return !fail;
}
#define Com_Printf(...) ((void)0)
'@
$checks = @'
int main(void) {
    int original, restored;
    for (original = 0; original <= 1; ++original) {
        for (fail = 0; fail <= 1; ++fail) {
            memcpy(data + SVAP_CPMA153_DEDICATED_INTEGER, &original, sizeof(original));
            calls = 0;
            assert(SVAP_CPMA_ApplyStageLimits(10) == !fail);
            assert(calls == 2);
            SVAP_CPMA153_VMInt(SVAP_CPMA153_DEDICATED_INTEGER, &restored);
            assert(restored == original);
        }
    }
    calls = 0; layout = 0; assert(!SVAP_CPMA_ApplyStageLimits(10));
    layout = 1; enabled = 0; assert(!SVAP_CPMA_ApplyStageLimits(10));
    enabled = 1; assert(!SVAP_CPMA_ApplyStageLimits(0));
    assert(calls == 0);
    puts("PASS: listen/dedicated dispatch, restoration on success/failure, and layout guards.");
    return 0;
}
'@
Set-Content -LiteralPath (Join-Path $output 'test.c') -Value ($harness + "`n" + $function + "`n" + $checks)
$dev = 'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat'
Push-Location $output
try {
    & cmd /d /c "call `"$dev`" >nul && cl /nologo /W3 /D_CRT_SECURE_NO_WARNINGS test.c /Fe:test.exe"
    if ($LASTEXITCODE -ne 0) { throw 'Test compilation failed' }
    & .\test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Stage-limit regression failed' }
} finally { Pop-Location }
