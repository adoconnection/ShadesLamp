$port = [System.IO.Ports.SerialPort]::new('COM13', 115200)
$port.ReadTimeout = 2000
$port.Open()
$sw = [System.Diagnostics.Stopwatch]::StartNew()
while ($sw.ElapsedMilliseconds -lt 20000) {
    try {
        $line = $port.ReadLine()
        Write-Host $line
    } catch {
    }
}
$port.Close()
