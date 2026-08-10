$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot
$javaHome = $env:JAVA_HOME
if ([string]::IsNullOrWhiteSpace($javaHome)) {
    $javaHome = 'C:\Users\woan\.vscode\extensions\redhat.java-1.55.0-win32-x64\jre\21.0.11-win32-x86_64'
}
$javac = Join-Path $javaHome 'bin\javac.exe'
$java = Join-Path $javaHome 'bin\java.exe'
if (-not (Test-Path -LiteralPath $javac) -or -not (Test-Path -LiteralPath $java)) {
    throw 'A Java 17+ runtime with javac is required'
}

$output = Join-Path $env:TEMP 'find-momo-java-host'
New-Item -ItemType Directory -Force -Path $output | Out-Null
& $javac -encoding UTF-8 -d $output `
    (Join-Path $repoRoot 'phone_app/android/app/src/main/java/com/huangshan/badge/FindMomoState.java') `
    (Join-Path $PSScriptRoot 'android_find_momo_state_host/FindMomoStateHostTest.java')
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $java -cp $output com.huangshan.badge.FindMomoStateHostTest
exit $LASTEXITCODE
