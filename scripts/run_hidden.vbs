Set WshShell = CreateObject("WScript.Shell")
Set fso = CreateObject("Scripting.FileSystemObject")
scriptDir = fso.GetParentFolderName(WScript.ScriptFullName)
exePath = scriptDir & "\audio_receiver.exe"

If fso.FileExists(exePath) Then
    WshShell.Run """" & exePath & """ -p 4010 -b 32", 0, False
Else
    WshShell.Run """D:\LanAudio\audio_receiver.exe"" -p 4010 -b 32", 0, False
End If
