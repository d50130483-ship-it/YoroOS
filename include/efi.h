/* YoroOS 0.2 - Self-contained UEFI 2.10 + Microsoft Hyper-V Gen 2 (MsvmPkg) Protocol Definitions */
#ifndef YORO_EFI_H
#define YORO_EFI_H

#include <stdint.h>
#include <stddef.h>

#if defined(__x86_64__)
#define EFIAPI __attribute__((ms_abi))
#else
#define EFIAPI
#endif

typedef uint64_t  UINTN;
typedef int64_t   INTN;
typedef uint64_t  EFI_STATUS;
typedef void     *EFI_HANDLE;
typedef void     *EFI_EVENT;
typedef uint64_t  EFI_LBA;
typedef uint64_t  EFI_PHYSICAL_ADDRESS;
typedef uint64_t  EFI_TPL;
typedef uint16_t  CHAR16;
typedef uint8_t   BOOLEAN;

#define EFI_SUCCESS               0ULL
#define EFI_ERR_BIT               0x8000000000000000ULL
#define EFI_ERROR(s)              (((s) & EFI_ERR_BIT) != 0)
#define EFI_LOAD_ERROR            (EFI_ERR_BIT | 1ULL)
#define EFI_INVALID_PARAMETER     (EFI_ERR_BIT | 2ULL)
#define EFI_UNSUPPORTED           (EFI_ERR_BIT | 3ULL)
#define EFI_BAD_BUFFER_SIZE       (EFI_ERR_BIT | 4ULL)
#define EFI_BUFFER_TOO_SMALL      (EFI_ERR_BIT | 5ULL)
#define EFI_NOT_READY             (EFI_ERR_BIT | 6ULL)
#define EFI_DEVICE_ERROR          (EFI_ERR_BIT | 7ULL)
#define EFI_WRITE_PROTECTED       (EFI_ERR_BIT | 8ULL)
#define EFI_OUT_OF_RESOURCES      (EFI_ERR_BIT | 9ULL)
#define EFI_NOT_FOUND             (EFI_ERR_BIT | 14ULL)
#define EFI_ALREADY_STARTED       (EFI_ERR_BIT | 20ULL)

#define TPL_APPLICATION           4ULL
#define TPL_CALLBACK              8ULL
#define TPL_NOTIFY                16ULL
#define TPL_HIGH_LEVEL            31ULL

#define EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL  0x00000001U
#define EFI_OPEN_PROTOCOL_GET_PROTOCOL        0x00000002U
#define EFI_OPEN_PROTOCOL_TEST_PROTOCOL       0x00000004U
#define EFI_OPEN_PROTOCOL_BY_DRIVER           0x00000010U

typedef struct {
    uint32_t Data1;
    uint16_t Data2;
    uint16_t Data3;
    uint8_t  Data4[8];
} EFI_GUID;

typedef struct {
    uint64_t Signature;
    uint32_t Revision;
    uint32_t HeaderSize;
    uint32_t CRC32;
    uint32_t Reserved;
} EFI_TABLE_HEADER;

typedef enum {
    AllocateAnyPages = 0,
    AllocateMaxAddress = 1,
    AllocateAddress = 2,
    MaxAllocateType = 3
} EFI_ALLOCATE_TYPE;

typedef enum {
    EfiReservedMemoryType = 0,
    EfiLoaderCode = 1,
    EfiLoaderData = 2,
    EfiBootServicesCode = 3,
    EfiBootServicesData = 4
} EFI_MEMORY_TYPE;

typedef enum {
    AllHandles = 0,
    ByRegisterNotify = 1,
    ByProtocol = 2
} EFI_LOCATE_SEARCH_TYPE;

typedef enum {
    EfiResetCold = 0,
    EfiResetWarm = 1,
    EfiResetShutdown = 2,
    EfiResetPlatformSpecific = 3
} EFI_RESET_TYPE;

typedef struct {
    uint16_t Year;
    uint8_t  Month;
    uint8_t  Day;
    uint8_t  Hour;
    uint8_t  Minute;
    uint8_t  Second;
    uint8_t  Pad1;
    uint32_t Nanosecond;
    int16_t  TimeZone;
    uint8_t  Daylight;
    uint8_t  Pad2;
} EFI_TIME;

