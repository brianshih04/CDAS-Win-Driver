# CDAS-Win-Driver 專案完整分析報告

分析日期：2026-06-06  
分析目標：`C:\projects\CDAS-Win-Driver`，branch `org`，commit `3a10327 Initial CDAS Windows driver import`

## 1. Executive Summary

`CDAS-Win-Driver` 是 CapsoVision Capsule Data Access System (CDAS) / docking system 的 legacy Windows USB driver package。整體專案以 Microsoft WDM BulkUsb DDK/WDK sample 為基礎，加入 CapsoVision 的裝置 ID、使用者模式 helper library、測試/維護工具，以及 XP/Vista/Windows 7 時期的安裝包。

核心驅動程式是 WDM USB function driver，不是 KMDF。它在 PnP start 時讀取 USB configuration descriptor、選擇 interface、建立 pipe context，並透過 device interface GUID 暴露給 user-mode。User-mode 端以 SetupAPI 找到裝置介面，再開啟 `PIPE00` 作為 bulk-in、`PIPE01` 作為 bulk-out，透過 `ReadFile` / `WriteFile` 與裝置傳輸資料。額外的 `capsousb_test` 工具包含早期 CDAS command protocol，例如 firmware update、image upload、serial number read/write。

新增的 `Doc\CDAS3 New command_V16_AVISION.xlsx` 是 CDAS3 command protocol spec，內容包含 release history、CDAS2 command set、Capsoview-to-FX2 command structure、status structure 與 error code。它顯示硬體/韌體協定已經擴充到 FX2/FPGA firmware update、FPGA register R/W、EEPROM/flash read、capsule general data transfer 等功能；現有 `capsousb_test.cpp` 只實作其中很小的一部分，因此 report 將 driver 層、user-mode helper 層、command protocol 層分開看待。

專案可以視為「歷史封存 + 可重建 Windows 7 driver」而不是現代 Windows driver 專案。它包含多個版本來源與二進位封裝：`src`、`src.wdk.vista`、`installation*`、`installer.src`。其中 `installer.src\InstCaps32` / `InstCaps64` 與 `installation.Win7.x64.Signed` 內的部分 driver/catalog 簽章有效；多數舊 `.sys` / tool `.exe` 未簽章，對現代 Windows 安裝與安全審查都有風險。

最重要的結論：

1. Driver 架構完整但高度 legacy，依賴 WDM、舊 WDK 7600、cross-signing、DPInst。
2. `src\sys\capsousb.inf` 與 README / installer INF 的 VID/PID 不一致，可能不是目前實際部署的 INF。
3. IOCTL 權限使用 `FILE_ANY_ACCESS`，reset device/pipe 等操作可被任何可開啟 device interface 的程序呼叫，安全邊界偏弱。
4. User-mode helper 使用 global mutable state、硬編碼 pipe 名稱、硬編碼 VID/PID，並存在一些錯誤處理與 buffer 安全性不足。
5. CDAS3 command spec 已超前現有測試工具，若要支援 CDAS3，應先把協定層整理成正式 API/CLI，而不是只擴充 hard-coded test app。
6. 二進位 artefacts、installer、old signed packages 混在 repo 內，供應鏈與版本辨識成本高。
7. 若要在 Windows 10/11 長期維護，建議以 WinUSB 或 KMDF 重建，而不是繼續擴充這份 WDM sample code。

## 2. 專案目錄與檔案組成

主要目錄：

- `src\sys`：WDM kernel driver 原始碼，包含 PnP、power、read/write、device control、WMI。
- `src\lib`：user-mode static library，封裝 SetupAPI device discovery 與 `OpenBulkUSB()`。
- `src\exe`：console test/maintenance utility，包含 CDAS command protocol。
- `src.wdk.vista`：另一份 WDK/Vista 風格 driver source 與 INF，看起來比 `src\sys` 更接近 Win7 signed package。
- `installation*`：XP/Vista/Win7 的預編譯 driver 安裝包。
- `installer.src`：DPInst-based installer source/package，含 32-bit/64-bit installer、DPInst XML、zip self-extract tooling。
- `installer.AutoDetectOS.BinaryCD`：已封裝的 32-bit/64-bit auto-detect installer binary。
- `Doc`：driver build/signing guide、PDF 版本，以及新增的 CDAS3 command protocol workbook。

檔案類型統計：

- `.h` 22 個
- `.c` 13 個
- `.inf` 11 個
- `.exe` 11 個
- `.sys` 10 個
- `.cat` 6 個
- `.cpp` 6 個
- solution/project/build files 多份，包括 `.sln`、`.vcproj`、`.vcxproj`、WDK `sources`、`dirs`、`makefile`

這表示專案不是單一乾淨 source tree，而是同時保存 source、build output、installer input、installer output、legacy binary release。

### 2.1 Driver/package 結構總覽

整個 driver project 可以分成六層：

1. Kernel USB transport driver

   - 主要位置：`src\sys`
   - Vista/Win7 變體：`src.wdk.vista\sys`
   - 角色：綁定 CDAS USB VID/PID，暴露 device interface GUID，將 user-mode `ReadFile` / `WriteFile` 轉成 USB bulk/interrupt URB。
   - 主要檔案：
     - `bulkusb.c`：`DriverEntry`、`AddDevice`、device object/interface 初始化。
     - `bulkpnp.c`：PnP dispatch、start/stop/remove、USB descriptor 讀取與 interface selection。
     - `bulkrwr.c`：read/write data path、URB 建立、分段傳輸、completion routine。
     - `bulkdev.c`：create/close/device control、pipe open、reset pipe/device、config descriptor IOCTL。
     - `bulkpwr.c`：power IRP、selective suspend、wait-wake。
     - `bulkwmi.c`：WMI registration/system control。
     - `bulkusr.h`：user/kernel 共用 GUID 與 IOCTL 定義。

