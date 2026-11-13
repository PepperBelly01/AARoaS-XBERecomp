# Key and mouse icons in place of the controller's buttons, as texture replacements (textures\replacement\ beside the
# exe; matched by the hash in the file name, any size: drawn at 4x for sharp keys at high resolutions).
#
#   powershell -ExecutionPolicy Bypass -File tools\make_key_icons.ps1 [output folder]
#
# The game's button art, found in a texture dump:
#   128x128_cee43c3773341320  the menus' buttons, 32 x 32 cells: row 1 A X B, row 2 Start Y, row 3 L (left trigger)
#                             Black, row 4 White R (right trigger)
#   (play's icons, the HUD's A and the tutorial boxes' buttons, the game draws itself with the player's keys:
#   src\game\key_icons.cpp)
# The menus have their own keys (core/keyboard.cpp): A Enter, B Escape, X X, Y Y, the shoulders (pages) Q and E;
# Start, Black and White show play's Escape, Tab and Z (game_info.cpp). Kept in
# @keyboard: used while the keyboard or mouse was used last, the controller's own art otherwise.
param([string]$Out = "$PSScriptRoot\..\out\build\release\textures\replacement\@keyboard")

Add-Type -AssemblyName System.Drawing
New-Item -ItemType Directory -Force $Out | Out-Null

$Cap = [Drawing.Color]::FromArgb(255, 28, 31, 24)       # key face
$Edge = [Drawing.Color]::FromArgb(255, 214, 214, 190)   # key rim, text

function RoundRect([Drawing.Graphics]$g, [float]$x, [float]$y, [float]$w, [float]$h, [float]$r, $brush, $pen) {
    $p = New-Object Drawing.Drawing2D.GraphicsPath
    $p.AddArc($x, $y, 2 * $r, 2 * $r, 180, 90)
    $p.AddArc($x + $w - 2 * $r, $y, 2 * $r, 2 * $r, 270, 90)
    $p.AddArc($x + $w - 2 * $r, $y + $h - 2 * $r, 2 * $r, 2 * $r, 0, 90)
    $p.AddArc($x, $y + $h - 2 * $r, 2 * $r, 2 * $r, 90, 90)
    $p.CloseFigure()
    if ($brush) { $g.FillPath($brush, $p) }
    if ($pen) { $g.DrawPath($pen, $p) }
    $p.Dispose()
}

# a key cap with its label in a cell of size s at (x, y)
function Key([Drawing.Graphics]$g, [float]$x, [float]$y, [float]$s, [string]$label) {
    $m = $s * 0.08
    $face = New-Object Drawing.SolidBrush $Cap
    $rim = New-Object Drawing.Pen $Edge, ($s * 0.06)
    RoundRect $g ($x + $m) ($y + $m) ($s - 2 * $m) ($s - 2 * $m) ($s * 0.16) $face $rim
    # the largest bold type that fits the face
    $box = $s - 4 * $m
    $size = $s * 0.62
    do {
        $font = New-Object Drawing.Font "Arial", $size, ([Drawing.FontStyle]::Bold), ([Drawing.GraphicsUnit]::Pixel)
        $fit = $g.MeasureString($label, $font)
        if ($fit.Width -le $box) { break }
        $font.Dispose()
        $size *= 0.9
    } while ($size -gt 4)
    $fmt = New-Object Drawing.StringFormat
    $fmt.Alignment = 'Center'; $fmt.LineAlignment = 'Center'
    $text = New-Object Drawing.SolidBrush $Edge
    $g.DrawString($label, $font, $text, (New-Object Drawing.RectangleF $x, ($y + $s * 0.02), $s, $s), $fmt)
    $font.Dispose(); $face.Dispose(); $rim.Dispose(); $text.Dispose()
}

function NewCanvas([int]$w, [int]$h) {
    $bmp = New-Object Drawing.Bitmap $w, $h, ([Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = 'AntiAlias'; $g.TextRenderingHint = 'AntiAliasGridFit'; $g.Clear([Drawing.Color]::Transparent)
    return $bmp, $g
}

# the menus' atlas: 4 x 4 cells of 128
$bmp, $g = NewCanvas 512 512
$c = 128
Key   $g (0 * $c) (0 * $c) $c "ENTER"   # A
Key   $g (1 * $c) (0 * $c) $c "X"       # X
Key   $g (2 * $c) (0 * $c) $c "ESC"     # B
Key   $g (0 * $c) (1 * $c) $c "ESC"     # Start (Escape in play)
Key   $g (1 * $c) (1 * $c) $c "Y"       # Y
Key   $g (0 * $c) (2 * $c) $c "Q"       # left shoulder: the page before
Key   $g (1 * $c) (2 * $c) $c "TAB"     # Black: map (play)
Key   $g (0 * $c) (3 * $c) $c "Z"       # White: alt fire (play)
Key   $g (1 * $c) (3 * $c) $c "E"       # right shoulder: the page after
$bmp.Save((Join-Path $Out "128x128_cee43c3773341320_keys.png"), [Drawing.Imaging.ImageFormat]::Png)
$g.Dispose(); $bmp.Dispose()

"Key icons written to $Out"
