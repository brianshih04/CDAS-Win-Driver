# CDAS WinUSB Source Package

`src_new` 是目前 active 的 CDAS WinUSB sample source package。

它保留 user-mode sample command flow，並以 Windows 內建 `winusb.sys` 取代舊的
自製 WDM kernel driver transport。

## 支援的 Windows 版本

這份 source package 只規劃給：

- Windows 8
- Windows 10
- Windows 11

Windows XP、Windows Vista、Windows 7 不屬於這份 WinUSB package 的支援範圍。
這些舊系統的歷史檔案已封存在 `..\Old_files`。

## 目錄結構

```text
src_new
|-- readme.md
|-- readme_cht.md
|-- exe
|   |-- capsousb_test.cpp
|   |-- capsousb_test.vcxproj
|   |-- stdafx.cpp
|   |-- stdafx.h
|   `-- targetver.h
|-- exe_no_wrapper
|   |-- capsousb_test_no_wrapper.cpp
|   `-- capsousb_test_no_wrapper.vcxproj
|-- include
|   |-- bulkusb_api.h
|   `-- winusb_compat.h
|-- lib
|   |-- bulkusb_lib-2010.vcxproj
|   |-- stdafx.cpp
|   |-- stdafx.h
|   |-- targetver.h
|   `-- winusb_compat.cpp
`-- sys
    `-- cdas_winusb.inf
```

## 架構

```text
exe_wtih_wrapper/capsousb_test.cpp
  -> OpenBulkUSB / ReadFile / WriteFile / CloseHandle
  -> include/winusb_compat.h
  -> lib/winusb_compat.cpp
  -> WinUSB API
  -> winusb.sys
  -> CDAS USB device
```

CDAS command protocol 仍保留在 `exe_wtih_wrapper/capsousb_test.cpp`。轉換層只
改變 USB transport path。

`exe_no_wrapper/capsousb_test_no_wrapper.cpp` 是第二份 sample，保留相同的 serial
number command test flow，但直接呼叫 SetupAPI 與 WinUSB。它不 include
`winusb_compat.h`，也不 reference `lib/bulkusb_lib-2010.vcxproj`。

## 轉換層

`include/winusb_compat.h` 會把舊 BulkUsb-style sample call 導到 WinUSB-backed
function：

```cpp
#define OpenBulkUSB WinUsbCompatOpenBulkUSB
#define ReadFile WinUsbCompatReadFile
#define WriteFile WinUsbCompatWriteFile
#define CloseHandle WinUsbCompatCloseHandle
```

`lib/winusb_compat.cpp` 負責：

1. 用 SetupAPI 找 device interface。
2. 用 `CreateFile` 開啟 device path。
3. 用 `WinUsb_Initialize` 初始化 WinUSB。
4. 依方向選擇 bulk 或 interrupt endpoint。
5. 用 `WinUsb_ReadPipe` 與 `WinUsb_WritePipe` 傳輸資料。

## INF

`sys/cdas_winusb.inf` 會將支援的 CDAS USB device 綁定到 Windows 內建
`winusb.sys`。

目前 hardware IDs：

- `USB\VID_03EB&PID_941C`
- `USB\VID_0638&PID_0931`
- `USB\VID_03EB&PID_952C`

部署前必須再次確認實際 VID/PID。INF 必須依目標 Windows 環境完成 package 與
簽章。

INF 已包含 `PnpLockdown=1`，並註冊 docking-system 與 production-test 兩組
interface GUID。

## Build

目前 active Visual Studio project files：

- `exe_wtih_wrapper/capsousb_test.vcxproj`
- `lib/bulkusb_lib-2010.vcxproj`
- `exe_no_wrapper/capsousb_test_no_wrapper.vcxproj`

目前使用 VS2022 `v143` toolset，以及單純的 Win32/WinUSB API。

已驗證 with-wrapper sample 指令：

```bat
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x86
MSBuild.exe exe_wtih_wrapper\capsousb_test.vcxproj /p:Configuration=Debug /p:Platform=Win32 /t:Build
```

已驗證 no-wrapper sample 指令：

```bat
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x86
MSBuild.exe exe_no_wrapper\capsousb_test_no_wrapper.vcxproj /p:Configuration=Debug /p:Platform=Win32 /t:Build
```

sample 會 link：

- `setupapi.lib`
- `winusb.lib`

INF 已使用 WDK `InfVerif.exe` 驗證。

repository 根目錄的 `dist` folder 會保留預先 build 好的 Win32 debug sample
executables：

- `dist/capsousb_test_with_wrapper.exe`
- `dist/capsousb_test_no_wrapper.exe`

若這份 source package 有變更，請重新 build 並更新這些檔案。

## 安裝

請使用 repository 根目錄的安裝指南：

- `..\installation-guide.md`
- `..\installation-guide_cht.md`

## 限制

- `OpenBulkUSB` 回傳的是轉換層 context pointer，不是真正的 Windows kernel
  handle。
- 回傳的 handle 只能交給 sample 中被包裝過的 calls 使用。
- endpoint selection 目前會選擇第一個符合方向的 bulk 或 interrupt endpoint。
- 如果 device 在同方向有多個 endpoints，需要在 `lib/winusb_compat.cpp` 補上明確
  endpoint mapping。
- device-counting helper API 目前是 compatibility stub，會回傳 "not
  implemented"。
- 原 sample command flow 被保留下來，但它不是完整的 CDAS3 command
  implementation。