2. INF 與 driver package metadata

   - 主要位置：`src\sys\capsousb.inf`、`src.wdk.vista\sys\cvusb.inf`、`installer.src\InstCaps32\cvusb.inf`、`installer.src\InstCaps64\cvusb.inf`
   - 角色：宣告 USB hardware IDs、driver service、binary path、registry parameters、catalog file。
   - 重要觀察：`src\sys\capsousb.inf` 使用 `PID_931C`，但 README、library 與 installer INF 使用 `PID_941C` / `PID_0931`，因此 source INF 與 release INF 不可視為同一版本。

3. User-mode access library

   - 主要位置：`src\lib`
   - Public header：`src\include\bulkusb_api.h`
   - 角色：用 SetupAPI 枚舉 device interface，依 VID/PID 選 CDAS1/CDAS2，開啟 `PIPE00` / `PIPE01`。
   - API：`OpenBulkUSB()`、`ChooseUSBDevice()`、`ChooseUSBMode()`、`GetUSBDeviceNum()`、`SelectCDASVersion()`。

4. User-mode test/maintenance tool

   - 主要位置：`src\exe\capsousb_test.cpp`
   - 角色：用 library 開 USB pipe，送出 CDAS command packet，做 image upload、firmware update、serial number read/write 等維護功能。
   - 重要觀察：目前工具只覆蓋早期 command subset，不完整支援新增 CDAS3 V16 spec。

5. Installer 與 release binaries

   - 主要位置：`installation*`、`installer.src`、`installer.AutoDetectOS.BinaryCD`
   - 角色：保存 XP/Vista/Win7 時代的 `.sys`、`.inf`、`.cat`、DPInst、自解壓 installer。
   - 可部署性：`installation.Win7.x64.Signed` 與 `installer.src\InstCaps32/64` 中部分 `.sys`/`.cat` 簽章有效；多數舊 package 與 tool exe 未簽章。

6. Documentation/specification

   - `Doc\USB docking system driver Guide.docx`：WDK 7600 build 與 Windows 7 cross-signing guide。
   - `Doc\USB docking system driver Guide.pdf`：同 guide 的 PDF 版本，便於閱讀/散佈。
   - `Doc\CDAS3 New command_V16_AVISION.xlsx`：CDAS3 command protocol V16，補足 driver source 沒有描述的上層協定。

簡化依賴關係：

```text
CDAS hardware
  -> Windows USB stack
  -> WDM driver: src/sys or src.wdk.vista/sys
  -> Device interface GUID
  -> User-mode library: src/lib + src/include
  -> Maintenance/test tool or product app: src/exe / external app
  -> CDAS command protocol: Doc/CDAS3 New command_V16_AVISION.xlsx
```

### 2.2 新增 Doc 檔案分析

這次 `Doc` 目錄新增/確認的檔案：

- `CDAS3 New command_V16_AVISION.xlsx`：新增，大小約 25 KB。這是 V16 command workbook，包含 4 個 sheets：`release history`、`CDAS2 Cmd Set`、`Cmd structure`、`Error Code`。
- `USB docking system driver Guide.pdf`：新增 PDF，內容與既有 driver guide docx 同主題，重點仍是 WDK 7600 build、GlobalSign cross certificate、Windows 7 x64 KMCS signing。
- `USB docking system driver Guide.docx`：原本已存在，是 driver build/signing 的主要文字來源。

`CDAS3 New command_V16_AVISION.xlsx` 的重點：

- `release history` 到 V16；V15 新增 updating FPGA/Fx2 FW 與對應 error code，V16 新增 `Error_Unknown_Command`。
- `CDAS2 Cmd Set` 同時列出 FPGA-to-capsule、FPGA-to-FX2、Capsoview-to-FX2 command mapping。
- `Cmd structure` 定義 Capsoview-to-FX2 的 32-byte command layout：開頭為 `"U" "S" "B" "C"`，含 Tag、command ID 與命令參數欄位。
- `Error Code` 定義 status structure：開頭為 `"U" "S" "B" "S"`，包含 Tag、Error、FX2 FW version、threshold value/high/low 等欄位。

新增 spec 對現有 source 的影響：

- 現有 `capsousb_test.cpp` 的 `CMD_LENGTH` 是 64，`FillCmd()` 寫入 `USBC`、tag、cmd、start sector、sector count；這與 workbook 的 32-byte command stage 表達方式不完全一致，代表 test tool 是較早期或簡化版 protocol client。
- 現有工具實作的 command 主要是 `0x11`、`0x12`、`0x13`、`0x18`、`0x19`、`0x21`、`0x22` 與 firmware update subset；workbook V16 額外定義 `0x23`、`0x24`、`0x28`、`0x29`、`0x32`、`0x35`-`0x3a`、`0x41`-`0x43`、`0x50`、`0x59`、`0x98`、`0x9a`-`0x9c`、`0xa0`-`0xa5`、`0xac`、`0xad`、`0xc0`-`0xc3`、`0xf0` 等更多命令。
- Driver kernel 層仍然只是 USB bulk transport，不理解這些 command；真正需要更新的是 user-mode command API、測試工具、產品 application 或韌體協定文件。

## 3. Driver 架構分析

### 3.1 DriverEntry 與 dispatch table

`src\sys\bulkusb.c` 是 driver 入口。`DriverEntry()` 配置 registry path，然後註冊主要 IRP dispatch：

