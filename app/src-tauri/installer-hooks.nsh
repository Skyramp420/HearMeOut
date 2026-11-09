; HearMeOut – háčiky NSIS inštalátora
; host drží svoj .exe otvorený -> pred kopírovaním ho zavri, po inštalácii ho spusti znova (ak je zapnutý VST režim)

!macro NSIS_HOOK_PREINSTALL
  nsExec::Exec 'taskkill /IM HearMeOutHost.exe'
  Sleep 1500
  nsExec::Exec 'taskkill /IM HearMeOutHost.exe /F'
  Sleep 500
!macroend

!macro NSIS_HOOK_POSTINSTALL
  ReadRegStr $0 HKCU "Software\Microsoft\Windows\CurrentVersion\Run" "HearMeOutHost"
  StrCmp $0 "" +3
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Run" "HearMeOutHost" '"$INSTDIR\HearMeOutHost.exe"'
    Exec '"$INSTDIR\HearMeOutHost.exe"'
!macroend

!macro NSIS_HOOK_PREUNINSTALL
  nsExec::Exec 'taskkill /IM HearMeOutHost.exe'
  Sleep 1500
  nsExec::Exec 'taskkill /IM HearMeOutHost.exe /F'
  DeleteRegValue HKCU "Software\Microsoft\Windows\CurrentVersion\Run" "HearMeOutHost"
!macroend
