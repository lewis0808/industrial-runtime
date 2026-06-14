#requires -Version 5
# Windows 下一次性运行 clang-format（就地格式化）+ clang-tidy（静态检查）。
# 用法：
#   powershell -File tools\lint.ps1                  # 默认 build 目录 cmake-build-debug
#   powershell -File tools\lint.ps1 -BuildDir <dir>  # 指定 build 目录（需含 compile_commands.json）
#   powershell -File tools\lint.ps1 -NoTidy          # 只格式化
#   powershell -File tools\lint.ps1 -NoFormat        # 只静态检查

param(
    [string]$BuildDir = 'cmake-build-debug',
    [switch]$NoFormat,
    [switch]$NoTidy
)

$ErrorActionPreference = 'Stop'

function Resolve-Tool([string]$name, [string[]]$fallbacks) {
    $cmd = Get-Command $name -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    foreach ($f in $fallbacks) { if (Test-Path $f) { return $f } }
    return $null
}

$clangFormat = Resolve-Tool 'clang-format' @('G:\cpp_depend\llvm\bin\clang-format.exe')
$clangTidy = Resolve-Tool 'clang-tidy' @('G:\cpp_depend\llvm\bin\clang-tidy.exe')

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot '..')
Push-Location $repoRoot
try {
    $files = @(git ls-files '*.c' '*.cc' '*.cpp' '*.cxx' '*.h' '*.hh' '*.hpp')
    if ($files.Count -eq 0) {
        Write-Host '没有找到 C/C++ 源码文件。'
        exit 0
    }

    # ---- clang-format ----
    if (-not $NoFormat) {
        if (-not $clangFormat) { Write-Error 'clang-format 未找到，请安装或加入 PATH。'; exit 1 }
        Write-Host "== clang-format：格式化 $($files.Count) 个文件 =="
        & $clangFormat -i @files
        if ($LASTEXITCODE -ne 0) { Write-Error "clang-format 失败（退出码 $LASTEXITCODE）。"; exit $LASTEXITCODE }
    }

    # ---- clang-tidy ----
    if (-not $NoTidy) {
        if (-not $clangTidy) { Write-Error 'clang-tidy 未找到，请安装或加入 PATH。'; exit 1 }
        $compileDb = Join-Path $BuildDir 'compile_commands.json'
        if (-not (Test-Path $compileDb)) {
            Write-Error "未找到 $compileDb。请先用 CMAKE_EXPORT_COMPILE_COMMANDS=ON 配置（重配 $BuildDir）。"
            exit 1
        }
        # 仅对编译单元（.c/.cc/.cpp/.cxx）运行；头文件经其 includer 覆盖。
        $sources = @($files | Where-Object { $_ -match '\.(c|cc|cpp|cxx)$' })
        Write-Host "== clang-tidy：检查 $($sources.Count) 个编译单元（-p $BuildDir）=="
        $withFindings = 0
        foreach ($src in $sources) {
            & $clangTidy -p $BuildDir --quiet $src
            if ($LASTEXITCODE -ne 0) { $withFindings++ }
        }
        if ($withFindings -gt 0) {
            Write-Host "clang-tidy：$withFindings 个文件报告了问题（见上）。"
        } else {
            Write-Host 'clang-tidy：无问题。'
        }
    }

    Write-Host 'lint 完成。'
} finally {
    Pop-Location
}
