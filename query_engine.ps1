$base = "http://localhost:8090/api/command"

# Get player/entities
$body = '{"action":"list_entities"}'
$entities = Invoke-RestMethod -Uri $base -Method Post -Body $body -ContentType "application/json"
Write-Host "=== ENTITIES ==="
$entities | ConvertTo-Json -Depth 5

# Get placed objects
$body2 = '{"action":"list_placed_objects"}'
$objects = Invoke-RestMethod -Uri $base -Method Post -Body $body2 -ContentType "application/json"
Write-Host "=== PLACED OBJECTS ==="
$objects | ConvertTo-Json -Depth 8