- `IRP_MJ_DEVICE_CONTROL` -> `BulkUsb_DispatchDevCtrl`
- `IRP_MJ_POWER` -> `BulkUsb_DispatchPower`
- `IRP_MJ_PNP` -> `BulkUsb_DispatchPnP`
- `IRP_MJ_CREATE` -> `BulkUsb_DispatchCreate`
- `IRP_MJ_CLOSE` -> `BulkUsb_DispatchClose`
- `IRP_MJ_CLEANUP` -> `BulkUsb_DispatchClean`
- `IRP_MJ_READ` / `IRP_MJ_WRITE` -> `BulkUsb_DispatchReadWrite`
- `IRP_MJ_SYSTEM_CONTROL` -> `BulkUsb_DispatchSysCtrl`
- `AddDevice` -> `BulkUsb_AddDevice`

參考位置：`src\sys\bulkusb.c:67`、`src\sys\bulkusb.c:131`

這是典型 WDM function driver 寫法，沒有使用 KMDF object model、queues、I/O target、power policy ownership abstraction。

### 3.2 AddDevice 初始化

`BulkUsb_AddDevice()` 會：

- `IoCreateDevice()` 建立 unnamed FDO，`FILE_DEVICE_UNKNOWN`，設定 `DO_DIRECT_IO`。
- 初始化 device extension：PnP state、queue state、spin locks、remove/stop event、OutstandingIO counter。
- 註冊 WMI。
- attach 到 PDO stack。
- 用 `IoRegisterDeviceInterface()` 註冊 `GUID_CLASS_I82930_BULK`。
- 檢查 WDM 版本。
- 讀取 registry `BulkUsbEnable` 控制 selective suspend。

參考位置：`src\sys\bulkusb.c:186`、`src\sys\bulkusb.c:218`、`src\sys\bulkusb.c:244`、`src\sys\bulkusb.c:360`

架構上，device interface 是 user-mode 找裝置的主要入口，不是傳統 named device symbolic link。

### 3.3 PnP start 與 USB configuration

`HandleStartDevice()` 先把 start IRP 送到 lower driver，成功後呼叫 `ReadandSelectDescriptors()`，再啟用 device interface：

- 讀取 device/configuration descriptor。
- 選擇 interface。
- 設定 `DeviceState = Working`、`QueueState = AllowRequests`。
- 可能啟動 wait-wake 與 selective suspend timer。

參考位置：`src\sys\bulkpnp.c:214`、`src\sys\bulkpnp.c:293`、`src\sys\bulkpnp.c:306`

`ConfigureDevice()` 採兩階段讀 configuration descriptor：先讀固定大小，再用 `wTotalLength` 配置完整 buffer。之後呼叫 `SelectInterfaces()`。

參考位置：`src\sys\bulkpnp.c:456`、`src\sys\bulkpnp.c:499`、`src\sys\bulkpnp.c:536`、`src\sys\bulkpnp.c:597`

`SelectInterfaces()` 使用 `USBD_CreateConfigurationRequestEx()`，並將每個 pipe 的 `MaximumTransferSize` 設為 `USBD_DEFAULT_MAXIMUM_TRANSFER_SIZE`。選擇成功後把 `USBD_INTERFACE_INFORMATION` copy 到 device extension。

參考位置：`src\sys\bulkpnp.c:700`、`src\sys\bulkpnp.c:714`、`src\sys\bulkpnp.c:725`

### 3.4 Pipe open model

User-mode 不是直接對 device interface 做 read/write，而是把 pipe 名接到 device path 後，例如：

- `\\?\...\PIPE00`
- `\\?\...\PIPE01`

Driver 的 `IRP_MJ_CREATE` 解析 `FileObject->FileName`，由 `BulkUsb_PipeWithName()` 從名稱最後的數字取 pipe index。`PIPE00` 對應 pipe 0，`PIPE01` 對應 pipe 1。

參考位置：`src\sys\bulkdev.c:107`、`src\sys\bulkdev.c:128`、`src\sys\bulkrwr.c:33`、`src\sys\bulkrwr.c:73`

限制與觀察：

- `BulkUsb_PipeWithName()` 只接受 `uval < 6`，也就是最多 6 個 pipe context。
- open 時沒有檢查 pipe direction 與 pipe name 的語意是否一致，只是把 file object 綁到對應 pipe。
- `PipeOpen` 被設定，但未看到明確阻止重複開啟同一 pipe 的邏輯。

### 3.5 Read/Write data path

`BulkUsb_DispatchReadWrite()` 是資料傳輸主路徑：

1. 確認 device state 是 `Working`。
2. 等待 selective suspend idle request 完成。
3. 從 `FileObject->FsContext` 取得 `PUSBD_PIPE_INFORMATION`。
4. 只接受 bulk 或 interrupt pipe。
5. 從原始 IRP MDL 取得長度。
6. 限制單次總長度不能超過 `BULKUSB_TEST_BOARD_TRANSFER_BUFFER_SIZE`，也就是 64 KB。
7. 每 stage 最多 `BULKUSB_MAX_TRANSFER_SIZE`，目前是 4096 bytes。
8. 建立 partial MDL 與 `_URB_BULK_OR_INTERRUPT_TRANSFER`。
9. 把原 read/write IRP 改成 `IRP_MJ_INTERNAL_DEVICE_CONTROL` / `IOCTL_INTERNAL_USB_SUBMIT_URB` 送往 USB stack。
10. completion routine 在需要時 recirculate 同一 IRP，直到傳完整個 request。

參考位置：`src\sys\bulkrwr.c:126`、`src\sys\bulkrwr.c:248`、`src\sys\bulkrwr.c:289`、`src\sys\bulkrwr.c:338`、`src\sys\bulkrwr.c:363`

