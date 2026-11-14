$s = "C:\Users\jack\AppData\Local\Temp\claude\88187f369423edff\e326662c-cb88-4b3e-8111-42d79f622943\scratchpad"
Set-Location G:\Github\phyxel-matsimb-lab
foreach ($p in 2, 4, 8) {
    if (-not (Test-Path "$s\f214_ab_P$p.json")) {
        Set-Content -Encoding ascii "$s\f214_ab_P$p.json" ('{"A": [["POST", "/api/debug/voxfound_preview", {"lod_auto": false}]], "B": [["POST", "/api/debug/voxfound_preview", {"lod_auto": true, "lod_px": ' + $p + '}]]}')
    }
    python tools\perf_harness.py sample --base http://127.0.0.1:8120 --poses "$s\f29_poses.json" --ab "$s\f214_ab_P$p.json" --repeats 8 --frames 240 --provenance "engine-generated: /api/debug/voxfound_preview Perlin s7 flora region 254..257x0..1x2..5, lod_levels 3" --out "$s\f214_ab_P$p.jsonl" 2>&1 | Select-Object -Last 3
    python tools\perf_harness.py compare "$s\f214_ab_P$p.jsonl" 2>&1 | Out-File -Encoding utf8 "$s\f214_ab_P$p.txt"
    "P$p done"
}
Invoke-RestMethod -Method Post http://127.0.0.1:8120/api/debug/voxfound_preview -Body '{"lod_auto": false}' -ContentType application/json | Out-Null
