# CDAS WinUSB Driver Package

這個 repository 目前保存的是 CapsoVision Capsule Data Access System
(CDAS) USB docking device 的 WinUSB 版本 sample package。

目前有效的實作放在 `src_new`。舊的 WDM driver source、舊 installer、
歷史 binary package 已移到 `Old_files`，不屬於目前 active package。

## 目前範圍

目前版本用 Windows 內建的 `winusb.sys` 加上一個很薄的 user-mode 轉換層，
取代原本自製 kernel driver 的路徑。sample 的 command flow 仍然保留在
user mode。

```text
CDAS with-wrapper sample application
  -> BulkUsb-compatible API names
  -> src_new/include/winusb_compat.h
  -> src_new/lib/winusb_compat.cpp
  -> WinUSB API
  -> Windows 內建 winusb.sys
  -> CDAS USB device
```

repository 也包含一份直接使用 WinUSB 的 sample。它執行相同的 serial-number
command test flow，但不經過 compatibility wrapper：

```text
CDAS no-wrapper sample application
  -> SetupAPI / WinUSB API
  -> Windows 內建 winusb.sys
  -> CDAS USB device
```

## 支援的 Windows 版本

這份 WinUSB package 只規劃給：

- Windows 8
- Windows 10
- Windows 11

Windows XP、Windows Vista、Windows 7 不屬於目前 WinUSB package 的支援範圍。
這些舊系統相關檔案只保留在 `Old_files` 作為歷史參考。

## Repository 結構

```text
.
|-- README.md
|-- README_cht.md
|-- installation-guide.md
|-- installation-guide_cht.md
|-- CDAS-Win-Driver-analysis.md
|-- dist
|   |-- capsousb_test_with_wrapper.exe
|   `-- capsousb_test_no_wrapper.exe
|-- Doc
|   |-- CDAS3 New command_V16_AVISION.xlsx
|   |-- USB docking system driver Guide.docx
|   `-- USB docking system driver Guide.pdf
|-- src_new
|   |-- readme.md
|   |-- readme_cht.md
|   |-- sys
|   |   `-- cdas_winusb.inf
|   |-- include
|   |-- lib
|   |-- exe_wtih_wrapper
|   `-- exe_no_wrapper
`-- Old_files
```

`Old_files` 不應視為 active source。它只用於參考、比對與必要時復原。

## 主要檔案

- `src_new/sys/cdas_winusb.inf`：將 CDAS device 綁定到 `winusb.sys` 的
  WinUSB INF 範本。
- `src_new/include/winusb_compat.h`：把舊 sample call 導到 WinUSB function
  的 macro 轉換層。
- `src_new/lib/winusb_compat.cpp`：SetupAPI 與 WinUSB transport 實作。
- `src_new/exe_wtih_wrapper/capsousb_test.cpp`：with-wrapper sample
  command-flow code。
- `src_new/exe_no_wrapper/capsousb_test_no_wrapper.cpp`：直接呼叫 WinUSB 的
  sample，保留相同 serial-number test flow，且不依賴 wrapper project。
- `dist/capsousb_test_with_wrapper.exe`：預先 build 好的 Win32 debug
  with-wrapper sample executable。
- `dist/capsousb_test_no_wrapper.exe`：預先 build 好的 Win32 debug no-wrapper
  sample executable。
- `installation-guide_cht.md`：安裝與驗證流程。
- `CDAS-Win-Driver-analysis.md`：專案分析報告。

## Hardware IDs

目前 WinUSB INF 包含：

- `USB\VID_03EB&PID_941C`
- `USB\VID_0638&PID_0931`
- `USB\VID_03EB&PID_952C`

正式 package 或部署前，必須再次確認實際 device VID/PID。

`src_new/sys/cdas_winusb.inf` 會註冊 WinUSB 轉換層使用的 docking-system
interface GUID 與 production-test interface GUID。

## Build 摘要

`src_new` 內的 Visual Studio project 使用 VS2022 `v143` toolset，以及單純的
Win32/WinUSB API。

已驗證的本機 build targets：

```bat
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x86
MSBuild.exe src_new\exe_wtih_wrapper\capsousb_test.vcxproj /p:Configuration=Debug /p:Platform=Win32 /t:Build
MSBuild.exe src_new\exe_no_wrapper\capsousb_test_no_wrapper.vcxproj /p:Configuration=Debug /p:Platform=Win32 /t:Build
```

samples 會 link：

- `setupapi.lib`
- `winusb.lib`

目前已使用 WDK `InfVerif.exe` 驗證；INF 結果為 valid。

預先 build 好的 sample executables 放在 `dist`：

- `dist/capsousb_test_with_wrapper.exe`
- `dist/capsousb_test_no_wrapper.exe`

若 sample source 有變更，請從對應 project 重新 build 並更新這些檔案。

## 安裝

Windows 8/10/11 的 WinUSB 安裝流程請看 `installation-guide_cht.md`。

簡短流程：

1. 確認 CDAS VID/PID。
2. package 並簽章 `src_new/sys/cdas_winusb.inf`。
3. 使用 `pnputil` 安裝。
4. 確認 device 正在使用 `winusb.sys`。
5. 執行 `dist/capsousb_test_with_wrapper.exe`、`dist/capsousb_test_no_wrapper.exe`，或本機
   重新 build 的 sample application。

## 重要注意事項

- `OpenBulkUSB` 回傳的是轉換層 context pointer，不是真正的 Windows kernel
  handle。
- 這個 handle 只能交給 sample 中已包裝的 `ReadFile`、`WriteFile`、
  `CloseHandle` 使用。
- 轉換層目前會選擇第一個符合方向的 bulk 或 interrupt endpoint。
- 舊的 device counting helper API 目前只提供 compatibility stub，會回傳
  "not implemented"。
- 目前 active WinUSB package 不使用舊的自製 WDM driver。