Completion routine `BulkUsb_ReadWriteCompletion()`：

- 成功時累加已傳輸 byte count。
- 若還有剩餘長度，重建 partial MDL 與 URB 長度，再次送往 lower driver。
- 最後設定 `Irp->IoStatus.Information`，釋放 URB/MDL/context。

參考位置：`src\sys\bulkrwr.c:439`、`src\sys\bulkrwr.c:489`、`src\sys\bulkrwr.c:517`、`src\sys\bulkrwr.c:545`、`src\sys\bulkrwr.c:556`

### 3.6 IOCTL surface

`src\sys\bulkusr.h` 定義三個 IOCTL：

- `IOCTL_BULKUSB_GET_CONFIG_DESCRIPTOR`
- `IOCTL_BULKUSB_RESET_DEVICE`
- `IOCTL_BULKUSB_RESET_PIPE`

全部使用：

- `FILE_DEVICE_UNKNOWN`
- `METHOD_BUFFERED`
- `FILE_ANY_ACCESS`

參考位置：`src\sys\bulkusr.h:34`

`BulkUsb_DispatchDevCtrl()` 實作：

- reset pipe：使用目前 file object 的 `FsContext` pipe。
- get config descriptor：copy cached USB config descriptor 到 output buffer。
- reset device：呼叫 `BulkUsb_ResetDevice()`。

參考位置：`src\sys\bulkdev.c:259`、`src\sys\bulkdev.c:339`、`src\sys\bulkdev.c:375`、`src\sys\bulkdev.c:406`

## 4. User-mode library 與工具分析

### 4.1 `src\lib\bulkusb_lib.cpp`

這個 static library 是主要 user-mode API：

- `OpenBulkUSB(int output)`：`0` 開 bulk-in，`1` 開 bulk-out。
- `ChooseUSBDevice(int)`：切換 docking system / production test system GUID。
- `ChooseUSBMode(int)`：同步/overlapped 模式。
- `GetUSBDeviceNum()`：掃描 CDAS1/CDAS2。
- `SelectCDASVersion()`：指定 CDAS version。

參考位置：`src\include\bulkusb_api.h`

Device discovery 流程：

1. 使用 `SetupDiGetClassDevs()` 依 GUID 找 present device interfaces。
2. `SetupDiEnumDeviceInterfaces()` 枚舉 interface。
3. `SetupDiGetDeviceRegistryProperty(... SPDRP_HARDWAREID ...)` 比對 `CDAS1_VID_PID` / `CDAS2_VID_PID`。
4. `OpenOneDevice()` 取得 device path 並用 `CreateFile()` 測試打開。
5. `open_file()` 將 `PIPE00` / `PIPE01` 接在 device path 後，再次 `CreateFile()` 取得傳輸 handle。

參考位置：`src\lib\bulkusb_lib.cpp:48`、`src\lib\bulkusb_lib.cpp:118`、`src\lib\bulkusb_lib.cpp:172`、`src\lib\bulkusb_lib.cpp:246`、`src\lib\bulkusb_lib.cpp:517`

重要問題：

- `OpenOneDevice()` 使用 `wcscpy(devName, functionClassDeviceData->DevicePath)`，沒有明確檢查 `devName` buffer 長度。`completeDeviceName` 是 256 wchar，device path 理論上可能更長。
- `completeDeviceName`、`device_type`、`cdas_version`、`asynchronous_mode` 都是 process-wide static/global，thread safety 不足。
- `OpenBulkUSB()` 會自動偏好 CDAS2；若 CDAS1/CDAS2 同時存在，選擇策略可能不符合 caller 預期。
- `GetUSBDeviceNum()` 沒有呼叫 `SetupDiDestroyDeviceInfoList()`，會造成 user-mode HDEVINFO resource leak。
- `OpenUsbDevice()` 的 `NUMBEROFDEVICES` 設為 4，但 loop 邏輯沒有真正 realloc；註解與實作不一致。
- asynchronous mode 預設為 1，但部分測試程式呼叫 `ReadFile`/`WriteFile` 傳入 `NULL` OVERLAPPED，這和 `FILE_FLAG_OVERLAPPED` 的使用意圖不完全一致。

### 4.2 `src\exe\capsousb_test.cpp`

測試工具實作早期 CDAS command protocol client。Command packet 固定 64 bytes，以 `USBC` magic 開頭：

- bytes 0-3：`0x55 0x53 0x42 0x43`
- bytes 4-7：tag
- byte 8：command
- bytes 12-15：start sector
- bytes 16-17：sector count

參考位置：`src\exe\capsousb_test.cpp:72`

目前 source 中實作的命令包含：

- firmware update：boot/core/cam
- upload image
- rigi-flex board test
- real data upload / upload end
- read/write serial number

參考位置：`src\exe\capsousb_test.cpp:52`

對照 `Doc\CDAS3 New command_V16_AVISION.xlsx` 後，這個工具不是完整 CDAS3 protocol implementation。它沒有覆蓋 V16 workbook 中的 FX2/FPGA firmware update、FPGA register R/W、EEPROM/flash read、general data write/read、capsule ID、INA219D、learning data dump 等命令。

`IssueCommand()` 每次都開 read/write handle、寫 64 byte command、視需求讀/寫 sector data，最後讀 32 byte status。

參考位置：`src\exe\capsousb_test.cpp:115`

`ReadImageFromDockingSystem()` 會讀影像頁數，若 metadata 為 `0xFFFFFFFF` 則使用估算最大頁數，然後每次 32 sector 讀取直到完成。

參考位置：`src\exe\capsousb_test.cpp:246`

