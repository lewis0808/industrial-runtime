# 一次性配置 + 构建脚本（在 MSVC 环境里跑）。
# 用法: powershell -File tools/run-build.ps1
$ErrorActionPreference = "Stop"

$vsRoot = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"
$vcvars = "$vsRoot\VC\Auxiliary\Build\vcvars64.bat"
$cmake  = "$vsRoot\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
$repo   = "C:\Users\Dao\Code\changthink\industrial-runtime"
$build  = Join-Path $repo "cmake-build-release"

# 1) 进入 MSVC 环境，把 VCPKG_ROOT 设好，跑 cmake configure + build。
$cmd = @"
call `"$vcvars`" || exit /b 1
set VCPKG_ROOT=C:\vcpkg
set VCPKG_USE_SYSTEM_BINARIES=1
cd /d `"$repo`"
rmdir /s /q `"$build`" 2>nul
`"$cmake`" -S . -B `"$build`" -G `"Ninja`" -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake -DCMAKE_BUILD_TYPE=Release || exit /b 1
`"$cmake`" --build `"$build`" --config Release --parallel || exit /b 1
"@

& cmd /c $cmd
$rc = $LASTEXITCODE
if ($rc -ne 0) { Write-Error "构建失败 (exit $rc)"; exit $rc }
Write-Host "=== 构建完成 ==="
Write-Host "产物: $build\IndustrialRuntime.exe"
