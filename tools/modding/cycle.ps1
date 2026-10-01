# Rebuild the modded Ape Escape build, relaunch it without the launcher and
# restore save state 9. Only touches the process running from build\.
$ErrorActionPreference = 'Stop'
$root  = 'D:\Ape Escape Recomp\modding'
$build = "$root\ApeEscapeRecomp\build"
$exe   = "$build\ApeEscapeRecomp.exe"
Get-Process ApeEscapeRecomp -ErrorAction SilentlyContinue |
    Where-Object { $_.Path -eq $exe } | ForEach-Object { Stop-Process -Id $_.Id -Force; $_.WaitForExit() }
$out = & cmd /c "`"$root\vsbuild.cmd`" cmake --build build --target psx-runtime 2>&1"
if ($LASTEXITCODE -ne 0) { $out | Select-String 'error|FAILED' | Select-Object -First 15; throw 'build failed' }
$env:PSX_NO_LAUNCHER = '1'
Start-Process -FilePath $exe -WorkingDirectory $build | Out-Null
for ($i = 0; $i -lt 60; $i++) {
    Start-Sleep -Milliseconds 500
    try { $c = New-Object Net.Sockets.TcpClient('127.0.0.1', 4480); $c.Close(); break } catch {}
}
Start-Sleep -Seconds 4
python "$root\re\dbg.py" savestate op=load slot=9
Start-Sleep -Seconds 2
'ready'
