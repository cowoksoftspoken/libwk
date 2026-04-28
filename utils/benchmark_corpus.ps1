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

function Get-RelativePath([string]$BasePath, [string]$TargetPath) {
    $base = [System.IO.Path]::GetFullPath($BasePath)
    if (!$base.EndsWith([System.IO.Path]::DirectorySeparatorChar) -and !$base.EndsWith([System.IO.Path]::AltDirectorySeparatorChar)) {
        $base += [System.IO.Path]::DirectorySeparatorChar
    }
    $baseUri = [System.Uri]$base
    $targetUri = [System.Uri]([System.IO.Path]::GetFullPath($TargetPath))
    return [System.Uri]::UnescapeDataString($baseUri.MakeRelativeUri($targetUri).ToString())
}

function Get-PortablePath([string]$Path) {
    return ((($Path -replace '\\', '/') -replace '/+', '/')).TrimStart('./')
}

function Get-SceneGroup([string]$PhotoPath, [string]$PhotosRoot, [string]$InputRoot) {
    $base = if ([System.IO.Path]::GetFullPath($PhotoPath).StartsWith([System.IO.Path]::GetFullPath($PhotosRoot), [System.StringComparison]::OrdinalIgnoreCase)) {
        $PhotosRoot
    } else {
        $InputRoot
    }
    $relative = Get-PortablePath (Get-RelativePath $base $PhotoPath)
    $parts = $relative -split '/'
    if ($parts.Length -le 1) {
        return 'root'
    }
    return $parts[0].ToLowerInvariant()
}

function Get-OutputStem([string]$RelativePath) {
    $withoutExt = [System.IO.Path]::ChangeExtension($RelativePath, $null)
    if ($withoutExt.EndsWith('.')) {
        $withoutExt = $withoutExt.Substring(0, $withoutExt.Length - 1)
    }
    return ((($withoutExt -replace '[\\/]+', '_') -replace '[^A-Za-z0-9._-]', '_')).Trim('._')
}

function Get-LightingBucket([double]$MeanLuma, [double]$DarkFraction, [double]$BrightFraction) {
    if ($DarkFraction -ge 0.40 -or $MeanLuma -le 0.33) {
        return 'dark'
    }
    if ($BrightFraction -ge 0.35 -or $MeanLuma -ge 0.62) {
        return 'bright'
    }
    return 'balanced'
}

function New-Rollup([string]$Name, $Rows) {
    if ($Rows.Count -eq 0) {
        return [pscustomobject]@{
            key = $Name
            count = 0
            total_wk_bytes = 0
            avg_psnr = 0.0
            avg_ssim = 0.0
            avg_chroma_psnr = 0.0
            avg_weighted_chroma_mae = 0.0
            avg_source_mean_luma = 0.0
        }
    }

    return [pscustomobject]@{
        key = $Name
        count = $Rows.Count
        total_wk_bytes = [int64](($Rows | Measure-Object -Property wk_bytes -Sum).Sum)
        avg_psnr = [Math]::Round([double](($Rows | Measure-Object -Property psnr -Average).Average), 4)
        avg_ssim = [Math]::Round([double](($Rows | Measure-Object -Property ssim -Average).Average), 6)
        avg_chroma_psnr = [Math]::Round([double](($Rows | Measure-Object -Property chroma_psnr -Average).Average), 4)
        avg_weighted_chroma_mae = [Math]::Round([double](($Rows | Measure-Object -Property weighted_chroma_mae -Average).Average), 4)
        avg_source_mean_luma = [Math]::Round([double](($Rows | Measure-Object -Property source_mean_luma -Average).Average), 4)
    }
}

$RepoRoot = Split-Path -Parent $PSScriptRoot
$PhotosRoot = Join-Path $RepoRoot "photos"
$InputRoot = Join-Path $RepoRoot $InputDir
$wkenc = Join-Path $RepoRoot "build\wkenc.exe"
$wkdec = Join-Path $RepoRoot "build\wkdec.exe"
$wkmetric = Join-Path $RepoRoot "build\wkmetric.exe"

if (!(Test-Path $wkenc)) {
    throw "wkenc.exe not found at $wkenc"
}
if (!(Test-Path $wkdec)) {
    throw "wkdec.exe not found at $wkdec"
}
if (!(Test-Path $wkmetric)) {
    throw "wkmetric.exe not found at $wkmetric"
}
if (!(Test-Path $InputRoot)) {
    throw "InputDir not found: $InputRoot"
}

