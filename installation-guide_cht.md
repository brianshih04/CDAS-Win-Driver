# CDAS WinUSB 安裝指南

這份文件說明 `src_new` 內目前 active CDAS WinUSB package 在 Windows 8/10/11
上的安裝流程。

舊的 XP/Vista/Windows 7 driver package 已封存到 `Old_files`，不屬於本指南
範圍。

## 支援系統

- Windows 8
- Windows 10
- Windows 11

`src_new/sys/cdas_winusb.inf` 內包含 x86 與 x64 INF section，但正式部署前仍需
在實際目標 OS 與架構上驗證。

## 使用檔案

```text
src_new/sys/cdas_winusb.inf
src_new/include/winusb_compat.h
src_new/lib/winusb_compat.cpp
src_new/exe/capsousb_test.cpp
```

INF 會將 USB device 綁定到 Windows 內建 `winusb.sys`。sample application 透過
WinUSB API 與 device 溝通。

## 安裝前確認

1. 在 Device Manager 或使用 `usbview.exe` 確認 device hardware ID。
2. 與 `src_new/sys/cdas_winusb.inf` 內的 ID 比對。
3. 如果實際 VID/PID 不同，先更新 INF。
4. 依目標 Windows 環境 package 並簽章 driver package。

目前 INF 內的 IDs：

- `USB\VID_03EB&PID_941C`
- `USB\VID_0638&PID_0931`
- `USB\VID_03EB&PID_952C`

## 簽章需求

repository 內提供的是 INF 範本，不包含已產生的 catalog，也不包含 private
signing key。

正式部署時，必須依目標 Windows 版本與安全政策產生 catalog 並簽章。
Windows 10 與 Windows 11 的正式部署通常需要受信任的 driver package。

若僅供工程測試，可依公司 driver test policy 使用 Windows test-signing mode。

目前 INF 已包含 `PnpLockdown=1`，並已使用 WDK `InfVerif.exe` 檢查。

## 使用 pnputil 安裝

開啟系統管理員權限的 Command Prompt 或 PowerShell。

在 repository 根目錄執行：

```bat
pnputil /add-driver src_new\sys\cdas_winusb.inf /install
```

如果 Windows 沒有立刻套用新的 driver binding，請拔除並重新接上 CDAS device。

## 驗證 Driver Binding

在 Device Manager：

1. 找到 CDAS USB device。
2. 開啟 device properties。
3. 檢查 driver details。
4. 確認清單中包含 `winusb.sys`。

也可以使用 `usbview.exe` 檢查 device，確認 VID/PID 與 INF 相符。

## Build Sample

sample 已用 VS2022 Build Tools 驗證。

```bat
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x86
MSBuild.exe src_new\exe\capsousb_test.vcxproj /p:Configuration=Debug /p:Platform=Win32 /t:Build
```

build 輸出檔：

```text
src_new\exe\Debug\capsousb_test.exe
```

repository 也保留一份預先 build 好的 copy：

```text
dist\capsousb_test.exe
```

如果 sample source 有變更，請重新 build executable，並更新 `dist` 內的 copy。

## 執行 Sample

確認 device 已綁定到 `winusb.sys` 後，依本機 device access policy，以一般或
系統管理員權限的 command prompt 執行 sample。

```bat
dist\capsousb_test.exe
```

sample 保留 `capsousb_test.cpp` 內原本的 command flow。WinUSB 轉換層只替換
USB transport layer。

## Troubleshooting

- 如果安裝失敗，請確認 INF 已簽章，且 catalog 與 INF 內容相符。
- 如果 Windows 選到其他 driver，請移除舊 driver binding 後重新安裝 WinUSB
  package。
- 如果 sample 無法開啟 device，請確認 VID/PID、interface GUID 與 device
  permission。
- 如果傳輸失敗，請確認 device 有預期的 bulk-in 與 bulk-out endpoints。
- 如果同方向有多個 endpoints，請在 `src_new/lib/winusb_compat.cpp` 增加明確的
  endpoint selection。
- 如果程式呼叫舊的 device-counting helper API，這份 WinUSB sample layer 會回傳
  "not implemented"。

## Rollback

如需 rollback，可使用 Device Manager 或 `pnputil` 移除已安裝的 WinUSB driver
package。

範例：

```bat
pnputil /enum-drivers
pnputil /delete-driver oemXX.inf /uninstall
```

請將 `oemXX.inf` 換成 `pnputil` 顯示的 published INF name。
