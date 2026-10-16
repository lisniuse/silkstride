$ErrorActionPreference = 'Stop'
$executable = Join-Path $PSScriptRoot 'build/Silkstride.exe'
if (!(Test-Path -LiteralPath $executable -PathType Leaf)) {
    throw 'Build first / 请先编译: pwsh -NoProfile -File ./build.ps1'
}
# Ask the Windows shell to launch the long-lived GUI pet. Keep synchronous
# Start-Process use for diagnostics in test.ps1, not normal pet launches.
$shell = New-Object -ComObject Shell.Application
try {
    $shell.ShellExecute($executable, '', (Split-Path -Parent $executable), 'open', 0)
} finally {
    [void][Runtime.InteropServices.Marshal]::ReleaseComObject($shell)
}
Write-Host 'Launch requested through Windows shell / 已通过 Windows Shell 启动。'
