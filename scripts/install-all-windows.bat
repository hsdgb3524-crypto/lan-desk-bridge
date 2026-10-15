@echo off
setlocal enabledelayedexpansion

echo =========================================================
echo  Installing Dual-PC Bridge (KVM + Audio) on Windows Host
echo =========================================================

set BARRIER_DIR=D:\Barrier
set AUDIO_DIR=D:\LanAudio

if not exist "%BARRIER_DIR%" mkdir "%BARRIER_DIR%"
if not exist "%AUDIO_DIR%" mkdir "%AUDIO_DIR%"

echo [1/6] Copying configurations and binaries...
copy /Y "%~dp0..\kvm\config\barrier.conf" "%BARRIER_DIR%\barrier.conf"
copy /Y "%~dp0..\bin\lan-audio-receiver.exe" "%AUDIO_DIR%\audio_receiver.exe"
copy /Y "%~dp0run_hidden.vbs" "%AUDIO_DIR%\run_hidden.vbs"

echo [2/6] Applying High-DPI Mouse Coordinate Fix...
reg import "%~dp0..\kvm\scripts\fix_mouse_dpi.reg"

echo [3/6] Opening Firewall Ports (TCP 24800 for KVM, UDP 4010 for Audio)...
netsh advfirewall firewall add rule name="Barrier-KVM-Port" dir=in action=allow protocol=TCP localport=24800 >nul 2>&1
netsh advfirewall firewall add rule name="LanAudio-Port" dir=in action=allow protocol=UDP localport=4010 >nul 2>&1

echo [4/6] Creating VBS runner for Barrier Server...
(
echo Set WshShell = CreateObject("WScript.Shell"^)
echo WshShell.Run """C:\Program Files\Barrier\barriers.exe"" --no-restart --name LAPTOP-JCHTMK43 -f --no-tray --debug DEBUG1 -c ""%BARRIER_DIR%\barrier.conf"" --address 169.254.123.131:24800", 0, False
) > "%BARRIER_DIR%\run_hidden.vbs"

echo [5/6] Registering Autostart Scheduled Tasks...
schtasks /create /tn "BarrierServer" /tr "wscript.exe \"%BARRIER_DIR%\run_hidden.vbs\"" /sc onlogon /rl highest /f >nul
schtasks /create /tn "LanAudioReceiver" /tr "wscript.exe \"%AUDIO_DIR%\run_hidden.vbs\"" /sc onlogon /rl highest /f >nul

powershell -NoProfile -Command "$t1 = Get-ScheduledTask -TaskName 'BarrierServer'; $t1.Settings.DisallowStartIfOnBatteries = $false; $t1.Settings.StopIfGoingOnBatteries = $false; $t1.Settings.ExecutionTimeLimit = 'PT0S'; Set-ScheduledTask -InputObject $t1; $t2 = Get-ScheduledTask -TaskName 'LanAudioReceiver'; $t2.Settings.DisallowStartIfOnBatteries = $false; $t2.Settings.StopIfGoingOnBatteries = $false; $t2.Settings.ExecutionTimeLimit = 'PT0S'; Set-ScheduledTask -InputObject $t2"

echo [6/6] Creating Desktop Restart Shortcut...
set DESKTOP_DIR=%USERPROFILE%\Desktop
copy /Y "%~dp0restart-dual-pc.bat" "%DESKTOP_DIR%\重启双机协同(键鼠+音频).bat"

echo Starting services now...
schtasks /run /tn "BarrierServer" >nul 2>&1
schtasks /run /tn "LanAudioReceiver" >nul 2>&1

echo =========================================================
echo  Installation Complete!
echo  - KVM Server and Audio Receiver are now running silently.
echo  - Desktop shortcut created: 重启双机协同(键鼠+音频).bat
echo =========================================================
pause
