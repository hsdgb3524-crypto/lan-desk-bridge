# ==============================================================
#   双机协同 (键鼠跨屏 + 耳机音频) 一键启动与同步器
# ==============================================================

Write-Host "==============================================================" -ForegroundColor Cyan
Write-Host "     双机协同 (键鼠跨屏 + 耳机音频) 一键拉起与同步工具" -ForegroundColor Cyan
Write-Host "==============================================================" -ForegroundColor Cyan
Write-Host ""

# 1. 清理 Windows 旧进程
Write-Host "[1/4] 清理 Windows 端历史进程..." -ForegroundColor Gray
Stop-Process -Name barriers, audio_receiver -Force -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 600

# 2. 静默拉起 Windows 服务
Write-Host "[2/4] 启动 Windows 键鼠服务端与音频接收端..." -ForegroundColor Gray
if (Test-Path "D:\Barrier\run_hidden.vbs") {
    Start-Process "wscript.exe" -ArgumentList "D:\Barrier\run_hidden.vbs"
} else {
    schtasks.exe /run /tn "BarrierServer" | Out-Null
}

if (Test-Path "D:\LanAudio\run_hidden.vbs") {
    Start-Process "wscript.exe" -ArgumentList "D:\LanAudio\run_hidden.vbs"
} else {
    schtasks.exe /run /tn "LanAudioReceiver" | Out-Null
}
Start-Sleep -Milliseconds 1000

# 3. 联动拉起 Linux 辅机端
Write-Host "[3/4] 正在检测 Linux 辅机并联动拉起服务..." -ForegroundColor Gray
cmd.exe /c "ssh -n -o BatchMode=yes -o ConnectTimeout=2 linux systemctl --user restart barrier-client.service lan-audio-sender.service >nul 2>&1"

# 4. 获取运行状态并展示
Write-Host "[4/4] 校验双端运行状态..." -ForegroundColor Gray
Write-Host "--------------------------------------------------------------" -ForegroundColor DarkGray

$barr = Get-Process -Name barriers -ErrorAction SilentlyContinue
$audi = Get-Process -Name audio_receiver -ErrorAction SilentlyContinue

if ($barr) {
    $ramB = [math]::Round($barr.WorkingSet64 / 1MB, 1)
    Write-Host "  [✓] Windows 键鼠服务端: 已拉起 (PID: $($barr.Id), 内存: $ramB MB)" -ForegroundColor Green
} else {
    Write-Host "  [✗] Windows 键鼠服务端: 未启动，请检查 D:\Barrier\barriers.exe" -ForegroundColor Red
}

if ($audi) {
    $ramA = [math]::Round($audi.WorkingSet64 / 1MB, 1)
    Write-Host "  [✓] Windows 音频接收端: 已拉起 (PID: $($audi.Id), 内存: $ramA MB)" -ForegroundColor Green
} else {
    Write-Host "  [✗] Windows 音频接收端: 未启动，请检查 D:\LanAudio\audio_receiver.exe" -ForegroundColor Red
}

$tempFile = [System.IO.Path]::Combine($env:TEMP, "dual_pc_status.txt")
cmd.exe /c "ssh -n -o BatchMode=yes -o ConnectTimeout=2 linux systemctl --user is-active barrier-client.service lan-audio-sender.service > `"$tempFile`" 2>nul"

if (Test-Path $tempFile) {
    $linuxStatus = Get-Content $tempFile -Raw -ErrorAction SilentlyContinue
    Remove-Item $tempFile -Force -ErrorAction SilentlyContinue
} else {
    $linuxStatus = ""
}

if ($linuxStatus -match "active") {
    Write-Host "  [✓] Linux 辅机协同服务: 已联通且处于活跃状态" -ForegroundColor Green
} else {
    Write-Host "  [!] Linux 辅机未响应 (请确认是否开机或 2.5G 网线连接)" -ForegroundColor Yellow
}

Write-Host "--------------------------------------------------------------" -ForegroundColor DarkGray
Write-Host ""
Write-Host "快捷键指南:" -ForegroundColor White
Write-Host "  Ctrl + Alt + K  -->  一键切入 Linux 辅机" -ForegroundColor Cyan
Write-Host "  Ctrl + Alt + J  -->  一键切回 Windows 主机" -ForegroundColor Cyan
Write-Host ""
Write-Host "音频说明:" -ForegroundColor White
Write-Host "  Linux 产生的所有声音已自动混入 Windows 当前耳机 (无感低延时)" -ForegroundColor Gray
Write-Host "==============================================================" -ForegroundColor Cyan
Write-Host ""
Write-Host "服务已全部就绪！此窗口将在 4 秒后自动关闭..." -ForegroundColor Gray
Start-Sleep -Seconds 4
