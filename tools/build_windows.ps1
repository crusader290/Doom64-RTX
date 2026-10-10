param([switch]$Help, [switch]$SetupOnly)
$ErrorActionPreference = 'Stop'
if ($Help) {
    Write-Host 'build.bat [-SetupOnly] : download portable MSYS2 dependencies and build Windows x64.'
    Write-Host 'For Linux and Windows cross builds, run ./build.sh inside Ubuntu/WSL.'
    exit 0
}
$repoPath = Split-Path $PSScriptRoot -Parent
$depsPath = Join-Path $repoPath 'build-deps'
$msysPath = Join-Path $depsPath 'msys64'
$bashPath = Join-Path $msysPath 'usr\bin\bash.exe'
New-Item -ItemType Directory -Force -Path $depsPath | Out-Null
if (!(Test-Path -LiteralPath $bashPath)) {
    Write-Host 'Downloading portable MSYS2 from its official GitHub release...'
    $releases = Invoke-RestMethod 'https://api.github.com/repos/msys2/msys2-installer/releases?per_page=20'
    $release = $releases | Where-Object { !$_.prerelease -and $_.tag_name -match '^\d{4}-\d{2}-\d{2}$' } | Select-Object -First 1
    $asset = $release.assets | Where-Object name -Match '^msys2-base-x86_64-\d+\.sfx\.exe$' | Select-Object -First 1
    if (!$asset) { throw 'The MSYS2 release has no supported x64 portable archive.' }
    $archive = Join-Path $depsPath $asset.name
    Invoke-WebRequest -UseBasicParsing $asset.browser_download_url -OutFile $archive
    if ($asset.digest -notmatch '^sha256:([0-9a-f]{64})$') { throw 'The official asset has no SHA256 digest.' }
    $expected = $Matches[1]
    if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $expected) { throw 'MSYS2 SHA256 verification failed.' }
    & $archive '-y' ("-o" + $depsPath)
    if ($LASTEXITCODE -ne 0 -or !(Test-Path -LiteralPath $bashPath)) { throw 'MSYS2 extraction failed.' }
}
$env:MSYSTEM = 'MINGW64'
$env:CHERE_INVOKING = '1'
# Keep the checkout path out of shell source: pass it as a positional argument.
& $bashPath '--login' '-c' 'pacman -Syu --noconfirm'
if ($LASTEXITCODE -ne 0) { throw 'MSYS2 update stopped. Run build.bat again to finish the core update in a fresh shell.' }
& $bashPath '--login' '-c' 'pacman -Syu --noconfirm && pacman -S --noconfirm --needed git mingw-w64-x86_64-gcc mingw-w64-x86_64-cmake mingw-w64-x86_64-ninja mingw-w64-x86_64-rust mingw-w64-x86_64-sdl3 mingw-w64-x86_64-python'
if ($LASTEXITCODE -ne 0) { throw 'Dependency installation failed. Run build.bat again after resolving the reported network/package error.' }
if ($SetupOnly) { exit 0 }
& $bashPath '--login' '-c' 'set -e; cd "$(cygpath -u "$1")"; cmake -S . -B build-native-win -G Ninja -DCMAKE_BUILD_TYPE=Release; cmake --build build-native-win --parallel; cp /mingw64/bin/SDL3.dll build-native-win/; for dll in libgcc_s_seh-1.dll libwinpthread-1.dll; do if test -f "/mingw64/bin/$dll"; then cp "/mingw64/bin/$dll" build-native-win/; fi; done' 'build-windows' $repoPath
exit $LASTEXITCODE
