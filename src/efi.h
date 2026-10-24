/* Minimal UEFI types: only what YoroOS uses. Layout follows the UEFI 2.x spec. */
#ifndef YORO_EFI_H
#define YORO_EFI_H

typedef unsigned char      u8;
typedef unsigned short     u16;
typedef unsigned int       u32;
typedef unsigned long long u64;

typedef u64 EFI_STATUS;
typedef void *EFI_HANDLE;

typedef struct {
    u32 Data1;
    u16 Data2;
    u16 Data3;
    u8  Data4[8];
} EFI_GUID;

typedef struct {
    u64 Signature;
    u32 Revision;
    u32 HeaderSize;
    u32 CRC32;
    u32 Reserved;
} EFI_TABLE_HEADER;

typedef struct {
    u32 Version;
    u32 HorizontalResolution;
    u32 VerticalResolution;
    u32 PixelFormat;          /* 0 = RGB 8:8:8, 1 = BGR 8:8:8 */
    u32 PixelInformation[4];
    u32 PixelsPerScanLine;
} EFI_GOP_MODE_INFO;

typedef struct {
    u32                MaxMode;
    u32                Mode;
    EFI_GOP_MODE_INFO *Info;
    u64                SizeOfInfo;
    u64                FrameBufferBase;
    u64                FrameBufferSize;
} EFI_GOP_MODE;

typedef struct {
    void        *QueryMode;
    void        *SetMode;
    void        *Blt;
    EFI_GOP_MODE *Mode;
} EFI_GRAPHICS_OUTPUT_PROTOCOL;

/* Boot services table. Field order must match the spec exactly (checked below). */
typedef struct {
    EFI_TABLE_HEADER Hdr;
    void *RaiseTPL, *RestoreTPL, *AllocatePages, *FreePages, *GetMemoryMap;
    void *AllocatePool, *FreePool, *CreateEvent, *SetTimer, *WaitForEvent;
    void *SignalEvent, *CloseEvent, *CheckEvent, *InstallProtocolInterface;
    void *ReinstallProtocolInterface, *UninstallProtocolInterface, *HandleProtocol;
    void *Reserved, *RegisterProtocolNotify, *LocateHandle, *LocateDevicePath;
    void *InstallConfigurationTable, *LoadImage, *StartImage, *Exit, *UnloadImage;
    void *ExitBootServices, *GetNextMonotonicCount, *Stall, *SetWatchdogTimer;
    void *ConnectController, *DisconnectController, *OpenProtocol, *CloseProtocol;
    void *OpenProtocolInformation, *ProtocolsPerHandle, *LocateHandleBuffer;
    void *LocateProtocol, *InstallMultipleProtocolInterfaces;
    void *UninstallMultipleProtocolInterfaces, *CalculateCrc32, *CopyMem, *SetMem;
    void *CreateEventEx;
} EFI_BOOT_SERVICES;

typedef struct {
    EFI_TABLE_HEADER    Hdr;
    u16                *FirmwareVendor;
    u32                 FirmwareRevision;
    EFI_HANDLE          ConsoleInHandle;
    void               *ConIn;
    EFI_HANDLE          ConsoleOutHandle;
    void               *ConOut;
    EFI_HANDLE          StandardErrorHandle;
    void               *StdErr;
    void               *RuntimeServices;
    EFI_BOOT_SERVICES  *BootServices;
    u64                 NumberOfTableEntries;
    void               *ConfigurationTable;
} EFI_SYSTEM_TABLE;

/* Offsets from the UEFI spec: header is 24 bytes, each slot is 8 bytes. */
_Static_assert(__builtin_offsetof(EFI_BOOT_SERVICES, Stall) == 248, "Stall offset");
_Static_assert(__builtin_offsetof(EFI_BOOT_SERVICES, LocateProtocol) == 320, "LocateProtocol offset");
_Static_assert(__builtin_offsetof(EFI_SYSTEM_TABLE, BootServices) == 96, "BootServices offset");

#endif