重要問題：

- 多處錯誤路徑沒有 close handle 或 free buffer，例如 `IssueCommand()` 在讀寫失敗時直接 return，可能 leak handle。
- `ReadImageFromDockingSystem()` 若初始 command 失敗，會 return 但未 close file / free buffer。
- `strcpy(buffer, serial_num)` 對目前固定字串尚可，但不是可擴充安全 API。
- `main()` 是 hard-coded serial number maintenance tool，不是一般 CLI；沒有 argv parsing。
- firmware path 在 `#if 0` 片段中硬編碼到舊開發機路徑 `X:\USBBULK\...`。

### 4.3 CDAS3 command workbook 分析

`Doc\CDAS3 New command_V16_AVISION.xlsx` 將上層協定分成三個通道/角色：

- Command between FPGA and Capsule
- Command between FPGA and FX2
- Command between Capsoview and FX2

這份 workbook 的價值是補上 driver source 不會包含的 application protocol。Kernel driver 只負責 bulk pipe transport；`capsousb_test.cpp` 只是早期 command client；真正完整的 CDAS3 行為應以這份 workbook 作為 protocol source of truth。

重要命令族群：

- Image/data transfer：`SCSI_CMD_UPLOAD_IMAGE` (`0x11`)、`SCSI_CMD_UPLOADING` (`0x18`)、`SCSI_CMD_UPLOAD_END` (`0x19`)。
- Serial number：`SCSI_CMD_WRITE_SERIAL_NUMBER` (`0x21`)、`SCSI_CMD_READ_SERIAL_NUMBER` (`0x22`)、`WRITE_SERIAL_NUMBER2/3` (`0x38`/`0x39`)。
- Power/test/control：`INDUCTIVE_ON` (`0x23`)、`INDUCTIVE_OFF` (`0x24`)、`LEARNING_TRIGGER` (`0x28`)、`CLEAR_ERROR` (`0x32`)、`RESET` (`0x3a`)。
- Capsule/general data：`WRITE_GD_TO_CAPSULE` (`0x41`)、`READ_GD_FROM_CAPSULE` (`0x42`)、`READ_GD_FROM_CAPSULE_BYTE` (`0x43`)、`WRITE_FREEDATA_TO_CAPSULE` (`0xf0`)。
- Device identity/status：`CAPSULE_ID` (`0x50`)、`READ_INFO_FROM_CAPSULE` (`0x59`)、`READ_FX2FW_VERSION` (`0xa4`)、`READ_FPGA_VERSION` (`0xa5`)。
- Hardware register/firmware maintenance：`RW_FPGA_REG` (`0xad`)、`WRITE_UPDATE_FX2` (`0xc0`)、`WRITE_UPDATE_FPGA` (`0xc1`)、`READ_EEPROM` (`0xc2`)、`READ_FLASH` (`0xc3`)。
- Sensor/learning/diagnostic：`READ_INA219D` (`0x9a`)、`WRITE_INA219D` (`0x9c`)、`DUMP_LEARNINGDATA` (`0xac`)。

Command structure 重點：

- Command stage 欄位以 byte offset 0-31 表示，開頭為 `"U" "S" "B" "C"`，接著是 Tag 與 command ID。
- `UPLOAD_IMAGE` (`0x11`) 增加 flash selection、download speed、page size、CRC ability、capsule frequency 等參數。
- `UPLOADING` (`0x18`) 使用 start page 與 page count 欄位。
- `WRITE_GD_TO_CAPSULE` (`0x41`) / `READ_GD_FROM_CAPSULE` (`0x42`) 有 parameter 1/2 與 data size，並描述 FX2/FPGA/capsule 間的 `0x4f` / `0x5f` bypass/receive-go sequence。
- `WRITE_UPDATE_FX2` (`0xc0`) / `WRITE_UPDATE_FPGA` (`0xc1`) 有 start/continue/stop bit、checksum、data size。

Status/error structure 重點：

- Status structure 開頭為 `"U" "S" "B" "S"`。
- Workbook 定義 error code：`ERROR_Command` (`0x10`)、`ERROR_Unknown_Command` (`0x11`)、`ERROR_ReadPage` (`0x20`)、`ERROR_Data` (`0x21`)、`ERROR_Decode` (`0x22`)、`ERROR_Failed_CRC` (`0x23`)、`ERROR_FIFO_Overflow` (`0x24`)、`ERROR_WritePage` (`0x30`)、`ERROR_EraseBlock` (`0x31`)、signal/capsule/current/cover errors、`ERROR_UpdateFx2` (`0x70`)、`ERROR_UpdateFPGA` (`0x71`)。
- 現有 `VerifyCmdStatus()` 只做 tag/status 類檢查，不足以表達 V16 error taxonomy。

建議：

- 將 workbook 轉成 machine-readable `protocol/cdas3_commands.yaml` 或 `.json`，生成 C/C++ enum、CLI help、測試向量。
- 把 `capsousb_test.cpp` 重構為 protocol client，支援 command registry、參數驗證、status decode、timeout/retry。
- 明確標示哪些 command 適用 CDAS1、CDAS2、CDAS3，哪些需要特定 FX2/FPGA firmware version。

## 5. INF、裝置 ID 與版本一致性

README 中列出的 device IDs：

- `USB\VID_03EB&PID_941C`：CDAS1
- `USB\VID_0638&PID_0931`：CDAS2
- `USB\VID_03EB&PID_952C`：production test system

`src\include\bulkusb_api.h` 也定義：

- `CDAS1_VID_PID = USB\VID_03EB&PID_941C`
- `CDAS2_VID_PID = USB\VID_0638&PID_0931`

