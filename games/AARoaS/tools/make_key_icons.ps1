# Key and mouse icons in place of the controller's buttons, as texture replacements (textures\replacement\ beside the
# exe; matched by the hash in the file name, any size: drawn at 4x for sharp keys at high resolutions).
#
#   powershell -ExecutionPolicy Bypass -File tools\make_key_icons.ps1 [output folder]
#
# The game's button art, found in a texture dump:
#   128x128_cee43c3773341320  the menus' buttons, 32 x 32 cells: row 1 A X B, row 2 Start Y, row 3 L (left trigger)
#                             Black, row 4 White R (right trigger)
#   32x32_e4dd91ad26fcd422    the HUD's A (the "talk" and "use" prompts), also the tutorial boxes' A
#   32x32_* below             the tutorial boxes' other buttons, T-UIAAC.utx buttons_xbox_*
# The menus have their own keys (core/keyboard.cpp): A Enter, B Escape, X X, Y Y, the shoulders (pages) Q and E;
# Start, Black and White show play's Escape, Tab and Z, and the HUD's A play's action key, F (game_info.cpp). Kept in
# @keyboard: used while the keyboard or mouse was used last, the controller's own art otherwise.
param([string]$Out = "$PSScriptRoot\..\out\build\release\textures\replacement\@keyboard")

Add-Type -AssemblyName System.Drawing
New-Item -ItemType Directory -Force $Out | Out-Null

$Cap = [Drawing.Color]::FromArgb(255, 28, 31, 24)       # key face
$Edge = [Drawing.Color]::FromArgb(255, 214, 214, 190)   # key rim, text
$Lit = [Drawing.Color]::FromArgb(255, 150, 196, 84)     # the mouse button meant

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

