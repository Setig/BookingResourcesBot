[CmdletBinding(DefaultParameterSetName = 'Cell')]
param(
    [Parameter(ParameterSetName = 'Cell')]
    [ValidateSet('24.04', '26.04')]
    [string] $Ubuntu = '24.04',

    [Parameter(ParameterSetName = 'Cell')]
    [ValidateSet('5', '6')]
    [string] $Qt = '6',

    [Parameter(ParameterSetName = 'All')]
    [switch] $All
)

$ErrorActionPreference = 'Stop'

if (-not (Get-Command docker -ErrorAction SilentlyContinue)) {
    throw 'docker was not found in PATH. Start Docker Desktop and retry.'
}

$repoRoot = Split-Path -Parent $PSScriptRoot
$env:DOCKER_BUILDKIT = '1'

function Invoke-UbuntuDebBuild {
    param(
        [Parameter(Mandatory = $true)]
        [string] $UbuntuVersion,

        [Parameter(Mandatory = $true)]
        [string] $QtMajor
    )

    $destRel = "dist/ubuntu-$UbuntuVersion-qt$QtMajor"
    $dest = Join-Path $repoRoot $destRel
    if (Test-Path -LiteralPath $dest) {
        Remove-Item -LiteralPath $dest -Recurse -Force
    }

    Write-Host "Building Ubuntu $UbuntuVersion / Qt$QtMajor -> $destRel"
    docker build `
        -f (Join-Path $PSScriptRoot 'Dockerfile') `
        --build-arg "UBUNTU_VERSION=$UbuntuVersion" `
        --build-arg "QT_MAJOR=$QtMajor" `
        --target export `
        --output "type=local,dest=$dest" `
        $repoRoot
    if ($LASTEXITCODE -ne 0) {
        throw "docker build failed for Ubuntu $UbuntuVersion / Qt$QtMajor."
    }
}

if ($All) {
    foreach ($ubuntuVersion in @('24.04', '26.04')) {
        foreach ($qtMajor in @('5', '6')) {
            Invoke-UbuntuDebBuild -UbuntuVersion $ubuntuVersion -QtMajor $qtMajor
        }
    }
}
else {
    Invoke-UbuntuDebBuild -UbuntuVersion $Ubuntu -QtMajor $Qt
}
