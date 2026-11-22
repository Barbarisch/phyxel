param([string]$Tag = 'control', [switch]$Red)
$ErrorActionPreference = 'Continue'
Set-Location G:\Github\phyxel-matsimb-lab
$W = 'C:\Users\jack\Documents\PhyxelProjects\VoxFoundGate\worlds'
$BK = 'C:\Users\jack\AppData\Local\Temp\claude\88187f369423edff\e326662c-cb88-4b3e-8111-42d79f622943\scratchpad\gate_db_backup'
function Stop-Mine { Get-Process phyxel -ErrorAction SilentlyContinue | Where-Object { $_.Path -like "G:\Github\phyxel-matsimb-lab\*" } | Stop-Process -Force; Start-Sleep 3 }
function Launch {
    $old = $env:PATH
    $env:PATH = "C:\Users\jack\AppData\Roaming\uv\python\cpython-3.12.13-windows-x86_64-none;" + $env:PATH
    Start-Process build\editor\Release\phyxel.exe -WorkingDirectory G:\Github\phyxel-matsimb-lab -ArgumentList '--project','C:\Users\jack\Documents\PhyxelProjects\VoxFoundGate','--port','8120'
    $env:PATH = $old
    $t0 = Get-Date
    do { Start-Sleep 5; try { $s = Invoke-RestMethod http://127.0.0.1:8120/api/debug/load_state -TimeoutSec 20; $ok = $s.settled } catch { $ok = $false } } until ($ok -or ((Get-Date) - $t0).TotalSeconds -gt 400)
    "settled=$ok"
}
Stop-Mine
if (-not (Test-Path $BK)) { New-Item -ItemType Directory $BK | Out-Null; Copy-Item "$W\default.db*" $BK; "backup made: $((Get-ChildItem $BK).Name -join ', ')" }
else { Copy-Item "$BK\default.db*" $W -Force; "world.db restored from the backup before the run" }
Launch
if ($Red) { Invoke-RestMethod -Method Post -Uri http://127.0.0.1:8120/api/debug/voxfound_preview -Body '{"save_stub":true}' -ContentType 'application/json' | ConvertTo-Json -Compress }
python tools\voxfound_f76a_live.py before $Tag
Stop-Mine
Launch
python tools\voxfound_f76a_live.py after $Tag
Stop-Mine
Copy-Item "$BK\default.db*" $W -Force
Get-ChildItem $W | % { "$($_.Name) $($_.Length) $($_.LastWriteTime)" }
"world.db restored"
