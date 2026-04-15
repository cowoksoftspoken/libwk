param(
    [string]$InputDir = "photos",
    [float]$Quality = 75,
    [ValidateSet("444", "420")]
    [string]$Subsampling = "444",
    [switch]$Lossless,
    [string]$OutputDir = "benchmark",
    [ValidateSet("any", "jpeg", "webp", "png", "ppm", "unknown")]
    [string]$FormatFilter = "any"
)

function Get-DeclaredFormat([string]$Path) {
    $ext = [System.IO.Path]::GetExtension($Path).TrimStart('.').ToLowerInvariant()
    if ($ext -eq 'jpg') {
        return 'jpeg'
    }
    return $ext
}

function Get-ImageSignatureFormat([string]$Path) {
    $buffer = New-Object byte[] 16
    $stream = [System.IO.File]::OpenRead($Path)
    try {
        $read = $stream.Read($buffer, 0, $buffer.Length)
    } finally {
        $stream.Dispose()
    }

    if ($read -ge 12 -and $buffer[0] -eq 0x52 -and $buffer[1] -eq 0x49 -and $buffer[2] -eq 0x46 -and $buffer[3] -eq 0x46 -and $buffer[8] -eq 0x57 -and $buffer[9] -eq 0x45 -and $buffer[10] -eq 0x42 -and $buffer[11] -eq 0x50) {
        return 'webp'
    }
    if ($read -ge 8 -and $buffer[0] -eq 0x89 -and $buffer[1] -eq 0x50 -and $buffer[2] -eq 0x4E -and $buffer[3] -eq 0x47 -and $buffer[4] -eq 0x0D -and $buffer[5] -eq 0x0A -and $buffer[6] -eq 0x1A -and $buffer[7] -eq 0x0A) {
        return 'png'
    }
    if ($read -ge 3 -and $buffer[0] -eq 0xFF -and $buffer[1] -eq 0xD8 -and $buffer[2] -eq 0xFF) {
        return 'jpeg'
    }
    if ($read -ge 2 -and $buffer[0] -eq 0x50 -and $buffer[1] -eq 0x36) {
        return 'ppm'
    }
    return 'unknown'
}

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
$formatSuffix = if ($FormatFilter -eq "any") { "" } else { "_src$FormatFilter" }
$profile = if ($Lossless) { "lossless$formatSuffix" } else { "q$([int][Math]::Round($Quality))_yuv$Subsampling$formatSuffix" }
$rows = @()

foreach ($photo in $photos) {
    $declaredFormat = Get-DeclaredFormat $photo.Name
    $signatureFormat = Get-ImageSignatureFormat $photo.FullName
    if ($signatureFormat -ne 'unknown' -and $signatureFormat -ne $declaredFormat) {
        Write-Warning "Extension/content mismatch for $($photo.Name): extension says $declaredFormat, signature says $signatureFormat"
    }
    if ($FormatFilter -ne "any" -and $signatureFormat -ne $FormatFilter) {
        continue
    }

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
        declared_format = $declaredFormat
        detected_format = $signatureFormat
        source_bytes = [int64]$metric.reference_bytes
        wk_bytes = [int64]$metric.candidate_bytes
        size_ratio = [Math]::Round([double]$metric.size_ratio, 4)
        mae = [Math]::Round([double]$metric.mae, 4)
        psnr = [Math]::Round([double]$metric.psnr, 4)
        ssim = [Math]::Round([double]$metric.ssim, 6)
    }
}

if ($rows.Count -eq 0) {
    throw "No input files matched FormatFilter=$FormatFilter"
}

$summaryPath = Join-Path (Join-Path $RepoRoot $OutputDir) ("summary_{0}.json" -f $profile)
$rows | ConvertTo-Json | Set-Content -Path $summaryPath
$rows | Format-Table -AutoSize
Write-Host "Saved summary to $summaryPath"