/* ---------------- Console In / Ex ---------------- */
typedef struct {
    uint16_t ScanCode;
    CHAR16   UnicodeChar;
} EFI_INPUT_KEY;

typedef struct _EFI_SIMPLE_TEXT_INPUT_PROTOCOL EFI_SIMPLE_TEXT_INPUT_PROTOCOL;
struct _EFI_SIMPLE_TEXT_INPUT_PROTOCOL {
    EFI_STATUS (EFIAPI *Reset)(EFI_SIMPLE_TEXT_INPUT_PROTOCOL *This, BOOLEAN ExtendedVerification);
    EFI_STATUS (EFIAPI *ReadKeyStroke)(EFI_SIMPLE_TEXT_INPUT_PROTOCOL *This, EFI_INPUT_KEY *Key);
    EFI_EVENT  WaitForKey;
};

#define EFI_SHIFT_STATE_VALID     0x80000000U
#define EFI_RIGHT_SHIFT_PRESSED   0x00000001U
#define EFI_LEFT_SHIFT_PRESSED    0x00000002U
#define EFI_RIGHT_CONTROL_PRESSED 0x00000004U
#define EFI_LEFT_CONTROL_PRESSED  0x00000008U
#define EFI_RIGHT_ALT_PRESSED     0x00000010U
#define EFI_LEFT_ALT_PRESSED      0x00000020U
#define EFI_RIGHT_LOGO_PRESSED    0x00000040U
#define EFI_LEFT_LOGO_PRESSED     0x00000080U
#define EFI_MENU_KEY_PRESSED      0x00000100U
#define EFI_SYS_REQ_PRESSED       0x00000200U

typedef struct {
    uint32_t KeyShiftState;
    uint8_t  KeyToggleState;
} EFI_KEY_STATE;

typedef struct {
    EFI_INPUT_KEY Key;
    EFI_KEY_STATE KeyState;
} EFI_KEY_DATA;

typedef struct _EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL;
struct _EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL {
    EFI_STATUS (EFIAPI *Reset)(EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL *This, BOOLEAN ExtendedVerification);
    EFI_STATUS (EFIAPI *ReadKeyStrokeEx)(EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL *This, EFI_KEY_DATA *KeyData);
    EFI_EVENT  WaitForKeyEx;
    EFI_STATUS (EFIAPI *SetState)(EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL *This, uint8_t *KeyToggleState);
    EFI_STATUS (EFIAPI *RegisterKeyNotify)(EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL *This, EFI_KEY_DATA *KeyData, void *Fn, void **Handle);
    EFI_STATUS (EFIAPI *UnregisterKeyNotify)(EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL *This, void *Handle);
};

typedef struct _EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;
struct _EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL {
    EFI_STATUS (EFIAPI *Reset)(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This, BOOLEAN ExtendedVerification);
    EFI_STATUS (EFIAPI *OutputString)(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This, CHAR16 *String);
    void *TestString;
    void *QueryMode;
    void *SetMode;
    void *SetAttribute;
    EFI_STATUS (EFIAPI *ClearScreen)(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This);
    void *SetCursorPosition;
    EFI_STATUS (EFIAPI *EnableCursor)(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This, BOOLEAN Visible);
    void *Mode;
};

/* ---------------- Graphics Output Protocol (GOP) ---------------- */
typedef enum {
    PixelRedGreenBlueReserved8BitPerColor = 0,
    PixelBlueGreenRedReserved8BitPerColor = 1,
    PixelBitMask = 2,
    PixelBltOnly = 3,
    PixelFormatMax = 4
} EFI_GRAPHICS_PIXEL_FORMAT;

typedef struct {
    uint32_t RedMask;
    uint32_t GreenMask;
    uint32_t BlueMask;
    uint32_t ReservedMask;
} EFI_PIXEL_BITMASK;

typedef struct {
    uint32_t                  Version;
    uint32_t                  HorizontalResolution;
    uint32_t                  VerticalResolution;
    EFI_GRAPHICS_PIXEL_FORMAT PixelFormat;
    EFI_PIXEL_BITMASK         PixelInformation;
    uint32_t                  PixelsPerScanLine;
} EFI_GRAPHICS_OUTPUT_MODE_INFORMATION;

