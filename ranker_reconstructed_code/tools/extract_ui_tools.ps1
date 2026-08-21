param(
    [string]$SourcePath = (Join-Path $PSScriptRoot '..\resources\tools.png'),
    [string]$OutputDirectory = (Join-Path $PSScriptRoot '..\resources\ui\tools')
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$expectedSourceHash = '39FBA4529F6AC86C47CA95AD33D8920EE69AD88A6F40FE077484E5195D1D0828'
$expectedWidth = 1470
$expectedHeight = 1070
$padding = 5

function New-ToolSprite {
    param(
        [string]$Name,
        [string]$Family,
        [string]$Tone,
        [string]$State,
        [int]$X,
        [int]$Y,
        [int]$Width,
        [int]$Height
    )
    [pscustomobject][ordered]@{
        name = $Name
        family = $Family
        tone = $Tone
        state = $State
        x = $X
        y = $Y
        width = $Width
        height = $Height
    }
}

function Get-BitmapPixelSha256 {
    param($Bitmap)

    $rect = [Drawing.Rectangle]::new(0, 0, $Bitmap.Width, $Bitmap.Height)
    $data = $Bitmap.LockBits($rect, [Drawing.Imaging.ImageLockMode]::ReadOnly,
        [Drawing.Imaging.PixelFormat]::Format32bppArgb)
    try {
        $byteCount = [Math]::Abs($data.Stride) * $data.Height
        $bytes = [byte[]]::new($byteCount)
        [Runtime.InteropServices.Marshal]::Copy(
            $data.Scan0, $bytes, 0, $byteCount)
        $sha = [Security.Cryptography.SHA256]::Create()
        try {
            return ([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-', '')
        }
        finally {
            $sha.Dispose()
        }
    }
    finally {
        $Bitmap.UnlockBits($data)
    }
}

# Bounds are the connected visible regions at alpha > 32. Extraction adds a
# five-pixel transparent/antialiased perimeter without resampling any pixels.
$definitions = @(
    (New-ToolSprite 'logo' 'logo' 'bronze' 'default' 21 39 750 179),
    (New-ToolSprite 'frame_wide_bronze' 'wide_frame' 'bronze' 'normal' 807 26 607 101),
    (New-ToolSprite 'frame_wide_gold' 'wide_frame' 'gold' 'hot' 802 139 619 104),
    (New-ToolSprite 'button_large_bronze' 'large_button' 'bronze' 'normal' 27 267 432 110),
    (New-ToolSprite 'button_large_gold' 'large_button' 'gold' 'hot' 496 263 455 120),
    (New-ToolSprite 'button_large_gray' 'large_button' 'gray' 'disabled' 981 268 440 109),
    (New-ToolSprite 'button_medium_bronze' 'medium_button' 'bronze' 'normal' 26 410 300 86),
    (New-ToolSprite 'button_medium_gold' 'medium_button' 'gold' 'hot' 367 407 326 93),
    (New-ToolSprite 'button_medium_pressed' 'medium_button' 'pressed' 'pressed' 732 410 327 86),
    (New-ToolSprite 'button_medium_gray' 'medium_button' 'gray' 'disabled' 1106 410 312 86),
    (New-ToolSprite 'button_small_bronze' 'small_button' 'bronze' 'normal' 28 529 130 71),
    (New-ToolSprite 'button_small_gold' 'small_button' 'gold' 'hot' 177 528 130 73),
    (New-ToolSprite 'button_small_pressed' 'small_button' 'pressed' 'pressed' 324 529 126 71),
    (New-ToolSprite 'button_small_gray' 'small_button' 'gray' 'disabled' 469 529 126 71),
    (New-ToolSprite 'arrow_right_bronze' 'arrow_right' 'bronze' 'normal' 625 529 79 79),
    (New-ToolSprite 'arrow_right_gold' 'arrow_right' 'gold' 'hot' 726 526 82 86),
    (New-ToolSprite 'arrow_right_pressed' 'arrow_right' 'pressed' 'pressed' 829 529 78 79),
    (New-ToolSprite 'arrow_right_gray' 'arrow_right' 'gray' 'disabled' 931 529 78 79),
    (New-ToolSprite 'arrow_up_bronze' 'arrow_up' 'bronze' 'normal' 1053 531 77 73),
    (New-ToolSprite 'arrow_up_gold' 'arrow_up' 'gold' 'hot' 1152 529 84 79),
    (New-ToolSprite 'arrow_down_bronze' 'arrow_down' 'bronze' 'normal' 1260 531 77 73),
    (New-ToolSprite 'arrow_down_gray' 'arrow_down' 'gray' 'disabled' 1362 531 76 73),
    (New-ToolSprite 'checkbox_bronze_unchecked' 'checkbox' 'bronze' 'unchecked' 30 645 64 64),
    (New-ToolSprite 'checkbox_bronze_checked' 'checkbox' 'bronze' 'checked' 115 645 63 65),
    (New-ToolSprite 'checkbox_gray_unchecked' 'checkbox' 'gray' 'unchecked_disabled' 201 645 62 65),
    (New-ToolSprite 'checkbox_gray_checked' 'checkbox' 'gray' 'checked_disabled' 285 645 63 64),
    (New-ToolSprite 'radio_bronze_unselected' 'radio' 'bronze' 'unselected' 380 648 55 57),
    (New-ToolSprite 'radio_bronze_selected' 'radio' 'bronze' 'selected' 456 648 56 57),
    (New-ToolSprite 'radio_gray_unselected' 'radio' 'gray' 'unselected_disabled' 535 647 58 58),
    (New-ToolSprite 'radio_gray_selected' 'radio' 'gray' 'selected_disabled' 615 647 57 58),
    (New-ToolSprite 'combo_left_bronze' 'combo' 'bronze' 'left_variant' 30 749 376 71),
    (New-ToolSprite 'combo_right_bronze' 'combo' 'bronze' 'right_variant' 444 749 367 71),
    (New-ToolSprite 'combo_left_gold' 'combo' 'gold' 'left_variant_hot' 30 839 378 69),
    (New-ToolSprite 'combo_right_gold' 'combo' 'gold' 'right_variant_hot' 439 838 377 72),
    (New-ToolSprite 'combo_left_gray' 'combo' 'gray' 'left_variant_disabled' 30 933 376 69),
    (New-ToolSprite 'combo_right_gray' 'combo' 'gray' 'right_variant_disabled' 444 933 367 70),
    (New-ToolSprite 'scrollbar_bronze' 'scrollbar' 'bronze' 'normal' 887 644 51 381),
    (New-ToolSprite 'scrollbar_gold' 'scrollbar' 'gold' 'hot' 1000 644 56 383),
    (New-ToolSprite 'scrollbar_gray' 'scrollbar' 'gray' 'disabled' 1113 644 51 380),
    (New-ToolSprite 'vertical_slider_bronze' 'vertical_slider' 'bronze' 'normal' 1243 643 33 381),
    (New-ToolSprite 'vertical_slider_gold' 'vertical_slider' 'gold' 'hot' 1308 642 34 383),
    (New-ToolSprite 'vertical_slider_gray' 'vertical_slider' 'gray' 'disabled' 1382 643 32 382)
)

$resourceRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\resources'))
$sourceFullPath = [IO.Path]::GetFullPath($SourcePath)
$outputFullPath = [IO.Path]::GetFullPath($OutputDirectory)
if (-not $sourceFullPath.StartsWith($resourceRoot, [StringComparison]::OrdinalIgnoreCase) -or
    -not $outputFullPath.StartsWith($resourceRoot, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'UI tool extraction paths must stay under ranker_reconstructed_code\resources.'
}
if (-not (Test-Path -LiteralPath $sourceFullPath -PathType Leaf)) {
    throw "Source atlas was not found: $sourceFullPath"
}

$sourceHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $sourceFullPath).Hash
if ($sourceHash -ne $expectedSourceHash) {
    throw "Unexpected tools.png hash: $sourceHash"
}

Add-Type -AssemblyName System.Drawing
$atlas = [Drawing.Bitmap]::FromFile($sourceFullPath)
try {
    if ($atlas.Width -ne $expectedWidth -or $atlas.Height -ne $expectedHeight) {
        throw "Unexpected tools.png dimensions: $($atlas.Width)x$($atlas.Height)"
    }

    [IO.Directory]::CreateDirectory($outputFullPath) | Out-Null
    Get-ChildItem -LiteralPath $outputFullPath -File -Filter '*.png' |
        Remove-Item -Force

    $sprites = foreach ($definition in $definitions) {
        $cropX = [Math]::Max(0, $definition.x - $padding)
        $cropY = [Math]::Max(0, $definition.y - $padding)
        $cropRight = [Math]::Min($atlas.Width,
            $definition.x + $definition.width + $padding)
        $cropBottom = [Math]::Min($atlas.Height,
            $definition.y + $definition.height + $padding)
        $crop = [Drawing.Rectangle]::FromLTRB(
            $cropX, $cropY, $cropRight, $cropBottom)
        $destination = Join-Path $outputFullPath ($definition.name + '.png')
        $sprite = $atlas.Clone($crop, [Drawing.Imaging.PixelFormat]::Format32bppArgb)
        try {
            $sourcePixelHash = Get-BitmapPixelSha256 $sprite
            $sprite.Save($destination, [Drawing.Imaging.ImageFormat]::Png)
        }
        finally {
            $sprite.Dispose()
        }

        $decoded = [Drawing.Bitmap]::FromFile($destination)
        try {
            if ($decoded.Width -ne $crop.Width -or $decoded.Height -ne $crop.Height -or
                (($decoded.PixelFormat -band [Drawing.Imaging.PixelFormat]::Alpha) -eq 0 -and
                 ($decoded.PixelFormat -band [Drawing.Imaging.PixelFormat]::PAlpha) -eq 0)) {
                throw "Extracted sprite lost dimensions or alpha: $($definition.name)"
            }
            $decodedPixelHash = Get-BitmapPixelSha256 $decoded
            if ($decodedPixelHash -ne $sourcePixelHash) {
                throw "Extracted sprite pixels differ from the atlas: $($definition.name)"
            }
        }
        finally {
            $decoded.Dispose()
        }

        [pscustomobject][ordered]@{
            name = $definition.name
            file = $definition.name + '.png'
            family = $definition.family
            tone = $definition.tone
            state = $definition.state
            source_bounds = [pscustomobject][ordered]@{
                x = $definition.x
                y = $definition.y
                width = $definition.width
                height = $definition.height
            }
            crop = [pscustomobject][ordered]@{
                x = $crop.X
                y = $crop.Y
                width = $crop.Width
                height = $crop.Height
            }
            content_offset = [pscustomobject][ordered]@{
                x = $definition.x - $crop.X
                y = $definition.y - $crop.Y
            }
            sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $destination).Hash
            pixel_sha256 = $decodedPixelHash
        }
    }

    $manifest = [pscustomobject][ordered]@{
        schema_version = 1
        source_original_name = 'tools.png'
        source_distributed = $false
        source_sha256 = $sourceHash
        atlas = [pscustomobject][ordered]@{
            width = $atlas.Width
            height = $atlas.Height
            pixel_format = '32bpp ARGB'
            component_alpha_threshold = 32
            extraction_padding = $padding
        }
        sprite_count = $sprites.Count
        sprites = @($sprites)
    }
    $manifestPath = Join-Path $outputFullPath 'manifest.json'
    $json = $manifest | ConvertTo-Json -Depth 8
    [IO.File]::WriteAllText($manifestPath, $json,
        [Text.UTF8Encoding]::new($false))
}
finally {
    $atlas.Dispose()
}

Write-Output "Extracted $($definitions.Count) UI sprites to $outputFullPath"