# a mouse with one button lit: Left, Right or Middle
function Mouse([Drawing.Graphics]$g, [float]$x, [float]$y, [float]$s, [string]$button) {
    $w = $s * 0.56; $h = $s * 0.84
    $mx = $x + ($s - $w) / 2; $my = $y + ($s - $h) / 2
    $face = New-Object Drawing.SolidBrush $Cap
    $rim = New-Object Drawing.Pen $Edge, ($s * 0.06)
    $lit = New-Object Drawing.SolidBrush $Lit
    RoundRect $g $mx $my $w $h ($w * 0.45) $face $null
    $split = $my + $h * 0.42
    # the lit button: the body's top half on its side, clipped to the body
    $body = New-Object Drawing.Drawing2D.GraphicsPath
    $r = $w * 0.45
    $body.AddArc($mx, $my, 2 * $r, 2 * $r, 180, 90); $body.AddArc($mx + $w - 2 * $r, $my, 2 * $r, 2 * $r, 270, 90)
    $body.AddArc($mx + $w - 2 * $r, $my + $h - 2 * $r, 2 * $r, 2 * $r, 0, 90); $body.AddArc($mx, $my + $h - 2 * $r, 2 * $r, 2 * $r, 90, 90)
    $body.CloseFigure()
    $g.SetClip($body)
    if ($button -eq 'Left') { $g.FillRectangle($lit, $mx, $my, $w / 2, $split - $my) }
    if ($button -eq 'Right') { $g.FillRectangle($lit, $mx + $w / 2, $my, $w / 2, $split - $my) }
    $g.ResetClip()
    $g.DrawPath($rim, $body)
    $thin = New-Object Drawing.Pen $Edge, ($s * 0.04)
    $g.DrawLine($thin, $mx, $split, $mx + $w, $split)
    $g.DrawLine($thin, $mx + $w / 2, $my, $mx + $w / 2, $split)
    # the wheel, lit for the middle button
    $ww = $w * 0.2; $wh = $h * 0.2
    $wheel = New-Object Drawing.RectangleF ($mx + ($w - $ww) / 2), ($my + $h * 0.12), $ww, $wh
    if ($button -eq "Middle" -or $button -eq "Wheel") { $g.FillRectangle($lit, $wheel) } else { $g.FillRectangle($face, $wheel) }
    $g.DrawRectangle($thin, $wheel.X, $wheel.Y, $wheel.Width, $wheel.Height)
    # the wheel turned: arrows above and below it, beside the mouse
    if ($button -eq "Wheel") {
        $ax = $mx + $w + $s * 0.04; $aw = $s * 0.12
        $up = [Drawing.PointF[]]@((New-Object Drawing.PointF ($ax + $aw / 2), ($my + $h * 0.05)), (New-Object Drawing.PointF $ax, ($my + $h * 0.2)), (New-Object Drawing.PointF ($ax + $aw), ($my + $h * 0.2)))
        $dn = [Drawing.PointF[]]@((New-Object Drawing.PointF ($ax + $aw / 2), ($my + $h * 0.47)), (New-Object Drawing.PointF $ax, ($my + $h * 0.32)), (New-Object Drawing.PointF ($ax + $aw), ($my + $h * 0.32)))
        $g.FillPolygon($lit, $up); $g.FillPolygon($lit, $dn)  # $lit: the brush (PowerShell names ignore case)
    }
    $body.Dispose(); $face.Dispose(); $rim.Dispose(); $lit.Dispose(); $thin.Dispose()
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

# the HUD's A: F, the action key in play
$bmp, $g = NewCanvas 128 128
Key $g 0 0 128 "F"
$bmp.Save((Join-Path $Out "32x32_e4dd91ad26fcd422_keys.png"), [Drawing.Imaging.ImageFormat]::Png)
$g.Dispose(); $bmp.Dispose()

# the tutorial boxes' buttons (T-UIAAC.utx buttons_xbox_*, 32x32 DXT5 each; the A is the HUD's above): play's keys
# from the default layout (game_info.cpp). Keys: the texture key (texturedumper) of each, from the package.
function Cluster([Drawing.Graphics]$g, [float]$s, [string[]]$labels) {  # up left right down
    $k = $s / 3; $o = $k  # a plus of three keys across
    Key $g $o 0 $k $labels[0]; Key $g 0 $o $k $labels[1]; Key $g ($s - $k) $o $k $labels[2]; Key $g $o ($s - $k) $k $labels[3]
}
$Tutorial = [ordered]@{
    "1538c644e9ac0353" = @("Key", "G")         # B: Command
    "bdc2f027099b16f3" = @("Key", "R")         # X: Reload
    "29a9bc5af6801dba" = @("Mouse", "Middle")  # Y: Switch Weapon
    "42c1bc7874044606" = @("Key", "SHIFT")     # left stick click: Sprint
    "589124fb2ef0855b" = @("Mouse", "Wheel")   # right stick click: Zoom
    "ad544119729dd5ef" = @("Key", "Z")         # Back: Alt Fire
    "21fc4578608ea091" = @("Cluster", "")      # D-pad: C up, Q left, E right, X down
    "4d15816daa74ed2e" = @("Key", "ESC")       # Start: the menu
    "5889971a557d2bf0" = @("Key", "TAB")       # Black: map
    "191dc294f8d99c51" = @("Mouse", "Right")   # left trigger: Tweak Lock
    "ee3debd77c39e691" = @("Mouse", "Left")    # right trigger: Fire
    "060bb671b2957f25" = @("Key", "Z")         # White: Alt Fire
}
foreach ($key in $Tutorial.Keys) {
    $kind, $what = $Tutorial[$key]
    $bmp, $g = NewCanvas 128 128
    switch ($kind) {
        "Key" { Key $g 0 0 128 $what }
        "Mouse" { Mouse $g 0 0 128 $what }
        "Cluster" { Cluster $g 128 @("C", "Q", "E", "X") }
    }
    $bmp.Save((Join-Path $Out "32x32_${key}_keys.png"), [Drawing.Imaging.ImageFormat]::Png)
    $g.Dispose(); $bmp.Dispose()
}

"Key icons written to $Out"
