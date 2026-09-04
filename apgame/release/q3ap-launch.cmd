@echo off
cd /d "%~dp0"
if not exist "baseq3\pak0.pk3" (
    echo Missing baseq3\pak0.pk3. See docs\setup_en.md.
    pause
    exit /b 1
)
start "Quake III Archipelago" "%~dp0quake3e.x64.exe" +set cl_renderer vulkan +set r_fullscreen 0 +set fs_basegame baseq3 +set fs_game q3ap +set vm_game 0 +set vm_cgame 2 +set vm_ui 0
