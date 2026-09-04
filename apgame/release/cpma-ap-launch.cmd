@echo off
cd /d "%~dp0"
if not exist "baseq3\pak0.pk3" (
    echo Missing baseq3\pak0.pk3. See docs\setup_en.md.
    pause
    exit /b 1
)
if not exist "cpma\z-cpma-pak153.pk3" (
    echo CPMA 1.53 is required. See docs\setup_en.md.
    pause
    exit /b 1
)
start "Quake III Archipelago CPMA" "%~dp0quake3e.x64.exe" +set cl_renderer vulkan +set r_fullscreen 0 +set fs_basegame baseq3/cpma +set fs_game cpma-ap +set sv_pure 0 +set cg_predictItems 0 +set vm_game 2 +set vm_cgame 2 +set vm_ui 0 +setu osp_client 20231024
