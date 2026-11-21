param(
    [Parameter(Mandatory=$true)][string]$FontPath,
    [string]$Converter = 'lv_font_conv'
)
$ErrorActionPreference = 'Stop'
$RepoRoot = (Resolve-Path "$PSScriptRoot/../../../../../../..").Path
python "$RepoRoot/tools/generate_product_font.py" --size 14 --font $FontPath --converter $Converter
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
