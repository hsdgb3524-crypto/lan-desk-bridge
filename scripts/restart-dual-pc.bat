@echo off
setlocal
echo ========================================================
echo   Restarting Dual-PC Bridge (KVM + Audio Receiver)
echo ========================================================

echo Stopping existing processes...
taskkill /F /IM barriers.exe >nul 2>&1
taskkill /F /IM audio_receiver.exe >nul 2>&1

timeout /t 1 /nobreak >nul

echo Starting Barrier Server...
schtasks /run /tn "BarrierServer" >nul 2>&1

echo Starting LAN Audio Receiver...
schtasks /run /tn "LanAudioReceiver" >nul 2>&1

timeout /t 1 /nobreak >nul

echo Checking process status:
powershell -NoProfile -Command "Get-Process -Name barriers, audio_receiver -ErrorAction SilentlyContinue | Select-Object Id, ProcessName, WorkingSet64"

echo.
echo [OK] Both Barrier and Audio Receiver are running silently!
timeout /t 3 >nul
