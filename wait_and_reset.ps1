$base = "http://localhost:8090"

# Wait for engine
Write-Host "Waiting for engine..."
for ($i = 0; $i -lt 20; $i++) {
    Start-Sleep -Seconds 2
    try {
        Invoke-RestMethod -Uri "$base/api/status" -TimeoutSec 2 | Out-Null
        Write-Host "Engine ready."
        break
    } catch {}
}

function Post($action, $params = @{}) {
    $body = @{ action = $action } + $params | ConvertTo-Json -Depth 5
    try {
        $r = Invoke-RestMethod -Uri "$base/api/command" -Method Post -Body $body -ContentType "application/json" -TimeoutSec 10
        return $r
    } catch {
        # try alternate endpoint
        try {
            $r = Invoke-RestMethod -Uri "$base/api/$action" -Method Post -Body $body -ContentType "application/json" -TimeoutSec 10
            return $r
        } catch {
            Write-Host "Failed $action : $($_.Exception.Message)"
            return $null
        }
    }
}

# 1. Remove the stale placed object
Write-Host "`n--- Removing test_chair_2 ---"
$r = Invoke-RestMethod -Uri "$base/api/placed_objects/test_chair_2" -Method Delete -TimeoutSec 5 -ErrorAction SilentlyContinue
if (-not $r) {
    # Try POST remove
    $body = '{"action":"remove_placed_object","params":{"id":"test_chair_2"}}'
    try { $r = Invoke-RestMethod -Uri "$base/api/command" -Method Post -Body $body -ContentType "application/json" -TimeoutSec 5 } catch {}
}
Write-Host "Remove result: $($r | ConvertTo-Json)"

# 2. Generate flat world
Write-Host "`n--- Generating flat world ---"
$genBody = @{
    type = "Flat"
    from = @{ x = -1; y = 0; z = -1 }
    to   = @{ x =  1; y = 0; z =  1 }
} | ConvertTo-Json
try {
    $r = Invoke-RestMethod -Uri "$base/api/generate_world" -Method Post -Body $genBody -ContentType "application/json" -TimeoutSec 30
    Write-Host "Generate result: $($r | ConvertTo-Json)"
} catch { Write-Host "generate_world error: $($_.Exception.Message)" }

# 3. Save world
Write-Host "`n--- Saving world ---"
try {
    $r = Invoke-RestMethod -Uri "$base/api/save_world" -Method Post -Body '{}' -ContentType "application/json" -TimeoutSec 15
    Write-Host "Save result: $($r | ConvertTo-Json)"
} catch { Write-Host "save error: $($_.Exception.Message)" }

Write-Host "`nDone."
