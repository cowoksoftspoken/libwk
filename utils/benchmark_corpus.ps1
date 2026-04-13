param(
    [string]$InputDir = "photos",
    [float]$Quality = 75,
    [ValidateSet("444", "420")]
    [string]$Subsampling = "444",
    [switch]$Lossless,
    [string]$OutputDir = "benchmark"
)

$RepoRoot = Split-Path -Parent $PSScriptRoot
$wkenc = Join-Path $RepoRoot "build\wkenc.exe"
$wkmetric = Join-Path $RepoRoot "build\wkmetric.exe"

if (!(Test-Path $wkenc)) {
    throw "wkenc.exe not found at $wkenc"
}
if (!(Test-Path $wkmetric)) {
    throw "wkmetric.exe not found at $wkmetric"
}

$photos = Get-ChildItem -Path (Join-Path $RepoRoot $InputDir) -Filter *.jpg | Sort-Object Name
if ($photos.Count -eq 0) {
    throw "No .jpg files found in $InputDir"
}

New-Item -ItemType Directory -Force -Path (Join-Path $RepoRoot $OutputDir) | Out-Null
$profile = if ($Lossless) { "lossless" } else { "q$([int][Math]::Round($Quality))_yuv$Subsampling" }
$rows = @()

foreach ($photo in $photos) {
    $stem = [System.IO.Path]::GetFileNameWithoutExtension($photo.Name)
    $output = Join-Path (Join-Path $RepoRoot $OutputDir) ("{0}_{1}.wk" -f $stem, $profile)
    $args = @()
    if ($Lossless) {
        $args += "--lossless"
    } else {
        $args += "--quality"
        $args += ("{0}" -f $Quality)
        if ($Subsampling -eq "420") {
            $args += "--yuv420"
        } else {
            $args += "--yuv444"
        }
    }
    $args += $photo.FullName
    $args += $output

    & $wkenc @args
    if ($LASTEXITCODE -ne 0) {
        throw "wkenc failed for $($photo.Name)"
    }

    $json = & $wkmetric --json $photo.FullName $output
    if ($LASTEXITCODE -ne 0) {
        throw "wkmetric failed for $($photo.Name)"
    }

    $metric = $json | ConvertFrom-Json
    $rows += [pscustomobject]@{
        image = $photo.Name
        profile = $profile
        source_bytes = [int64]$metric.reference_bytes
        wk_bytes = [int64]$metric.candidate_bytes
        size_ratio = [Math]::Round([double]$metric.size_ratio, 4)
        mae = [Math]::Round([double]$metric.mae, 4)
        psnr = [Math]::Round([double]$metric.psnr, 4)
        ssim = [Math]::Round([double]$metric.ssim, 6)
    }
}

$summaryPath = Join-Path (Join-Path $RepoRoot $OutputDir) ("summary_{0}.json" -f $profile)
$rows | ConvertTo-Json | Set-Content -Path $summaryPath
$rows | Format-Table -AutoSize
Write-Host "Saved summary to $summaryPath"