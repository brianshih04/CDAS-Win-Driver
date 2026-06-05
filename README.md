# CDAS Windows Driver

Legacy Windows USB driver package for the CapsoVision Capsule Data Access System (CDAS) / docking system.

This repository contains:

- WDM Bulk USB kernel driver sources under `src/sys`.
- User-mode C++ helper library under `src/lib`.
- Console test and maintenance utilities under `src/exe`.
- Windows driver installation packages for XP/Vista/Windows 7 era systems.
- Auto-detect installer sources and binary installer packages.

The driver is based on the Microsoft BulkUsb DDK/WDK sample and customized for CapsoVision CDAS devices.

Known device IDs include:

- `USB\VID_03EB&PID_941C` - CapsoVision Capsule Data Access System 1
- `USB\VID_0638&PID_0931` - CapsoVision Capsule Data Access System 2
- `USB\VID_03EB&PID_952C` - CapsoVision production test system

Note: signing key material is intentionally excluded from the repository.
