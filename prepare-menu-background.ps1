param(
    [string]$ExtractedBinariesPath,
    [string]$FfmpegPath = 'C:\RE Tools\FFmpeg\ffmpeg-9.0.2-essentials_build\bin\ffmpeg.exe'
)

$ErrorActionPreference = 'Stop'
if (-not $ExtractedBinariesPath) {
    $config = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'SpyHunter.config') |
        Where-Object { $_ -match '^ExtractedBinariesPath=' } | Select-Object -First 1
    if (-not $config) { throw 'ExtractedBinariesPath was not supplied and SpyHunter.config has no value.' }
    $ExtractedBinariesPath = $config.Substring($config.IndexOf('=') + 1)
}
$source = Join-Path $ExtractedBinariesPath 'MOVIES\MBACK.IPU'
$outputDirectory = Join-Path $PSScriptRoot 'local-assets'
$output = Join-Path $outputDirectory 'MBACK.mp4'
if (-not (Test-Path -LiteralPath $FfmpegPath -PathType Leaf)) { throw "FFmpeg was not found: $FfmpegPath" }
if (-not (Test-Path -LiteralPath $source -PathType Leaf)) { throw "MBACK.IPU was not found: $source" }
New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null
& $FfmpegPath -y -i $source -an -c:v libx264 -preset slow -crf 12 -pix_fmt yuv420p -movflags +faststart $output
if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $output -PathType Leaf)) { throw 'FFmpeg conversion failed.' }
Get-FileHash -LiteralPath $source, $output -Algorithm SHA256
