---
name: screenshot
description: Build, run the game for 3 seconds, take a screenshot, and display it
user_invocable: true
---

# Screenshot Skill

Take a screenshot of the running game.

## Steps

1. Build the project: `export PATH="/c/msys64/ucrt64/bin:$PATH" && cmake --build build`
2. Kill any existing fruit-ninja.exe: `taskkill //F //IM fruit-ninja.exe 2>/dev/null`
3. Run the game in background: `export PATH="/c/msys64/ucrt64/bin:$PATH" && build/fruit-ninja.exe &`
4. Wait 3 seconds: `sleep 3`
5. Take screenshot via PowerShell:
```
powershell.exe -Command "
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
\$screen = [System.Windows.Forms.Screen]::PrimaryScreen
\$bitmap = New-Object System.Drawing.Bitmap(\$screen.Bounds.Width, \$screen.Bounds.Height)
\$graphics = [System.Drawing.Graphics]::FromImage(\$bitmap)
\$graphics.CopyFromScreen(\$screen.Bounds.Location, [System.Drawing.Point]::Empty, \$screen.Bounds.Size)
\$bitmap.Save('C:/Users/Mariotaku/Projects/webosbrew/fruit-ninja/tmp/screenshot.png')
\$graphics.Dispose()
\$bitmap.Dispose()
" 2>/dev/null
```
6. Kill the game: `taskkill //F //IM fruit-ninja.exe 2>/dev/null`
7. Display the screenshot using the Read tool on `tmp/screenshot.png`

Run all bash steps in a single command where possible. Always display the screenshot at the end.