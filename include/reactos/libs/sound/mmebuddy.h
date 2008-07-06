/*
    ReactOS Sound System
    MME Support Helper

    Author:
        Andrew Greenwood (silverblade@reactos.org)

    History:
        4 July 2008 - Created

    Notes:
        MME Buddy was the best name I could come up with...

        The structures etc. here should be treated as internal to the
        library so should not be directly accessed elsewhere.
*/

#ifndef ROS_AUDIO_MMEBUDDY_H
#define ROS_AUDIO_MMEBUDDY_H

/*
    Hacky debug macro
*/

#define SOUND_DEBUG(x) \
    MessageBox(0, x, L"Debug", MB_OK | MB_TASKMODAL);

#define SOUND_DEBUG_HEX(x) \
    { \
        WCHAR dbgmsg[1024], dbgtitle[1024]; \
        wsprintf(dbgtitle, L"%hS[%d]", __FILE__, __LINE__); \
        wsprintf(dbgmsg, L"%hS == %x", #x, x); \
        MessageBox(0, dbgmsg, dbgtitle, MB_OK | MB_TASKMODAL); \
    }

#define SOUND_ASSERT(x) \
    { \
        if ( ! ( x ) ) \
        { \
            WCHAR dbgmsg[1024], dbgtitle[1024]; \
            wsprintf(dbgtitle, L"%hS[%d]", __FILE__, __LINE__); \
            wsprintf(dbgmsg, L"ASSERT FAILED:\n%hS", #x); \
        } \
    }


/*
    Some memory allocation helper macros
*/

/*
#define AllocateMemory(size) \
    HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size)

#define FreeMemory(ptr) \
    HeapFree(GetProcessHeap(), 0, ptr)
*/

#define AllocateMemoryFor(thing) \
    (thing*) AllocateMemory(sizeof(thing))

#define StringLengthToBytes(chartype, string_length) \
    ( ( string_length + 1 ) * sizeof(chartype) )

#define AllocateWideString(string_length) \
    (PWSTR) AllocateMemory(StringLengthToBytes(WCHAR, string_length))

#define ZeroWideString(string) \
    ZeroMemory(string, StringLengthToBytes(WCHAR, wcslen(string)))

#define CopyWideString(dest, source) \
    CopyMemory(dest, source, StringLengthToBytes(WCHAR, wcslen(source)))


struct _SOUND_DEVICE;
struct _SOUND_DEVICE_INSTANCE;


/*
    Rather than pass caps structures around as a PVOID, this can be
    used instead.
*/

typedef union _UNIVERSAL_CAPS
{
    WAVEOUTCAPS WaveOut;
    WAVEINCAPS WaveIn;
    MIDIOUTCAPS MidiOut;
    MIDIINCAPS MidiIn;
} UNIVERSAL_CAPS, *PUNIVERSAL_CAPS;


/*
    Used internally to shuttle data to/from the sound processing thread.
*/
typedef struct _THREAD_REQUEST
{
    struct _SOUND_DEVICE_INSTANCE* DeviceInstance;
    DWORD RequestId;
    PVOID Data;
    MMRESULT Result;
} THREAD_REQUEST, *PTHREAD_REQUEST;


/*
    Thread helper operations
*/
typedef MMRESULT (*SOUND_THREAD_REQUEST_HANDLER)(
    IN  struct _SOUND_DEVICE_INSTANCE* Instance,
    IN  DWORD RequestId,
    IN  PVOID Data);

typedef struct _SOUND_THREAD
{
    HANDLE Handle;
    BOOLEAN Running;
    SOUND_THREAD_REQUEST_HANDLER RequestHandler;
    HANDLE ReadyEvent;      /* Thread waiting for a request */
    HANDLE RequestEvent;    /* Caller sending a request */
    HANDLE DoneEvent;       /* Thread completed a request */
    THREAD_REQUEST Request;
} SOUND_THREAD, *PSOUND_THREAD;


/*
    Audio device function table
    TODO - create/destroy instance need to work
*/

typedef MMRESULT (*MMCREATEINSTANCE_FUNC)(
    IN  struct _SOUND_DEVICE_INSTANCE* SoundDeviceInstance);

typedef VOID (*MMDESTROYINSTANCE_FUNC)(
    IN  struct _SOUND_DEVICE_INSTANCE* SoundDeviceInstance);

typedef MMRESULT (*MMGETCAPS_FUNC)(
    IN  struct _SOUND_DEVICE* Device,
    OUT PUNIVERSAL_CAPS Capabilities);

typedef MMRESULT (*MMWAVEQUERYFORMAT_FUNC)(
    IN  struct _SOUND_DEVICE* Device,
    IN  PWAVEFORMATEX WaveFormat,
    IN  DWORD WaveFormatSize);

typedef MMRESULT (*MMWAVESETFORMAT_FUNC)(
    IN  struct _SOUND_DEVICE_INSTANCE* Instance,
    IN  PWAVEFORMATEX WaveFormat,
    IN  DWORD WaveFormatSize);

typedef MMRESULT (*MMWAVEQUEUEBUFFER_FUNC)(
    IN  struct _SOUND_DEVICE_INSTANCE* Instance,
    IN  PWAVEHDR WaveHeader);

typedef struct _MMFUNCTION_TABLE
{
    MMCREATEINSTANCE_FUNC   Constructor;
    MMDESTROYINSTANCE_FUNC  Destructor;
    MMGETCAPS_FUNC          GetCapabilities;

    MMWAVEQUERYFORMAT_FUNC  QueryWaveFormat;
    MMWAVESETFORMAT_FUNC    SetWaveFormat;
    MMWAVEQUEUEBUFFER_FUNC  QueueWaveBuffer;
} MMFUNCTION_TABLE, *PMMFUNCTION_TABLE;


/*
    Represents an audio device
*/

typedef struct _SOUND_DEVICE
{
    struct _SOUND_DEVICE* Next;
    struct _SOUND_DEVICE_INSTANCE* FirstInstance;
    UCHAR DeviceType;
    LPWSTR DevicePath;
    HANDLE Handle;
    MMFUNCTION_TABLE Functions;
} SOUND_DEVICE, *PSOUND_DEVICE;


/*
    Represents an individual instance of an audio device
*/

typedef struct _SOUND_DEVICE_INSTANCE
{
    struct _SOUND_DEVICE_INSTANCE* Next;
    PSOUND_DEVICE Device;
    PSOUND_THREAD Thread;

    /* Stuff generously donated to us from WinMM */
    struct
    {
        DWORD ClientCallback;
    } WinMM;

    /* Everything below this is used by the worker thread only */
    OVERLAPPED Overlapped;

    union
    {
        struct
        {
            DWORD BufferCount;
            PWAVEHDR CurrentBuffer;
            PWAVEHDR FirstBuffer;
            PWAVEHDR LastBuffer;
        } Wave;
    };
} SOUND_DEVICE_INSTANCE, *PSOUND_DEVICE_INSTANCE;


/*
    Thread requests
*/

#define THREADREQUEST_EXIT              0
#define WAVEREQUEST_QUEUE_BUFFER        1


/*
    entry.c
*/

LONG
DefaultDriverProc(
    DWORD driver_id,
    HANDLE driver_handle,
    UINT message,
    LONG parameter1,
    LONG parameter2);


/*
    devices.c
*/

ULONG
GetSoundDeviceCount(
    IN  UCHAR DeviceType);

MMRESULT
GetSoundDevice(
    IN  UCHAR DeviceType,
    IN  ULONG DeviceIndex,
    OUT PSOUND_DEVICE* Device);

MMRESULT
GetSoundDevicePath(
    IN  PSOUND_DEVICE SoundDevice,
    OUT LPWSTR* DevicePath);

BOOLEAN
AddSoundDevice(
    IN  UCHAR DeviceType,
    IN  PWSTR DevicePath);

MMRESULT
RemoveSoundDevice(
    IN  PSOUND_DEVICE SoundDevice);

MMRESULT
RemoveSoundDevices(
    IN  UCHAR DeviceType);

VOID
RemoveAllSoundDevices();

MMRESULT
GetSoundDeviceType(
    IN  PSOUND_DEVICE Device,
    OUT PUCHAR DeviceType);


/*
    nt4.c
*/

typedef BOOLEAN (*SOUND_DEVICE_DETECTED_PROC)(
    UCHAR DeviceType,
    PWSTR DevicePath,
    HANDLE Handle);

MMRESULT
OpenSoundDriverParametersRegKey(
    IN  LPWSTR ServiceName,
    OUT PHKEY KeyHandle);

MMRESULT
OpenSoundDeviceRegKey(
    IN  LPWSTR ServiceName,
    IN  DWORD DeviceIndex,
    OUT PHKEY KeyHandle);

MMRESULT
EnumerateNt4ServiceSoundDevices(
    IN  LPWSTR ServiceName,
    IN  UCHAR DeviceType,
    IN  SOUND_DEVICE_DETECTED_PROC SoundDeviceDetectedProc);

MMRESULT
DetectNt4SoundDevices(
    IN  UCHAR DeviceType,
    IN  PWSTR BaseDevicePath,
    IN  SOUND_DEVICE_DETECTED_PROC SoundDeviceDetectedProc);


/*
    kernel.c
*/

MMRESULT
OpenKernelSoundDeviceByName(
    IN  PWSTR DeviceName,
    IN  DWORD AccessRights,
    IN  PHANDLE Handle);

MMRESULT
OpenKernelSoundDevice(
    IN  PSOUND_DEVICE SoundDevice,
    IN  DWORD AccessRights);

MMRESULT
CloseKernelSoundDevice(
    PSOUND_DEVICE SoundDevice);

MMRESULT
PerformSoundDeviceIo(
    IN  PSOUND_DEVICE SoundDevice,
    IN  DWORD IoControlCode,
    IN  LPVOID InBuffer,
    IN  DWORD InBufferSize,
    OUT LPVOID OutBuffer,
    IN  DWORD OutBufferSize,
    OUT LPDWORD BytesReturned,
    IN  LPOVERLAPPED Overlapped);

MMRESULT
ReadSoundDevice(
    IN  PSOUND_DEVICE SoundDevice,
    IN  DWORD IoControlCode,
    OUT LPVOID OutBuffer,
    IN  DWORD OutBufferSize,
    OUT LPDWORD BytesReturned,
    IN  LPOVERLAPPED Overlapped);

MMRESULT
WriteSoundDevice(
    IN  PSOUND_DEVICE SoundDevice,
    IN  DWORD IoControlCode,
    IN  LPVOID InBuffer,
    IN  DWORD InBufferSize,
    OUT LPDWORD BytesReturned,
    IN  LPOVERLAPPED Overlapped);

MMRESULT
WriteSoundDeviceBuffer(
    IN  PSOUND_DEVICE_INSTANCE SoundDeviceInstance,
    IN  LPVOID Buffer,
    IN  DWORD BufferSize,
    IN  LPOVERLAPPED_COMPLETION_ROUTINE CompletionRoutine);


/*
    utility.c
*/

PVOID
AllocateMemory(
    IN  DWORD Size);

VOID
FreeMemory(
    IN  PVOID Pointer);

DWORD
GetMemoryAllocations();

ULONG
GetDigitCount(
    IN  ULONG Number);

MMRESULT
Win32ErrorToMmResult(IN UINT error_code);


/*
    instances.c
*/

MMRESULT
CreateSoundDeviceInstance(
    IN  PSOUND_DEVICE SoundDevice,
    OUT PSOUND_DEVICE_INSTANCE* Instance);

MMRESULT
DestroySoundDeviceInstance(
    IN  PSOUND_DEVICE_INSTANCE Instance);

MMRESULT
DestroyAllInstancesOfSoundDevice(
    IN  PSOUND_DEVICE SoundDevice);

MMRESULT
GetSoundDeviceFromInstance(
    IN  PSOUND_DEVICE_INSTANCE SoundDeviceInstance,
    OUT PSOUND_DEVICE* SoundDevice);


/*
    ...
*/

MMRESULT
GetSoundDeviceCapabilities(
    IN  PSOUND_DEVICE Device,
    OUT PUNIVERSAL_CAPS Capabilities);

MMRESULT
DefaultGetSoundDeviceCapabilities(
    IN  PSOUND_DEVICE Device,
    OUT PUNIVERSAL_CAPS Capabilities);

MMRESULT
QueryWaveDeviceFormatSupport(
    IN  PSOUND_DEVICE Device,
    IN  PWAVEFORMATEX WaveFormat,
    IN  DWORD WaveFormatSize);

MMRESULT
DefaultQueryWaveDeviceFormatSupport(
    IN  PSOUND_DEVICE Device,
    IN  PWAVEFORMATEX WaveFormat,
    IN  DWORD WaveFormatSize);

MMRESULT
SetWaveDeviceFormat(
    IN  PSOUND_DEVICE_INSTANCE Instance,
    IN  PWAVEFORMATEX WaveFormat,
    IN  DWORD WaveFormatSize);

MMRESULT
DefaultSetWaveDeviceFormat(
    IN  PSOUND_DEVICE_INSTANCE Instance,
    IN  PWAVEFORMATEX WaveFormat,
    IN  DWORD WaveFormatSize);

MMRESULT
DefaultInstanceConstructor(
    IN  struct _SOUND_DEVICE_INSTANCE* SoundDeviceInstance);

VOID
DefaultInstanceDestructor(
    IN  struct _SOUND_DEVICE_INSTANCE* SoundDeviceInstance);

MMRESULT
QueueWaveDeviceBuffer(
    IN  PSOUND_DEVICE_INSTANCE Instance,
    IN  PWAVEHDR BufferHeader);


/*
    thread.c
*/

MMRESULT
StartSoundThread(
    IN  PSOUND_DEVICE_INSTANCE Instance,
    IN  SOUND_THREAD_REQUEST_HANDLER RequestHandler);

MMRESULT
StopSoundThread(
    IN  PSOUND_DEVICE_INSTANCE Instance);

MMRESULT
CallSoundThread(
    IN  PSOUND_DEVICE_INSTANCE Instance,
    IN  DWORD RequestId,
    IN  PVOID RequestData);



MMRESULT
StartWaveThread(
    IN  PSOUND_DEVICE_INSTANCE Instance);

MMRESULT
StopWaveThread(
    IN  PSOUND_DEVICE_INSTANCE Instance);


/*
    mme/wodMessage.c
*/

APIENTRY DWORD
wodMessage(
    DWORD device_id,
    DWORD message,
    DWORD private_handle,
    DWORD parameter1,
    DWORD parameter2);


#endif
