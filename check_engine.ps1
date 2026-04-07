Start-Sleep -Seconds 5
for ($i = 0; $i -lt 12; $i++) {
    try {
        $r = Invoke-RestMethod -Uri "http://localhost:8090/api/status" -Method Get -TimeoutSec 3
        Write-Host "Engine ready after $($i*3)s"
        break
    } catch {
        Write-Host "Waiting... ($i)"
        Start-Sleep -Seconds 3
    }
}
