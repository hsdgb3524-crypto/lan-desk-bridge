@echo off
setlocal
echo ====================================================
echo  Installing LAN Audio Receiver Windows Autostart Task
echo ====================================================

set TARGET_DIR=D:\LanAudio
if not exist "%TARGET_DIR%" mkdir "%TARGET_DIR%"

echo Copying binaries to %TARGET_DIR%...
copy /Y "%~dp0..\bin\lan-audio-receiver.exe" "%TARGET_DIR%\audio_receiver.exe"
copy /Y "%~dp0run_hidden.vbs" "%TARGET_DIR%\run_hidden.vbs"

echo Creating Windows Scheduled Task LanAudioReceiver...
schtasks /create /tn "LanAudioReceiver" /tr "wscript.exe \"%TARGET_DIR%\run_hidden.vbs\"" /sc onlogon /rl highest /f

echo Configuring task settings (battery, execution timeout)...
powershell -NoProfile -Command "$task = Get-ScheduledTask -TaskName 'LanAudioReceiver'; $task.Settings.DisallowStartIfOnBatteries = $false; $task.Settings.StopIfGoingOnBatteries = $false; $task.Settings.ExecutionTimeLimit = 'PT0S'; Set-ScheduledTask -InputObject $task"

echo Opening Windows Firewall port 4010 UDP...
netsh advfirewall firewall add rule name="LanAudio-Port" dir=in action=allow protocol=UDP localport=4010 >nul 2>&1

echo Starting task...
schtasks /run /tn "LanAudioReceiver"

echo ====================================================
echo  Installation Complete! LanAudioReceiver is running.
echo ====================================================
pause