但 `src\sys\capsousb.inf` 使用的是：

- `USB\VID_03EB&PID_931C`
- `DriverVer=12/30/2008,5.00.2064`
- `ServiceBinary=CAPSOUSB.sys`

這與 README / user-mode helper 的 `941C` 不一致，可能是舊硬體 ID、typo、或 `src\sys` 已非最新部署源。

相較之下，`src.wdk.vista\sys\cvusb.inf` 使用：

- `USB\VID_03EB&PID_941C`
- `DriverVer=02/15/2013,6.00.2090.1`
- `ServiceBinary=CVISIONUSB.sys`

`installer.src\InstCaps32\cvusb.inf` 與 `installer.src\InstCaps64\cvusb.inf` 使用：

- `USB\VID_03EB&PID_941C`
- `USB\VID_0638&PID_0931`
- `DriverVer=02/20/2014,6.00.2090.1`
- `ServiceBinary=CVISIONUSB.sys`
- service description `CapsoVision Docking System Bulk USB driver 2074`

推論：`installer.src\InstCaps32/64` 的 INF 最接近最後實際對外封裝；`src\sys\capsousb.inf` 可能是舊版或未同步版本。

建議把 INF 與 device ID 的 canonical source 明確化，至少建立一份 `DEVICE_IDS.md` 或更新 README，說明每個 VID/PID 對應硬體版本與支援狀態。

## 6. 建置與簽章

### 6.1 Kernel driver build

`src\sys\sources` 顯示 driver target：

- `TARGETNAME=bulkusb`
- `TARGETTYPE=DRIVER`
- `C_DEFINES=-DWMI_SUPPORT -DUSB2`
- sources：`bulkusb.c`、`bulkpnp.c`、`bulkpwr.c`、`bulkdev.c`、`bulkwmi.c`、`bulkrwr.c`、`bulkusb.rc`
- target libs：`hidclass.lib`、`usbd.lib`、`ntoskrnl.lib`
- warning：`/W3 /WX`

`Doc\USB docking system driver Guide.docx` 寫明：

- 安裝 WDK `7600.16385.1`
- 從 WDK x64 free build environment 進入 `\src.wdk.vista\sys\`
- 執行 `build -cZ`
- 使用 GlobalSign cross certificate 做 Windows 7 x64 kernel-mode code signing

這份文件確認最後維護方向是 Win7 DDK/WDK 時代，不是 Windows 10/11 HLK attestation/signing workflow。

### 6.2 User-mode projects

User-mode library 與 test app 有 VS 2008/2010 風格 project：

- `src\lib\bulkusb_lib.vcproj`
- `src\lib\bulkusb_lib-2010.vcxproj`
- `src\exe\capsousb_test.vcproj`
- `src\exe\capsousb_test.vcxproj`

這些不是現代 SDK-style 專案，也沒有 CI 或 reproducible build script。

### 6.3 Binary signing observations

用 `Get-AuthenticodeSignature` 檢查：

- `installation.Win7.x64.Signed\cvisionusb.sys`：Valid
- `installation.Win7.x64.Signed\cvusb64.cat`：Valid
- `installer.src\InstCaps32\cvisionusb.sys`：Valid
- `installer.src\InstCaps32\cvusb64.cat`：Valid
- `installer.src\InstCaps64\cvisionusb.sys`：Valid
- `installer.src\InstCaps64\cvusb64.cat`：Valid
- `dpinst.exe` / `dpinst32.exe` / `dpinst64.exe`：Valid
- 多數舊 installation 目錄下的 `.sys`：NotSigned 或 UnknownError
- `RwBulk.exe`、`usbview.exe`、`AutoOSbits.exe`：NotSigned

風險：

- 有效簽章不代表適用 Windows 10/11 kernel driver policy。
- cross-signed Windows 7 driver 在 Secure Boot / modern Windows policy 下通常不可直接作為新安裝依據。
- repo 內未簽章 binary 與 signed binary 混放，容易部署錯版本。

## 7. Installer 分析

`installer.src\AutoDetect\AutoInst\AutoInst.cpp` 是簡單的 OS bitness detector：

- 若 `IsWow64()` 為 true，執行 `Usb64.EXE`
- 否則執行 `Usb32.EXE`
- 用 `CreateProcess()`、`CREATE_NO_WINDOW`、`SW_HIDE`
- 目前沒有等待 process 完成的 active code；wait block 被包在 `#if 0`

參考位置：`installer.src\AutoDetect\AutoInst\AutoInst.cpp:28`、`installer.src\AutoDetect\AutoInst\AutoInst.cpp:36`、`installer.src\AutoDetect\AutoInst\AutoInst.cpp:61`

問題：

- 只檢查 WOW64，不完整涵蓋所有現代 ARM64 / x64-on-ARM 情境。
- 不等待 child installer 完成，因此 `AutoOSbits.exe` 成功只代表 process launch 成功，不代表 driver install 成功。
- `Usb32.EXE` / `Usb64.EXE` 是 self-extract package，內容與 build source 沒有 reproducibility guarantee。
- DPInst 是舊工具；現代 Windows driver package 通常改用 pnputil、DIFx replacement strategy 或企業部署工具。

## 8. 安全性與穩定性風險

### 8.1 Kernel IOCTL 權限過寬

`IOCTL_BULKUSB_RESET_DEVICE` 與 `IOCTL_BULKUSB_RESET_PIPE` 使用 `FILE_ANY_ACCESS`。只要 process 可打開 device interface，就可以 reset device/pipe。若 docking system 連在共用或低權限環境，這可能造成 denial-of-service 或干擾資料讀取。

建議：