typedef struct {
    uint32_t                              MaxMode;
    uint32_t                              Mode;
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *Info;
    UINTN                                 SizeOfInfo;
    EFI_PHYSICAL_ADDRESS                  FrameBufferBase;
    UINTN                                 FrameBufferSize;
} EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE;

typedef struct {
    uint8_t Blue;
    uint8_t Green;
    uint8_t Red;
    uint8_t Reserved;
} EFI_GRAPHICS_OUTPUT_BLT_PIXEL;

typedef enum {
    EfiBltVideoFill = 0,
    EfiBltVideoToBltBuffer = 1,
    EfiBltBufferToVideo = 2,
    EfiBltVideoToVideo = 3,
    EfiGraphicsOutputBltOperationMax = 4
} EFI_GRAPHICS_OUTPUT_BLT_OPERATION;

typedef struct _EFI_GRAPHICS_OUTPUT_PROTOCOL EFI_GRAPHICS_OUTPUT_PROTOCOL;
struct _EFI_GRAPHICS_OUTPUT_PROTOCOL {
    EFI_STATUS (EFIAPI *QueryMode)(EFI_GRAPHICS_OUTPUT_PROTOCOL *This, uint32_t ModeNumber,
                                   UINTN *SizeOfInfo, EFI_GRAPHICS_OUTPUT_MODE_INFORMATION **Info);
    EFI_STATUS (EFIAPI *SetMode)(EFI_GRAPHICS_OUTPUT_PROTOCOL *This, uint32_t ModeNumber);
    EFI_STATUS (EFIAPI *Blt)(EFI_GRAPHICS_OUTPUT_PROTOCOL *This,
                             EFI_GRAPHICS_OUTPUT_BLT_PIXEL *BltBuffer,
                             EFI_GRAPHICS_OUTPUT_BLT_OPERATION BltOperation,
                             UINTN SourceX, UINTN SourceY,
                             UINTN DestinationX, UINTN DestinationY,
                             UINTN Width, UINTN Height, UINTN Delta);
    EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE *Mode;
};

/* ---------------- Pointer Protocols (SimplePointer + AbsolutePointer) ---------------- */
typedef struct {
    int32_t RelativeMovementX;
    int32_t RelativeMovementY;
    int32_t RelativeMovementZ;
    BOOLEAN LeftButton;
    BOOLEAN RightButton;
} EFI_SIMPLE_POINTER_STATE;

typedef struct {
    uint64_t ResolutionX;
    uint64_t ResolutionY;
    uint64_t ResolutionZ;
    BOOLEAN  LeftButton;
    BOOLEAN  RightButton;
} EFI_SIMPLE_POINTER_MODE;

typedef struct _EFI_SIMPLE_POINTER_PROTOCOL EFI_SIMPLE_POINTER_PROTOCOL;
struct _EFI_SIMPLE_POINTER_PROTOCOL {
    EFI_STATUS (EFIAPI *Reset)(EFI_SIMPLE_POINTER_PROTOCOL *This, BOOLEAN ExtendedVerification);
    EFI_STATUS (EFIAPI *GetState)(EFI_SIMPLE_POINTER_PROTOCOL *This, EFI_SIMPLE_POINTER_STATE *State);
    EFI_EVENT  WaitForInput;
    EFI_SIMPLE_POINTER_MODE *Mode;
};

typedef struct {
    uint64_t CurrentX;
    uint64_t CurrentY;
    uint64_t CurrentZ;
    uint32_t ActiveButtons;
} EFI_ABSOLUTE_POINTER_STATE;

typedef struct {
    uint64_t AbsoluteMinX;
    uint64_t AbsoluteMinY;
    uint64_t AbsoluteMinZ;
    uint64_t AbsoluteMaxX;
    uint64_t AbsoluteMaxY;
    uint64_t AbsoluteMaxZ;
    uint32_t Attributes;
} EFI_ABSOLUTE_POINTER_MODE;

