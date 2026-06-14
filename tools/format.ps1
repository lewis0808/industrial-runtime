#requires -Version 5
# Windows 下格式化全部受 git 跟踪的 C/C++ 源码（对应 tools/format.sh）。
# 用法：在仓库任意位置执行  pwsh tools/format.ps1  或  powershell -File tools\format.ps1

$ErrorActionPreference = 'Stop'

# 定位 clang-format：优先 PATH，其次常见安装路径。
$clangFormat = $null
$cmd = Get-Command clang-format -ErrorAction SilentlyContinue
if ($cmd) {
    $clangFormat = $cmd.Source
} else {
    $candidates = @(
        'G:\cpp_depend\llvm\bin\clang-format.exe',
        'G:\cpp_depend\clangd_18.1.3\bin\clang-format.exe'
    )
    foreach ($c in $candidates) {
        if (Test-Path $c) { $clangFormat = $c; break }
    }
}
if (-not $clangFormat) {
    Write-Error 'clang-format 未找到，请安装或加入 PATH 后再执行。'
    exit 1
}

# 仓库根（脚本位于 tools/ 下）。
$repoRoot = Resolve-Path (Join-Path $PSScriptRoot '..')
Push-Location $repoRoot
try {
    # 受 git 跟踪的 C/C++ 文件。强制为数组，便于计数与 splat。
    $files = @(git ls-files '*.c' '*.cc' '*.cpp' '*.cxx' '*.h' '*.hh' '*.hpp')
    if ($files.Count -eq 0) {
        Write-Host '没有找到需要格式化的 C/C++ 源码文件。'
        exit 0
    }
    & $clangFormat -i @files
    if ($LASTEXITCODE -ne 0) {
        Write-Error "clang-format 执行失败（退出码 $LASTEXITCODE）。"
        exit $LASTEXITCODE
    }
    Write-Host "已格式化 $($files.Count) 个文件。"
} finally {
    Pop-Location
}