- 將 reset 類 IOCTL 改為 `FILE_WRITE_ACCESS` 或 `FILE_READ_DATA | FILE_WRITE_DATA`。
- 設定 device interface security descriptor，限制可開啟者。
- 對 maintenance operation 建立明確權限模型。

### 8.2 Device interface security 未明確設定

`IoRegisterDeviceInterface()` 沒有搭配 INF security descriptor 或 SDDL。實際存取權限取決於 Windows default device class/interface ACL。USB class 下的預設 ACL 未必符合醫療/資料擷取設備需求。

建議：

- 在 INF 中使用 `AddReg` / device interface security 相關機制明確限制存取。
- 若改 KMDF，可使用 WDF device init security descriptor。

### 8.3 User-mode buffer 與 resource management

風險點：

- `wcscpy()` 複製 device path 到固定 256 wchar buffer。
- `GetUSBDeviceNum()` 未 destroy HDEVINFO list。
- `IssueCommand()` 錯誤路徑未確保 close handles。
- `ReadImageFromDockingSystem()` early return 未 free/close。
- `strcpy()` 使用固定 buffer，現況可控但不適合長期維護。

建議：

- 改用 RAII wrapper 管理 HANDLE/HDEVINFO/FILE/buffer。
- 改用 `std::wstring` / `StringCchCopy` / bounds-checked APIs。
- 對 `ReadFile` / `WriteFile` 統一同步或 overlapped 模式，不混用。

### 8.4 Concurrency 與 multi-device

`bulkusb_lib.cpp` 使用 global static state：

- `device_type`
- `cdas_version`
- `asynchronous_mode`
- `completeDeviceName`

這使 library 在多 thread、多 device、多 caller 情境下容易互相干擾。

建議：

- 把 device selection 與 mode 放到 explicit context object。
- API 改成 `BulkUsbDevice` / `BulkUsbPipe` handle abstraction。
- 明確支援「選 CDAS1」、「選 CDAS2」、「選指定 device instance path」。

### 8.5 Legacy WDM complexity

WDM 手寫 PnP/power/selective suspend/IRP queue 非常容易有 race condition。這份 code 來自 Microsoft sample，功能面完整，但現代維護成本高。

建議：

- 若只需 bulk transfer，優先評估 WinUSB，避免自維 kernel driver。
- 若必須 kernel driver，重寫為 KMDF USB driver，使用 WDF queues、USB target pipe、power policy owner。

### 8.6 Protocol-level validation 不足

Driver 層只傳 bytes，不理解 CDAS command protocol。新增 CDAS3 workbook 證實協定層已經比現有 test app 複雜很多；user-mode test app 對 image total sector、firmware sector count、status/error code、data size、checksum 等資料信任度高，沒有完整範圍驗證。

建議：

- 對 `total_sector` 設定上限，避免 device 回傳異常造成長時間讀取或巨大檔案輸出。
- 對 firmware/image operation 加入 explicit confirmation、device state check、retry/backoff、timeout。
- 以 `Doc\CDAS3 New command_V16_AVISION.xlsx` 為基礎建立可版本控管的 protocol spec，避免只靠測試程式反推。
- 對 V16 error code 建立完整 decode table，讓工具能區分 CRC、FIFO overflow、unknown command、FX2/FPGA update failure 等狀態。

## 9. 維護性與程式品質

優點：

- Kernel driver 拆分清楚：`bulkpnp.c`、`bulkpwr.c`、`bulkrwr.c`、`bulkdev.c`、`bulkwmi.c`。
- PnP/power/WMI/selective suspend 都有完整 sample-level 實作。
- User-mode library 提供基本 API，讓 application 不必直接碰 SetupAPI。
- 保留 signed Win7 package 與 build/signing guide，有助於歷史追溯。

缺點：

- source tree 與 release binary 混放，版本來源不清。
- `src` 與 `src.wdk.vista` 重複，INF/driver name/device ID 不一致。
- 缺少 automated build/test。
- 沒有明確 changelog/release mapping。
- 測試工具是 hard-coded maintenance program，不是可靠 CLI。
- 現代 Windows driver signing/deployment 資訊不足。
- protocol specification 現已出現在 `Doc\CDAS3 New command_V16_AVISION.xlsx`，但尚未和 source code/API/測試工具同步。
- 沒有 threat model、API contract、release-to-protocol mapping。

## 10. 功能資料流

典型 user-mode 讀取流程：

1. Application 呼叫 `OpenBulkUSB(0)`。
2. Library 用 SetupAPI 找 `GUID_CLASS_DOCKING_SYSTEM` device interface。
3. Library 比對 hardware ID，偏好 CDAS2，否則 CDAS1。
4. Library 開啟 device interface path，組合 `\PIPE00`。
5. Driver `IRP_MJ_CREATE` 解析 `PIPE00`，把 file object 綁到 USB pipe 0。
6. Application 呼叫 `ReadFile()`。
7. Driver `IRP_MJ_READ` 轉成 USB bulk/interrupt URB。
8. USB stack 完成後，driver completion routine 設定 byte count 並完成 IRP。

典型 command operation：

1. Test app 開 `PIPE01` 寫 command packet。
2. 若命令需要資料上傳，從 `PIPE00` 讀資料；若需要寫入，往 `PIPE01` 寫 payload。
3. 從 `PIPE00` 讀 32 bytes response。
4. `VerifyCmdStatus()` 比對 tag 與 status。

CDAS3 V16 spec 下的更完整分層：