typedef struct _EFI_ABSOLUTE_POINTER_PROTOCOL EFI_ABSOLUTE_POINTER_PROTOCOL;
struct _EFI_ABSOLUTE_POINTER_PROTOCOL {
    EFI_STATUS (EFIAPI *Reset)(EFI_ABSOLUTE_POINTER_PROTOCOL *This, BOOLEAN ExtendedVerification);
    EFI_STATUS (EFIAPI *GetState)(EFI_ABSOLUTE_POINTER_PROTOCOL *This, EFI_ABSOLUTE_POINTER_STATE *State);
    EFI_EVENT  WaitForInput;
    EFI_ABSOLUTE_POINTER_MODE *Mode;
};

/* ---------------- Block I/O & Loaded Image & Device Path ---------------- */
typedef struct {
    uint32_t MediaId;
    BOOLEAN  RemovableMedia;
    BOOLEAN  MediaPresent;
    BOOLEAN  LogicalPartition;
    BOOLEAN  ReadOnly;
    BOOLEAN  WriteCaching;
    uint32_t BlockSize;
    uint32_t IoAlign;
    EFI_LBA  LastBlock;
} EFI_BLOCK_IO_MEDIA;

typedef struct _EFI_BLOCK_IO_PROTOCOL EFI_BLOCK_IO_PROTOCOL;
struct _EFI_BLOCK_IO_PROTOCOL {
    uint64_t            Revision;
    EFI_BLOCK_IO_MEDIA *Media;
    EFI_STATUS (EFIAPI *Reset)(EFI_BLOCK_IO_PROTOCOL *This, BOOLEAN ExtendedVerification);
    EFI_STATUS (EFIAPI *ReadBlocks)(EFI_BLOCK_IO_PROTOCOL *This, uint32_t MediaId,
                                    EFI_LBA Lba, UINTN BufferSize, void *Buffer);
    EFI_STATUS (EFIAPI *WriteBlocks)(EFI_BLOCK_IO_PROTOCOL *This, uint32_t MediaId,
                                     EFI_LBA Lba, UINTN BufferSize, void *Buffer);
    EFI_STATUS (EFIAPI *FlushBlocks)(EFI_BLOCK_IO_PROTOCOL *This);
};

typedef struct {
    uint8_t Type;
    uint8_t SubType;
    uint8_t Length[2];
} EFI_DEVICE_PATH_PROTOCOL;

typedef struct {
    uint32_t        Revision;
    EFI_HANDLE      ParentHandle;
    void           *SystemTable;
    EFI_HANDLE      DeviceHandle;
    EFI_DEVICE_PATH_PROTOCOL *FilePath;
    void           *Reserved;
    uint32_t        LoadOptionsSize;
    void           *LoadOptions;
    void           *ImageBase;
    uint64_t        ImageSize;
    EFI_MEMORY_TYPE ImageCodeType;
    EFI_MEMORY_TYPE ImageDataType;
    void           *Unload;
} EFI_LOADED_IMAGE_PROTOCOL;

typedef struct _EFI_DRIVER_BINDING_PROTOCOL EFI_DRIVER_BINDING_PROTOCOL;
struct _EFI_DRIVER_BINDING_PROTOCOL {
    EFI_STATUS (EFIAPI *Supported)(EFI_DRIVER_BINDING_PROTOCOL *This, EFI_HANDLE Controller, EFI_DEVICE_PATH_PROTOCOL *Rem);
    EFI_STATUS (EFIAPI *Start)(EFI_DRIVER_BINDING_PROTOCOL *This, EFI_HANDLE Controller, EFI_DEVICE_PATH_PROTOCOL *Rem);
    EFI_STATUS (EFIAPI *Stop)(EFI_DRIVER_BINDING_PROTOCOL *This, EFI_HANDLE Controller, UINTN NumChildren, EFI_HANDLE *Children);
    uint32_t   Version;
    EFI_HANDLE ImageHandle;
    EFI_HANDLE DriverBindingHandle;
};

/* ---------------- Hyper-V Generation 2 MsvmPkg EMCL & VMBus Protocols ---------------- */
#pragma pack(push, 1)
typedef struct {
    EFI_DEVICE_PATH_PROTOCOL Header;
    EFI_GUID                 VendorGuid;
    EFI_GUID                 InterfaceType;
    EFI_GUID                 InterfaceInstance;
} HV_VMBUS_DEVICE_PATH;
#pragma pack(pop)

