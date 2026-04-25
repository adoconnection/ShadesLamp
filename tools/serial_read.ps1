$port = New-Object System.IO.Ports.SerialPort('COM13', 115200)
$port.ReadTimeout = 2000
$port.DtrEnable = $true
$port.RtsEnable = $true
$port.Open()

# Toggle DTR to reset ESP32
$port.DtrEnable = $false
Start-Sleep -Milliseconds 100
$port.DtrEnable = $true

# Read for 5 seconds
$end = (Get-Date).AddSeconds(5)
while ((Get-Date) -lt $end) {
    try {
        $line = $port.ReadLine()
        Write-Host $line
    } catch {
        # timeout, continue
    }
}
$port.Close()