$photos = Get-ChildItem -Path $InputRoot -Recurse -File |
    Where-Object { $_.Extension.ToLowerInvariant() -in '.jpg', '.jpeg' } |
    Sort-Object FullName
if ($photos.Count -eq 0) {
    throw "No .jpg/.jpeg files found in $InputDir"
}

$OutputRoot = Join-Path $RepoRoot $OutputDir
New-Item -ItemType Directory -Force -Path $OutputRoot | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $OutputRoot "rollup") | Out-Null
$formatSuffix = if ($FormatFilter -eq "any") { "" } else { "_src$FormatFilter" }
$profile = if ($Lossless) { "lossless$formatSuffix" } else { "q$([int][Math]::Round($Quality))_yuv$Subsampling$formatSuffix" }
$rows = @()

foreach ($photo in $photos) {
    $relativePath = Get-PortablePath (Get-RelativePath $RepoRoot $photo.FullName)
    $declaredFormat = Get-DeclaredFormat $photo.Name
    $signatureFormat = Get-ImageSignatureFormat $photo.FullName
    if ($signatureFormat -ne 'unknown' -and $signatureFormat -ne $declaredFormat) {
        Write-Warning "Extension/content mismatch for ${relativePath}: extension says $declaredFormat, signature says $signatureFormat"
    }
    if ($FormatFilter -ne "any" -and $signatureFormat -ne $FormatFilter) {
        continue
    }

    $sceneGroup = Get-SceneGroup $photo.FullName $PhotosRoot $InputRoot
    $groupRoot = Join-Path $OutputRoot $sceneGroup
    $encodedDir = Join-Path $groupRoot "encoded"
    $decodedDir = Join-Path $groupRoot "decoded"
    $groupSummaryDir = Join-Path $groupRoot "summary"
    $groupRollupDir = Join-Path $groupRoot "rollup"
    New-Item -ItemType Directory -Force -Path $encodedDir, $decodedDir, $groupSummaryDir, $groupRollupDir | Out-Null

    $outputStem = Get-OutputStem $photo.Name
    $output = Join-Path $encodedDir ("{0}_{1}.wk" -f $outputStem, $profile)
    $decoded = Join-Path $decodedDir ("{0}_{1}.png" -f $outputStem, $profile)
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
        throw "wkenc failed for $relativePath"
    }

    & $wkdec $output $decoded
    if ($LASTEXITCODE -ne 0) {
        throw "wkdec failed for $relativePath"
    }

    $json = & $wkmetric --json $photo.FullName $output
    if ($LASTEXITCODE -ne 0) {
        throw "wkmetric failed for $relativePath"
    }

    $metric = $json | ConvertFrom-Json
    $lightingBucket = Get-LightingBucket ([double]$metric.reference_stats.mean_luma) ([double]$metric.reference_stats.dark_fraction) ([double]$metric.reference_stats.bright_fraction)
    $rows += [pscustomobject]@{
        image = $photo.Name
        relative_path = $relativePath
        scene_group = $sceneGroup
        lighting_bucket = $lightingBucket
        profile = $profile
        declared_format = $declaredFormat
        detected_format = $signatureFormat
        output_file = (Get-PortablePath (Get-RelativePath $RepoRoot $output))
        decoded_file = (Get-PortablePath (Get-RelativePath $RepoRoot $decoded))
        source_bytes = [int64]$metric.reference_bytes
        wk_bytes = [int64]$metric.candidate_bytes
        size_ratio = [Math]::Round([double]$metric.size_ratio, 4)
        mae = [Math]::Round([double]$metric.mae, 4)
        psnr = [Math]::Round([double]$metric.psnr, 4)
        ssim = [Math]::Round([double]$metric.ssim, 6)
        y_psnr = [Math]::Round([double]$metric.ycbcr.Y.psnr, 4)
        cb_psnr = [Math]::Round([double]$metric.ycbcr.Cb.psnr, 4)
        cr_psnr = [Math]::Round([double]$metric.ycbcr.Cr.psnr, 4)
        chroma_psnr = [Math]::Round([double]$metric.artifacts.chroma_psnr, 4)
        weighted_luma_mae = [Math]::Round([double]$metric.artifacts.weighted_luma_mae, 4)
        weighted_chroma_mae = [Math]::Round([double]$metric.artifacts.weighted_chroma_mae, 4)
        max_abs_error = [Math]::Round([double]$metric.artifacts.max_abs_error, 4)
        source_mean_luma = [Math]::Round([double]$metric.reference_stats.mean_luma, 4)
        source_luma_stddev = [Math]::Round([double]$metric.reference_stats.luma_stddev, 4)
        source_mean_chroma = [Math]::Round([double]$metric.reference_stats.mean_chroma, 4)
        source_dark_fraction = [Math]::Round([double]$metric.reference_stats.dark_fraction, 4)
        source_bright_fraction = [Math]::Round([double]$metric.reference_stats.bright_fraction, 4)
    }
}