1. Product app 或 maintenance CLI 依 command registry 建立 `USBC` command。
2. User-mode USB layer 將 command/payload 寫到 bulk-out pipe。
3. Kernel driver 只把 bytes 轉送到 USB stack，不解析 command。
4. FX2/FPGA 收到 command 後，視 command type 與 capsule、flash、EEPROM、FPGA register 或 sensor/status path 互動。
5. 裝置透過 bulk-in pipe 回傳 data 或 `USBS` status。
6. User-mode protocol layer decode status/error code，並根據 V16 spec 決定 retry、fail、或進入下一段 transfer。

## 11. 現代化建議

### 11.1 短期：整理與風險降低

1. 明確標記 canonical source：決定 `src` 或 `src.wdk.vista` 哪個是主線。
2. 更新 README：列出支援 OS、支援 VID/PID、build toolchain、最後有效 release package。
3. 把舊 binary 移到 `releases/legacy/` 或外部 release storage，source tree 保持清楚。
4. 修正 `GetUSBDeviceNum()` 的 HDEVINFO leak。
5. 把 `wcscpy()` 改成 bounded copy 或 `std::wstring`。
6. 統一同步/overlapped I/O 模式。
7. 把 `Doc\CDAS3 New command_V16_AVISION.xlsx` 轉成版本控管文字格式，建立 command/error enum。
8. 補上 basic CLI：列裝置、讀 config descriptor、reset pipe、簡單 loopback、decode V16 status。

### 11.2 中期：安全與部署改善

1. INF 補上 device/interface ACL。
2. IOCTL access bits 改為較嚴格權限。
3. 建立 reproducible build script，例如 PowerShell + WDK environment validation。
4. 產出 SBOM 或至少 release manifest：hash、簽章狀態、INF 對應 device IDs。
5. 增加 driver verifier 測試流程記錄。
6. 使用 HLK/InfVerif/StampInf/SignTool 的現代流程檢查 package。
7. 為 CDAS3 command 建立 protocol conformance tests，至少覆蓋 command packing、status decode、error mapping、data size/checksum validation。

### 11.3 長期：架構重建

選項 A：改用 WinUSB

- 適用情境：裝置只需要 user-mode bulk transfer，不需要 kernel-mode 特殊邏輯。
- 優點：免維護 kernel driver，安全性與部署簡化，Windows 10/11 支援更好。
- 代價：需要更新 INF 綁定 WinUSB，user-mode API 改用 WinUSB API/libusb。

選項 B：重寫 KMDF USB driver

- 適用情境：必須保留 kernel-mode driver，例如特殊 power policy、kernel client、或需要底層控制。
- 優點：PnP/power/queue/USB pipe 管理更安全，維護性大幅提升。
- 代價：初期重寫成本較高，需要重新測試與簽章。

選項 C：維持 WDM，只做最小維護

- 適用情境：只支援舊 Win7 裝置，不打算支援現代 Windows。
- 優點：最小改動。
- 風險：人才與工具鏈稀缺，driver verifier / security review 壓力高，未來平台相容性差。

建議排序：若硬體 protocol 允許，優先 WinUSB；否則 KMDF；只有在明確限定舊平台時才維持 WDM。

## 12. 優先修復清單

P0 - 版本與部署正確性：

- 確認 `PID_931C` vs `PID_941C` 哪個正確，修正或註記 `src\sys\capsousb.inf`。
- 確認 `installer.src\InstCaps32/64` 是否為最後正式 package。
- 建立 release manifest，列出每個 `.sys` / `.cat` / `.inf` 的用途、簽章狀態與支援 OS。
- 補上 driver source、installer package、CDAS command spec V16 之間的版本對照，避免誤以為 kernel driver 已完整實作 CDAS3 protocol。

P1 - 安全性：

- 調整 IOCTL access bits，reset 類操作不可 `FILE_ANY_ACCESS`。
- INF 加入明確 device/interface ACL。
- 移除或隔離未簽章工具與舊二進位。

P1 - User-mode library 穩定性：

- 修正 HDEVINFO leak。
- 移除 global mutable state，改成 context-based API。
- 修正 `wcscpy()`/固定 buffer 風險。
- 統一 overlapped I/O 使用方式。

P2 - Tooling：

- 增加 `build-driver.ps1` 與 `verify-package.ps1`。
- 增加 `pnputil`/driver package install guide。
- 把 `capsousb_test` 改成真正 CLI，並以 V16 workbook 的 command/error table 產生 help、enum 與 status decode。

P2 - 文件：

- 把 xlsx command protocol spec 轉成 repo-native Markdown/YAML/JSON，並保留 xlsx 作為原始來源或歷史附件。
- 補硬體版本 mapping。
- 補 Windows support matrix。
- 補 troubleshooting guide。

## 13. 結論

這個 project 的核心價值在於保留了 CDAS docking system legacy USB driver 的完整脈絡：kernel driver source、user-mode helper、測試工具、安裝包、簽章指南，以及新增的 CDAS3 command protocol V16。它對追溯舊產品、重新建置 Win7 時代 driver、理解 CDAS USB bulk transport 與上層 command protocol 都很有用。

但如果目標是未來維護或支援 Windows 10/11，現狀不適合作為長期基礎。最大問題不是單一 bug，而是整體技術棧停在 WDM/WDK 7600/DPInst/cross-signing 時代，而且 command spec 已經比現有測試工具更完整。建議先做版本整理與風險收斂，再把 CDAS3 V16 protocol 轉成可測試、可產生程式碼的規格，最後評估 WinUSB 或 KMDF 現代化。若只是維持既有 Win7 客戶，則應把可部署 package、簽章狀態、支援硬體 ID、build/signing 流程與 command spec version 文件化，避免未來無法判斷哪份二進位、哪份 INF、哪版 protocol 才是正確 release。
