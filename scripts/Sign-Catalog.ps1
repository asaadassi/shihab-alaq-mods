# Signs the store's catalog.json with the Shihab publisher key, on the publisher's computer (the key never leaves it).
# Checks every file the catalog names against its SHA-256 first, then commits catalog.json.sig to the store branch.
# Usage (from this repository, the "store" branch checked out next to it or fetched here):
#   powershell -ExecutionPolicy Bypass -File scripts\Sign-Catalog.ps1 -SigningKey D:\CodeLab\ShihabStudio\keys\shihab-update-signing.pem
param(
    [Parameter(Mandatory)][string]$SigningKey,
    [string]$Signer = (Join-Path $PSScriptRoot "..\..\ShihabShared\tools\Shihab.UpdateSigner")
)
$ErrorActionPreference = "Stop"
if (-not (Test-Path $SigningKey)) { throw "Signing key not found: $SigningKey" }
$root = Split-Path $PSScriptRoot -Parent
$work = Join-Path $root ".store"
if (Test-Path $work) { git -C $root worktree remove --force $work }
git -C $root fetch origin store
git -C $root worktree add $work origin/store --detach
try {
    $catalog = Get-Content (Join-Path $work "catalog.json") -Raw -Encoding UTF8 | ConvertFrom-Json
    if ($catalog.format -ne 1) { throw "Unexpected catalog format $($catalog.format)" }
    foreach ($m in $catalog.mods) {
        $files = @($m.source) + @($m.binaries.PSObject.Properties | ForEach-Object { $_.Value })
        foreach ($f in $files) {
            $p = Join-Path $work $f.path
            $h = (Get-FileHash $p -Algorithm SHA256).Hash.ToLowerInvariant()
            if ($h -ne $f.sha256.ToLowerInvariant()) { throw "$($f.path): SHA-256 doesn't match the catalog" }
        }
        Write-Host "ok  $($m.id) $($m.version)"
    }
    & dotnet run -c Release --project $Signer -- sign $SigningKey (Join-Path $work "catalog.json")
    if ($LASTEXITCODE -ne 0) { throw "Signing failed" }
    git -C $work add catalog.json.sig
    git -C $work commit -m "Sign catalog.json"
    git -C $work push origin HEAD:store
    Write-Host "Signed and published."
}
finally {
    git -C $root worktree remove --force $work
}