if ($rows.Count -eq 0) {
    throw "No input files matched FormatFilter=$FormatFilter"
}

$sceneRollups = @($rows | Group-Object scene_group | Sort-Object Name | ForEach-Object { New-Rollup $_.Name $_.Group })
$lightingRollups = @($rows | Group-Object lighting_bucket | Sort-Object Name | ForEach-Object { New-Rollup $_.Name $_.Group })
$sceneLightingRollups = @(
    $rows |
        Group-Object { "$($_.scene_group)|$($_.lighting_bucket)" } |
        Sort-Object Name |
        ForEach-Object {
            $parts = $_.Name -split '\|', 2
            [pscustomobject]@{
                scene_group = $parts[0]
                lighting_bucket = $parts[1]
                summary = New-Rollup $_.Name $_.Group
            }
        }
)
$rollup = [pscustomobject]@{
    profile = $profile
    input_dir = $InputDir
    total_images = $rows.Count
    overall = New-Rollup 'overall' $rows
    by_scene_group = $sceneRollups
    by_lighting_bucket = $lightingRollups
    by_scene_and_lighting = $sceneLightingRollups
}
$rollupPath = Join-Path (Join-Path $OutputRoot "rollup") ("rollup_{0}.json" -f $profile)
$rollup | ConvertTo-Json -Depth 6 | Set-Content -Path $rollupPath -Encoding utf8

foreach ($sceneGroup in ($rows | Select-Object -ExpandProperty scene_group -Unique | Sort-Object)) {
    $groupRows = @($rows | Where-Object { $_.scene_group -eq $sceneGroup })
    $groupRoot = Join-Path $OutputRoot $sceneGroup
    $groupSummaryPath = Join-Path (Join-Path $groupRoot "summary") ("summary_{0}.json" -f $profile)
    $groupRows | ConvertTo-Json -Depth 4 | Set-Content -Path $groupSummaryPath -Encoding utf8

    $groupRollup = [pscustomobject]@{
        profile = $profile
        input_dir = $InputDir
        total_images = $groupRows.Count
        overall = New-Rollup $sceneGroup $groupRows
        by_scene_group = @((New-Rollup $sceneGroup $groupRows))
        by_lighting_bucket = @($groupRows | Group-Object lighting_bucket | Sort-Object Name | ForEach-Object { New-Rollup $_.Name $_.Group })
        by_scene_and_lighting = @(
            $groupRows |
                Group-Object { "$($_.scene_group)|$($_.lighting_bucket)" } |
                Sort-Object Name |
                ForEach-Object {
                    $parts = $_.Name -split '\|', 2
                    [pscustomobject]@{
                        scene_group = $parts[0]
                        lighting_bucket = $parts[1]
                        summary = New-Rollup $_.Name $_.Group
                    }
                }
        )
    }
    $groupRollupPath = Join-Path (Join-Path $groupRoot "rollup") ("rollup_{0}.json" -f $profile)
    $groupRollup | ConvertTo-Json -Depth 6 | Set-Content -Path $groupRollupPath -Encoding utf8
}

$rows |
    Select-Object relative_path, scene_group, lighting_bucket, wk_bytes, psnr, ssim, chroma_psnr, @{ Name = 'w_chroma'; Expression = { $_.weighted_chroma_mae } } |
    Format-Table -AutoSize
Write-Host "Saved rollup to $rollupPath"
