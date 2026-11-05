# Send keystrokes to whatever window has focus (for poking a running port by hand
# when UI automation can't attach to it - e.g. a loose .exe that isn't a Start-menu app).
#   sendkeys.ps1 -Keys 0x26,0x26,0x31 [-HoldMs 80] [-GapMs 120] [-FocusPid 1234]
# Keys are Windows virtual-key codes (0x25-0x28 arrows, 0x30-0x39 digits, 0x41-0x5A letters,
# 0x0D Return, 0x20 Space, 0x11 Ctrl, 0x12 Alt). Prefer the port's own scripted-input
# env var (HLE_SCRIPT) for repeatable tests; this is for ad-hoc checks only.
param([string[]]$Keys, [int]$HoldMs = 80, [int]$GapMs = 120, [int]$FocusPid = 0)
Add-Type @"
using System; using System.Runtime.InteropServices;
public class K {
  [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
}
"@
if ($FocusPid) { [K]::SetForegroundWindow((Get-Process -Id $FocusPid).MainWindowHandle) | Out-Null; Start-Sleep -Milliseconds 200 }
foreach ($k in $Keys) {
  $vk = [byte]([Convert]::ToInt32($k, 16))
  [K]::keybd_event($vk, 0, 0, [UIntPtr]::Zero); Start-Sleep -Milliseconds $HoldMs
  [K]::keybd_event($vk, 0, 2, [UIntPtr]::Zero); Start-Sleep -Milliseconds $GapMs
}
