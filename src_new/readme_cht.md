# CDAS WinUSB 範例

這個資料夾是精簡後的 CDAS WinUSB sample package。它保留原本 sample 的
command flow，但把原本自製 WDM kernel driver 的依賴，替換成一個很薄的
WinUSB 轉換層。

## 這個資料夾保留哪些檔案

```text
src_new
|-- bulkusb.sln
|-- readme.md
|-- readme_cht.md
|-- exe
|   |-- capsousb_test.cpp
|   |-- capsousb_test.vcproj
|   |-- capsousb_test.vcxproj
|   |-- stdafx.cpp
|   |-- stdafx.h
|   `-- targetver.h
|-- include
|   |-- bulkusb_api.h
|   `-- winusb_compat.h
|-- lib
|   |-- bulkusb_lib.vcproj
|   |-- bulkusb_lib-2010.vcxproj
|   |-- stdafx.cpp
|   |-- stdafx.h
|   |-- targetver.h
|   `-- winusb_compat.cpp
`-- sys
    `-- cdas_winusb.inf
```

## 已移除的內容

這個 `src_new` 不再保留原本的 WDM kernel driver source。以下舊檔案已從
`src_new` 移除：

- `bulkusb.c`
- `bulkrwr.c`
- `bulkpnp.c`
- `bulkpwr.c`
- `bulkdev.c`
- WDK `sources` / `makefile`
- 舊 MOF/resource 檔
- 舊 BulkUsb HTML 文件
- 無關 sample data

原始版本仍保留在上一層的 `src` 資料夾。

## 架構

```text
capsousb_test.cpp
  -> OpenBulkUSB / ReadFile / WriteFile / CloseHandle
  -> winusb_compat.h macro 轉換層
  -> winusb_compat.cpp
  -> WinUSB API
  -> Windows 內建 winusb.sys
  -> CDAS USB device
```

`exe\capsousb_test.cpp` 仍然保留原本的 CDAS command protocol。轉換層只替換
USB transport layer。

## 轉換層

`include\winusb_compat.h` 會把原本 sample code 的呼叫導到 WinUSB 實作：

```cpp
#define OpenBulkUSB WinUsbCompatOpenBulkUSB
#define ReadFile WinUsbCompatReadFile
#define WriteFile WinUsbCompatWriteFile
#define CloseHandle WinUsbCompatCloseHandle
```

`lib\winusb_compat.cpp` 做的事情：

1. 用 SetupAPI 找 WinUSB device interface。
2. 用 `CreateFile` 開啟 device path。
3. 呼叫 `WinUsb_Initialize`。
4. 依讀/寫方向選第一個 bulk 或 interrupt endpoint。
5. 用 `WinUsb_ReadPipe` 或 `WinUsb_WritePipe` 傳輸資料。

## INF

`sys\cdas_winusb.inf` 是 WinUSB INF 範本，會把 CDAS device 綁定到 Windows
內建的 `winusb.sys`。

目前列出的 hardware IDs：

- `USB\VID_03EB&PID_941C`
- `USB\VID_0638&PID_0931`

部署前必須再次確認 VID/PID 是否正確，也要依目標 Windows 版本完成 INF/package
簽章。

## 支援的 Windows 版本

這個以 WinUSB 取代原 kernel driver 的方案只規劃給 Windows 8、Windows 10、
Windows 11 使用。它依賴 Windows 內建的 `winusb.sys`，以及較新的 WinUSB INF
binding 模式。

如果部署目標是 Windows XP、Windows Vista 或 Windows 7，除非另外準備並驗證
該 OS 可用且已簽章的 WinUSB package，否則應繼續使用 `..\src` /
`..\installation*` 內的原 legacy driver package。

## Build 注意事項

保留 Visual Studio legacy project：

- `exe\capsousb_test.vcproj`
- `exe\capsousb_test.vcxproj`
- `lib\bulkusb_lib.vcproj`
- `lib\bulkusb_lib-2010.vcxproj`

專案已更新為 VS2022 `v143` toolset，且不再需要 MFC。sample application 需要
link：

- `setupapi.lib`
- `winusb.lib`

本機已用下列方式完成 build 驗證：

```bat
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x86
MSBuild.exe exe\capsousb_test.vcxproj /p:Configuration=MFC_DLL_Debug /p:Platform=Win32 /t:Build
```

build 成功，會產生 `exe\MFC_DLL_Debug\test_exe.exe`。build output 不保留在這份
source package 內。

## 重要限制

- `OpenBulkUSB` 回傳的 `HANDLE` 實際上是轉換層 context pointer，不是真正的
  Windows kernel handle。
- 這個 handle 只能給本 sample 中被包裝過的 `ReadFile` / `WriteFile` /
  `CloseHandle` 使用。
- 不要把這個 handle 傳給其他 Win32 API。
- 目前轉換層只會依方向選第一個 bulk 或 interrupt endpoint。
- 如果 CDAS device 有多組同方向 endpoint，需要補明確 endpoint mapping。
- 原本 sample 的 command flow 沒有重寫，因此原本的限制仍存在，例如目前
  `main()` 仍是 hard-coded serial-number maintenance flow。

## 與原本 driver 的關係

原本 `..\src\sys` 的自製 WDM kernel driver 負責在 kernel mode 處理 USB bulk
transport。這個 `src_new` 改由 Windows 內建 `winusb.sys` 處理 transport。

CDAS command flow 仍然在 user-mode sample `exe\capsousb_test.cpp` 裡，沒有移到
kernel driver。
