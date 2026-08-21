# Install the latest VaultCLI release on Windows.

[CmdletBinding()]
param(
    [string]$Version = $(if ($env:VAULT_VERSION) { $env:VAULT_VERSION } else { "latest" })
)

$ErrorActionPreference = "Stop"
$repository = if ($env:VAULT_REPOSITORY) { $env:VAULT_REPOSITORY } else { "ZachFrechin/CVault" }
$installDirectory = if ($env:VAULT_INSTALL_DIR) {
    $env:VAULT_INSTALL_DIR
} else {
    Join-Path $env:LOCALAPPDATA "VaultCLI\bin"
}

if ($Version -eq "latest") {
    $releaseUrl = "https://github.com/$repository/releases/latest/download"
} else {
    if (-not $Version.StartsWith("v")) {
        $Version = "v$Version"
    }
    if ($Version -notmatch '^v[A-Za-z0-9._-]+$') {
        throw "La version demandee contient des caracteres invalides."
    }
    $releaseUrl = "https://github.com/$repository/releases/download/$Version"
}

$architecture = [System.Runtime.InteropServices.RuntimeInformation]::OSArchitecture.ToString()
switch ($architecture) {
    "X64" { $architecture = "x64" }
    "Arm64" { $architecture = "arm64" }
    default { throw "Architecture Windows non prise en charge : $architecture" }
}

$asset = "vault-windows-$architecture.zip"
$temporaryDirectory = Join-Path ([System.IO.Path]::GetTempPath()) ("vault-install-" + [guid]::NewGuid())
New-Item -ItemType Directory -Path $temporaryDirectory | Out-Null

try {
    $archivePath = Join-Path $temporaryDirectory $asset
    $checksumsPath = Join-Path $temporaryDirectory "SHA256SUMS.txt"
    Invoke-WebRequest -Uri "$releaseUrl/$asset" -OutFile $archivePath
    Invoke-WebRequest -Uri "$releaseUrl/SHA256SUMS.txt" -OutFile $checksumsPath

    $checksumLine = Get-Content $checksumsPath |
        ForEach-Object {
            $parts = $_ -split '\s+'
            if ($parts.Count -ge 2 -and
                $parts[0] -match '^[0-9a-fA-F]{64}$' -and
                $parts[1] -eq $asset) {
                $_
            }
        } |
        Select-Object -First 1
    if (-not $checksumLine) {
        throw "Aucune somme de controle pour $asset."
    }
    $expectedChecksum = ($checksumLine -split '\s+')[0].ToLowerInvariant()
    $actualChecksum = (Get-FileHash -Algorithm SHA256 -LiteralPath $archivePath).Hash.ToLowerInvariant()
    if ($expectedChecksum -ne $actualChecksum) {
        throw "La somme SHA-256 ne correspond pas."
    }

    $extractedDirectory = Join-Path $temporaryDirectory "extracted"
    Expand-Archive -LiteralPath $archivePath -DestinationPath $extractedDirectory
    $binaryPath = Join-Path $extractedDirectory "bin\vault.exe"
    if (-not (Test-Path -LiteralPath $binaryPath -PathType Leaf)) {
        throw "Le binaire vault.exe est absent de l archive."
    }

    New-Item -ItemType Directory -Force -Path $installDirectory | Out-Null
    Copy-Item -LiteralPath $binaryPath -Destination (Join-Path $installDirectory "vault.exe") -Force

    $userPath = [Environment]::GetEnvironmentVariable("Path", "User")
    if (-not $userPath) {
        $userPath = ""
    }
    $pathEntries = $userPath -split ';' | Where-Object { $_ }
    if ($pathEntries -notcontains $installDirectory) {
        $newPath = if ($userPath) { "$userPath;$installDirectory" } else { $installDirectory }
        [Environment]::SetEnvironmentVariable("Path", $newPath, "User")
    }
    $env:Path = "$installDirectory;$env:Path"

    Write-Output "vault.exe installe dans $installDirectory"
    Write-Output "Ouvrez un nouveau terminal pour utiliser la commande vault."
}
finally {
    Remove-Item -LiteralPath $temporaryDirectory -Recurse -Force -ErrorAction SilentlyContinue
}