typedef struct {
    uint32_t ByteCount;
    uint32_t ByteOffset;
} EFI_TRANSFER_RANGE;

typedef struct {
    void    *Buffer;
    uint32_t BufferSize;
} EFI_EXTERNAL_BUFFER;

typedef void (EFIAPI *EFI_EMCL_COMPLETION_ROUTINE)(void *Context, void *Buffer, uint32_t BufferLength);
typedef void (EFIAPI *EFI_EMCL_RECEIVE_PACKET)(void *ReceiveContext, void *PacketContext, void *Buffer,
                                               uint32_t BufferLength, uint16_t TransferPageSetId,
                                               uint32_t RangeCount, EFI_TRANSFER_RANGE *Ranges);

typedef struct _EFI_EMCL_PROTOCOL EFI_EMCL_PROTOCOL;
struct _EFI_EMCL_PROTOCOL {
    EFI_STATUS (EFIAPI *StartChannel)(EFI_EMCL_PROTOCOL *This, uint32_t IncomingPages, uint32_t OutgoingPages);
    void       (EFIAPI *StopChannel)(EFI_EMCL_PROTOCOL *This);
    EFI_STATUS (EFIAPI *SendPacket)(EFI_EMCL_PROTOCOL *This, void *InlineBuffer, uint32_t InlineBufferLength,
                                    EFI_EXTERNAL_BUFFER *ExternalBuffers, uint32_t ExternalBufferCount,
                                    EFI_EMCL_COMPLETION_ROUTINE CompletionRoutine, void *CompletionContext);
    EFI_STATUS (EFIAPI *CompletePacket)(EFI_EMCL_PROTOCOL *This, void *PacketContext, void *Buffer, uint32_t BufferLength);
    EFI_STATUS (EFIAPI *SetReceiveCallback)(EFI_EMCL_PROTOCOL *This, EFI_EMCL_RECEIVE_PACKET ReceiveCallback,
                                            void *ReceiveContext, EFI_TPL Tpl);
    void *CreateGpadl;
    void *DestroyGpadl;
    void *GetGpadlHandle;
    void *GetGpadlBuffer;
    void *CreateGpaRange;
    void *DestroyGpaRange;
};

/* ---------------- EFI Runtime & Boot Services Tables ---------------- */
typedef struct {
    EFI_TABLE_HEADER Hdr;
    EFI_STATUS (EFIAPI *GetTime)(EFI_TIME *Time, void *Capabilities);
    EFI_STATUS (EFIAPI *SetTime)(EFI_TIME *Time);
    void *GetWakeupTime;
    void *SetWakeupTime;
    void *SetVirtualAddressMap;
    void *ConvertPointer;
    EFI_STATUS (EFIAPI *GetVariable)(CHAR16 *Name, EFI_GUID *VendorGuid, uint32_t *Attributes, UINTN *DataSize, void *Data);
    void *GetNextVariableName;
    EFI_STATUS (EFIAPI *SetVariable)(CHAR16 *Name, EFI_GUID *VendorGuid, uint32_t Attributes, UINTN DataSize, void *Data);
    void *GetNextHighMonotonicCount;
    void (EFIAPI *ResetSystem)(EFI_RESET_TYPE ResetType, EFI_STATUS ResetStatus, UINTN DataSize, void *ResetData);
} EFI_RUNTIME_SERVICES;

