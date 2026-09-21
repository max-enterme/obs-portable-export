$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$wslRoot = (wsl -d Ubuntu-24.04 -u max wslpath -a ($root -replace '\\', '/')).Trim()
wsl -d Ubuntu-24.04 -u max bash -lc "set -e; cd '$wslRoot'; uvx --from cmake --with ninja cmake -G Ninja -S core -B build-core-test -DCMAKE_BUILD_TYPE=Debug -DPORTABLE_CORE_TESTS=ON; uvx --from cmake --with ninja cmake --build build-core-test; uvx --from cmake ctest --test-dir build-core-test --output-on-failure -V"
exit $LASTEXITCODE
