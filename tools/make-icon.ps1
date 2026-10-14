# Regenerates the checked-in multi-resolution ICO from the same geometry as
# src/assets/winhelper.svg. Normal builds use the ICO directly.
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$AssetRoot = Join-Path (Split-Path $PSScriptRoot -Parent) 'src\assets'
$Frames = @()
foreach ($Size in @(16,20,24,32,40,48,64,128,256)) {
    $Canvas = [Drawing.Bitmap]::new(1024,1024)
    $Graphics = [Drawing.Graphics]::FromImage($Canvas)
    $Graphics.SmoothingMode = [Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $Graphics.ScaleTransform(4,4)
    function RoundedRect($Color, $X, $Y, $W, $H, $R) {
        $Shape = [Drawing.Drawing2D.GraphicsPath]::new()
        $Shape.AddArc($X,$Y,2*$R,2*$R,180,90)
        $Shape.AddArc($X+$W-2*$R,$Y,2*$R,2*$R,270,90)
        $Shape.AddArc($X+$W-2*$R,$Y+$H-2*$R,2*$R,2*$R,0,90)
        $Shape.AddArc($X,$Y+$H-2*$R,2*$R,2*$R,90,90)
        $Shape.CloseFigure()
        $Brush = [Drawing.SolidBrush]::new([Drawing.ColorTranslator]::FromHtml($Color))
        $Graphics.FillPath($Brush,$Shape)
        $Brush.Dispose(); $Shape.Dispose()
    }
    RoundedRect '#165dde' 8 8 240 240 52
    RoundedRect '#ffffff' 43 54 170 118 15
    RoundedRect '#102f74' 55 66 146 94 5
    $Brush = [Drawing.SolidBrush]::new([Drawing.ColorTranslator]::FromHtml('#ffbe45'))
    $Graphics.FillPolygon($Brush,[Drawing.PointF[]]@([Drawing.PointF]::new(110,90),[Drawing.PointF]::new(151,113),[Drawing.PointF]::new(110,137)))
    $Brush.Dispose()
    $Graphics.FillRectangle([Drawing.Brushes]::White,116,172,24,21)
    $Graphics.FillRectangle([Drawing.Brushes]::White,90,193,76,12)
    $Graphics.Dispose()
    $Frame = [Drawing.Bitmap]::new($Size,$Size)
    $Resize = [Drawing.Graphics]::FromImage($Frame)
    $Resize.InterpolationMode = [Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $Resize.DrawImage($Canvas,0,0,$Size,$Size)
    $Stream = [IO.MemoryStream]::new()
    $Frame.Save($Stream,[Drawing.Imaging.ImageFormat]::Png)
    $Frames += @{ Size=$Size; Bytes=$Stream.ToArray() }
    $Resize.Dispose(); $Frame.Dispose(); $Canvas.Dispose(); $Stream.Dispose()
}
$File = [IO.File]::Create((Join-Path $AssetRoot 'winhelper.ico'))
$Writer = [IO.BinaryWriter]::new($File)
try {
    $Writer.Write([uint16]0); $Writer.Write([uint16]1); $Writer.Write([uint16]$Frames.Count)
    $Offset = 6 + 16 * $Frames.Count
    foreach ($Frame in $Frames) {
        $Dimension = if ($Frame.Size -eq 256) { 0 } else { $Frame.Size }
        $Writer.Write([byte]$Dimension); $Writer.Write([byte]$Dimension)
        $Writer.Write([byte]0); $Writer.Write([byte]0)
        $Writer.Write([uint16]1); $Writer.Write([uint16]32)
        $Writer.Write([uint32]$Frame.Bytes.Length); $Writer.Write([uint32]$Offset)
        $Offset += $Frame.Bytes.Length
    }
    foreach ($Frame in $Frames) { $Writer.Write([byte[]]$Frame.Bytes) }
} finally { $Writer.Dispose(); $File.Dispose() }
Write-Host 'Updated src/assets/winhelper.ico'
