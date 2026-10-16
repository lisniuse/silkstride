param(
    [ValidateSet('motion', 'interaction', 'behavior', 'feeding', 'i18n', 'startup')]
    [string[]]$Suite = @('motion', 'interaction', 'behavior', 'feeding', 'i18n', 'startup')
)

$ErrorActionPreference = 'Stop'
$executable = Join-Path $PSScriptRoot 'build/Silkstride.exe'
$reports = Join-Path $PSScriptRoot 'out/tests'
if (!(Test-Path -LiteralPath $executable -PathType Leaf)) {
    throw 'Build first / 请先编译: pwsh -NoProfile -File ./build.ps1'
}
if (Get-Process Silkstride, NeonSpider -ErrorAction SilentlyContinue) {
    throw 'Exit the running pet before tests / 测试前请先退出宠物。'
}
New-Item -ItemType Directory -Path $reports -Force | Out-Null
foreach ($name in $Suite) {
    $report = Join-Path $reports "$name-test.txt"
    # Remove only this known generated report, so an old PASS cannot mask a
    # process that exited without running its diagnostic (e.g. another instance).
    if (Test-Path -LiteralPath $report) { Remove-Item -LiteralPath $report }
    $process = Start-Process -FilePath $executable -ArgumentList "--$name-test" -WorkingDirectory $reports -Wait -PassThru
    if (!(Test-Path -LiteralPath $report -PathType Leaf)) { throw "No report generated / 未生成报告: $name" }
    Get-Content -LiteralPath $report
    if ($process.ExitCode -ne 0) { throw "$name test failed / 测试失败: $($process.ExitCode)" }
}
Write-Host "Reports / 测试结果: $reports"
