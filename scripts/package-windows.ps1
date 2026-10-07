#Requires -Version 5.1

<#
.SYNOPSIS
Build, test, deploy Qt dependencies, and create a Windows portable release ZIP.
.EXAMPLE
.\scripts\package-windows.ps1 -QtRoot C:\Qt\6.8.3\msvc2022_64
.EXAMPLE
.\scripts\package-windows.ps1 -QtRoot C:\Qt\6.8.3\msvc2022_64 -CertificateThumbprint <SHA1>
#>
[CmdletBinding()]
param(
    [string]$QtRoot = $env:QT_ROOT,
    [string]$OutputDir,
    [ValidateSet('x64', 'arm64')][string]$Architecture = 'x64',
    [ValidateRange(1, 256)][int]$Jobs = 4,
    [switch]$SkipTests,
    [string]$CertificateThumbprint,
    [string]$TimestampUrl = 'http://timestamp.digicert.com'
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$stage = $null

# PowerShell 5.1 不会自动将原生命令的非零退出码转换为异常。
function Invoke-Native {
    param([string]$Program, [string[]]$Arguments)
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Program failed (exit code $LASTEXITCODE)." }
}

function Initialize-Msvc {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path -LiteralPath $vswhere) {
        $installation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if ($LASTEXITCODE -ne 0) { throw 'vswhere failed.' }
        if ($installation) {
            $devCmd = Join-Path $installation 'Common7\Tools\VsDevCmd.bat'
            # 捕获环境后只导入进当前脚本进程，不修改系统 PATH，也不打印环境变量。
            # 不用 cmd /s + 手工拼 ""path"...：PowerShell 再传参时会把带空格的 VS 路径拆坏。
            $environmentLines = & $env:ComSpec /d /c "`"$devCmd`" -no_logo -arch=$Architecture -host_arch=x64 >nul && set"
            if ($LASTEXITCODE -ne 0) { throw 'Could not initialize the MSVC environment.' }
            foreach ($line in $environmentLines) {
                if ($line -match '^([^=]+)=(.*)$') {
                    [Environment]::SetEnvironmentVariable($Matches[1], $Matches[2], 'Process')
                }
            }
        }
    }
    if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
        throw 'Install Visual Studio 2022 C++ build tools, or run from its Developer PowerShell.'
    }
    if ($env:VSCMD_ARG_TGT_ARCH -and $env:VSCMD_ARG_TGT_ARCH -ne $Architecture) {
        throw "MSVC architecture does not match $Architecture."
    }
}

try {
    if ($env:OS -ne 'Windows_NT') { throw 'Run this script on Windows.' }
    if (-not $QtRoot) { throw 'Provide -QtRoot <Qt MSVC SDK> or set QT_ROOT.' }
    $QtRoot = (Resolve-Path -LiteralPath $QtRoot).Path
    if (-not (Test-Path -LiteralPath (Join-Path $QtRoot 'lib\cmake\Qt6\Qt6Config.cmake'))) {
        throw 'Qt SDK is invalid.'
    }
    if (-not (Test-Path -LiteralPath (Join-Path $QtRoot 'bin\windeployqt.exe'))) {
        throw 'windeployqt.exe is missing from the Qt SDK.'
    }
    Initialize-Msvc
    foreach ($tool in @('cmake', 'ctest', 'ninja')) {
        if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) { throw "Required tool is missing: $tool" }
    }
    if ($CertificateThumbprint) {
        if (-not (Get-Command signtool.exe -ErrorAction SilentlyContinue)) { throw 'signtool.exe is missing from the Windows SDK.' }
        if ($CertificateThumbprint -notmatch '^[a-fA-F0-9]{40}$') { throw 'CertificateThumbprint must be the SHA-1 certificate thumbprint.' }
    }
    if (-not $OutputDir) { $OutputDir = Join-Path $root 'dist' }
    if (-not [IO.Path]::IsPathRooted($OutputDir)) { $OutputDir = Join-Path (Get-Location).Path $OutputDir }
    $OutputDir = [IO.Path]::GetFullPath($OutputDir)
    New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null
    $stage = Join-Path $OutputDir ('.qding-windows-' + [Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $stage | Out-Null
    $buildDir = Join-Path $root "build\release-windows-$Architecture"
    $testing = if ($SkipTests) { 'OFF' } else { 'ON' }
    Invoke-Native cmake @('--fresh', '-S', $root, '-B', $buildDir, '-G', 'Ninja',
        '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_CXX_COMPILER=cl.exe', "-DBUILD_TESTING=$testing", "-DCMAKE_PREFIX_PATH=$QtRoot")
    Invoke-Native cmake @('--build', $buildDir, '--parallel', "$Jobs")
    if (-not $SkipTests) {
        # 构建目录里的测试未部署 Qt DLL，只在 ctest 期间从 SDK 加载。
        $savedTestPath = $env:PATH
        try {
            $env:PATH = (Join-Path $QtRoot 'bin') + ';' + $env:PATH
            Invoke-Native ctest @('--test-dir', $buildDir, '--output-on-failure')
        }
        finally { $env:PATH = $savedTestPath }
    }
    $versionLine = Select-String -LiteralPath (Join-Path $buildDir 'CMakeCache.txt') -Pattern '^CMAKE_PROJECT_VERSION:STATIC=(\d+\.\d+\.\d+)$'
    if (-not $versionLine) { throw 'Cannot read project version.' }
    $version = $versionLine.Matches[0].Groups[1].Value
    $name = "qDing-$version-Windows-$Architecture"
    $package = Join-Path $stage $name
    Invoke-Native cmake @('--install', $buildDir, '--prefix', $package, '--config', 'Release')
    $executable = Join-Path $package 'bin\qDing.exe'
    if (-not (Test-Path -LiteralPath $executable)) { throw 'Installed executable is missing.' }
    $binPlugins = Join-Path $package 'bin\plugins\platforms'
    if (-not (Test-Path -LiteralPath $binPlugins)) { throw 'Deployed plugins are missing under bin\plugins.' }
    foreach ($legacy in @('plugins', 'translations')) {
        if (Test-Path -LiteralPath (Join-Path $package $legacy)) {
            throw "Unexpected top-level $legacy directory; plugins and translations must live under bin\."
        }
    }

    # 部署后自洽检查（详见 docs/qt-plugins.zh-CN.md）：
    # - 从 PATH 去掉 SDK bin，避免加载开发机 Qt6*.dll；
    # - 清除 QT_PLUGIN_PATH / QT_QPA_PLATFORM_PLUGIN_PATH，避免 SDK plugins 掩盖漏打包；
    # - 强制 offscreen：验证包内 platforms/qoffscreen（CMake 通过 --include-plugins 纳入）。
    $savedPath = $env:PATH
    $savedPluginPath = $env:QT_PLUGIN_PATH
    $savedPlatformPath = $env:QT_QPA_PLATFORM_PLUGIN_PATH
    $savedPlatform = $env:QT_QPA_PLATFORM
    try {
        $qtBin = (Join-Path $QtRoot 'bin').TrimEnd('\')
        $env:PATH = ($env:PATH -split ';' | Where-Object { $_.Trim('"').TrimEnd('\') -ne $qtBin }) -join ';'
        $env:QT_PLUGIN_PATH = $null
        $env:QT_QPA_PLATFORM_PLUGIN_PATH = $null
        $env:QT_QPA_PLATFORM = 'offscreen'
        $process = Start-Process -FilePath $executable -ArgumentList '--smoke-test' -WorkingDirectory (Split-Path $executable) -PassThru
        try {
            # 先缓存句柄，兼容 PowerShell 5.1 的 Start-Process 退出码读取。
            $null = $process.Handle
            if (-not $process.WaitForExit(20000)) {
                $process.Kill()
                $process.WaitForExit()
                throw 'Deployed app smoke check timed out.'
            }
            $process.WaitForExit()
            if ($process.ExitCode -ne 0) { throw "Deployed app smoke check failed: $($process.ExitCode)" }
        }
        finally { $process.Dispose() }
    }
    finally {
        $env:PATH = $savedPath
        $env:QT_PLUGIN_PATH = $savedPluginPath
        $env:QT_QPA_PLATFORM_PLUGIN_PATH = $savedPlatformPath
        $env:QT_QPA_PLATFORM = $savedPlatform
    }
    if ($CertificateThumbprint) {
        Invoke-Native signtool.exe @('sign', '/sha1', $CertificateThumbprint, '/fd', 'SHA256',
            '/tr', $TimestampUrl, '/td', 'SHA256', $executable)
        Invoke-Native signtool.exe @('verify', '/pa', $executable)
    }
    @"
qDing $version ($Architecture)
解压整个目录后，双击 bin\qDing.exe 启动。不要只复制 EXE。
数据保存在当前用户目录，关闭窗口后从托盘退出才能完全结束程序。
"@ | Set-Content -LiteralPath (Join-Path $package '运行说明.txt') -Encoding UTF8
    # .NET ZipFile 避免 Compress-Archive 对隐藏文件的忽略，保留完整部署目录。
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip = Join-Path $stage "$name.zip"
    [IO.Compression.ZipFile]::CreateFromDirectory($package, $zip, [IO.Compression.CompressionLevel]::Optimal, $true)
    $hash = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash.ToLowerInvariant()
    $checksum = Join-Path $stage "$name.sha256"
    "$hash  $name.zip" | Set-Content -LiteralPath $checksum -Encoding ASCII
    Move-Item -LiteralPath $zip -Destination (Join-Path $OutputDir "$name.zip") -Force
    Move-Item -LiteralPath $checksum -Destination (Join-Path $OutputDir "$name.sha256") -Force
    Write-Host "`nRelease artifacts:"
    Write-Host (Join-Path $OutputDir "$name.zip")
    Write-Host (Join-Path $OutputDir "$name.sha256")
}
catch {
    Write-Error -ErrorRecord $_ -ErrorAction Continue
    exit 1
}
finally {
    # 只移除本次创建的唯一暂存目录；已有发布 ZIP 不受失败影响。
    if ($stage -and (Test-Path -LiteralPath $stage)) { Remove-Item -LiteralPath $stage -Recurse -Force }
}