typedef struct {
    EFI_TABLE_HEADER Hdr;
    EFI_TPL    (EFIAPI *RaiseTPL)(EFI_TPL NewTpl);
    void       (EFIAPI *RestoreTPL)(EFI_TPL OldTpl);
    EFI_STATUS (EFIAPI *AllocatePages)(EFI_ALLOCATE_TYPE Type, EFI_MEMORY_TYPE MemoryType, UINTN Pages, EFI_PHYSICAL_ADDRESS *Memory);
    EFI_STATUS (EFIAPI *FreePages)(EFI_PHYSICAL_ADDRESS Memory, UINTN Pages);
    void *GetMemoryMap;
    EFI_STATUS (EFIAPI *AllocatePool)(EFI_MEMORY_TYPE PoolType, UINTN Size, void **Buffer);
    EFI_STATUS (EFIAPI *FreePool)(void *Buffer);
    EFI_STATUS (EFIAPI *CreateEvent)(uint32_t Type, EFI_TPL NotifyTpl, void *NotifyFunction, void *NotifyContext, EFI_EVENT *Event);
    EFI_STATUS (EFIAPI *SetTimer)(EFI_EVENT Event, int Type, uint64_t TriggerTime);
    EFI_STATUS (EFIAPI *WaitForEvent)(UINTN NumberOfEvents, EFI_EVENT *Event, UINTN *Index);
    EFI_STATUS (EFIAPI *SignalEvent)(EFI_EVENT Event);
    EFI_STATUS (EFIAPI *CloseEvent)(EFI_EVENT Event);
    EFI_STATUS (EFIAPI *CheckEvent)(EFI_EVENT Event);
    void *InstallProtocolInterface;
    void *ReinstallProtocolInterface;
    void *UninstallProtocolInterface;
    EFI_STATUS (EFIAPI *HandleProtocol)(EFI_HANDLE Handle, EFI_GUID *Protocol, void **Interface);
    void *Reserved;
    void *RegisterProtocolNotify;
    EFI_STATUS (EFIAPI *LocateHandle)(EFI_LOCATE_SEARCH_TYPE SearchType, EFI_GUID *Protocol, void *SearchKey, UINTN *BufferSize, EFI_HANDLE *Buffer);
    void *LocateDevicePath;
    void *InstallConfigurationTable;
    void *LoadImage;
    void *StartImage;
    void *Exit;
    void *UnloadImage;
    void *ExitBootServices;
    void *GetNextMonotonicCount;
    EFI_STATUS (EFIAPI *Stall)(UINTN Microseconds);
    EFI_STATUS (EFIAPI *SetWatchdogTimer)(UINTN Timeout, uint64_t WatchdogCode, UINTN DataSize, CHAR16 *WatchdogData);
    EFI_STATUS (EFIAPI *ConnectController)(EFI_HANDLE ControllerHandle, EFI_HANDLE *DriverImageHandle, EFI_DEVICE_PATH_PROTOCOL *RemainingDevicePath, BOOLEAN Recursive);
    EFI_STATUS (EFIAPI *DisconnectController)(EFI_HANDLE ControllerHandle, EFI_HANDLE DriverImageHandle, EFI_HANDLE ChildHandle);
    EFI_STATUS (EFIAPI *OpenProtocol)(EFI_HANDLE Handle, EFI_GUID *Protocol, void **Interface, EFI_HANDLE AgentHandle, EFI_HANDLE ControllerHandle, uint32_t Attributes);
    EFI_STATUS (EFIAPI *CloseProtocol)(EFI_HANDLE Handle, EFI_GUID *Protocol, EFI_HANDLE AgentHandle, EFI_HANDLE ControllerHandle);
    void *OpenProtocolInformation;
    void *ProtocolsPerHandle;
    EFI_STATUS (EFIAPI *LocateHandleBuffer)(EFI_LOCATE_SEARCH_TYPE SearchType, EFI_GUID *Protocol, void *SearchKey, UINTN *NoHandles, EFI_HANDLE **Buffer);
    EFI_STATUS (EFIAPI *LocateProtocol)(EFI_GUID *Protocol, void *Registration, void **Interface);
    void *InstallMultipleProtocolInterfaces;
    void *UninstallMultipleProtocolInterfaces;
    void *CalculateCrc32;
    void *CopyMem;
    void *SetMem;
    void *CreateEventEx;
} EFI_BOOT_SERVICES;

typedef struct {
    EFI_TABLE_HEADER                 Hdr;
    CHAR16                          *FirmwareVendor;
    uint32_t                         FirmwareRevision;
    EFI_HANDLE                       ConsoleInHandle;
    EFI_SIMPLE_TEXT_INPUT_PROTOCOL  *ConIn;
    EFI_HANDLE                       ConsoleOutHandle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *ConOut;
    EFI_HANDLE                       StandardErrorHandle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *StdErr;
    EFI_RUNTIME_SERVICES            *RuntimeServices;
    EFI_BOOT_SERVICES               *BootServices;
    UINTN                            NumberOfTableEntries;
    void                            *ConfigurationTable;
} EFI_SYSTEM_TABLE;

#endif
