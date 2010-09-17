/*
 *  FreeLoader
 *
 *  Copyright (C) 1998-2003  Brian Palmer    <brianp@sginet.com>
 *  Copyright (C) 2006       Aleksey Bragin  <aleksey@reactos.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <freeldr.h>

#include <ndk/ldrtypes.h>
#include <debug.h>

// TODO: Move to .h
void WinLdrSetupForNt(PLOADER_PARAMETER_BLOCK LoaderBlock,
                      PVOID *GdtIdt,
                      ULONG *PcrBasePage,
                      ULONG *TssBasePage);

//FIXME: Do a better way to retrieve Arc disk information
extern ULONG reactos_disk_count;
extern ARC_DISK_SIGNATURE reactos_arc_disk_info[];
extern char reactos_arc_strings[32][256];

extern BOOLEAN UseRealHeap;
extern ULONG LoaderPagesSpanned;
extern BOOLEAN AcpiPresent;

BOOLEAN
WinLdrCheckForLoadedDll(IN OUT PLOADER_PARAMETER_BLOCK WinLdrBlock,
                        IN PCH DllName,
                        OUT PLDR_DATA_TABLE_ENTRY *LoadedEntry);

// debug stuff
VOID DumpMemoryAllocMap(VOID);
VOID WinLdrpDumpMemoryDescriptors(PLOADER_PARAMETER_BLOCK LoaderBlock);
VOID WinLdrpDumpBootDriver(PLOADER_PARAMETER_BLOCK LoaderBlock);
VOID WinLdrpDumpArcDisks(PLOADER_PARAMETER_BLOCK LoaderBlock);


// Init "phase 0"
VOID
AllocateAndInitLPB(PLOADER_PARAMETER_BLOCK *OutLoaderBlock)
{
	PLOADER_PARAMETER_BLOCK LoaderBlock;

	/* Allocate and zero-init the LPB */
	LoaderBlock = MmHeapAlloc(sizeof(LOADER_PARAMETER_BLOCK));
	RtlZeroMemory(LoaderBlock, sizeof(LOADER_PARAMETER_BLOCK));

	/* Init three critical lists, used right away */
	InitializeListHead(&LoaderBlock->LoadOrderListHead);
	InitializeListHead(&LoaderBlock->MemoryDescriptorListHead);
	InitializeListHead(&LoaderBlock->BootDriverListHead);

	/* Alloc space for NLS (it will be converted to VA in WinLdrLoadNLS) */
	LoaderBlock->NlsData = MmHeapAlloc(sizeof(NLS_DATA_BLOCK));
	if (LoaderBlock->NlsData == NULL)
	{
		UiMessageBox("Failed to allocate memory for NLS table data!");
		return;
	}
	RtlZeroMemory(LoaderBlock->NlsData, sizeof(NLS_DATA_BLOCK));

	*OutLoaderBlock = LoaderBlock;
}

// Init "phase 1"
VOID
WinLdrInitializePhase1(PLOADER_PARAMETER_BLOCK LoaderBlock,
                       PCHAR Options,
                       PCHAR SystemRoot,
                       PCHAR BootPath,
                       USHORT VersionToBoot)
{
	/* Examples of correct options and paths */
	//CHAR	Options[] = "/DEBUGPORT=COM1 /BAUDRATE=115200";
	//CHAR	Options[] = "/NODEBUG";
	//CHAR	SystemRoot[] = "\\WINNT\\";
	//CHAR	ArcBoot[] = "multi(0)disk(0)rdisk(0)partition(1)";

	CHAR	HalPath[] = "\\";
	CHAR	ArcBoot[256];
	CHAR	MiscFiles[256];
	ULONG i, PathSeparator;
	PLOADER_PARAMETER_EXTENSION Extension;

	/* Construct SystemRoot and ArcBoot from SystemPath */
	PathSeparator = strstr(BootPath, "\\") - BootPath;
	strncpy(ArcBoot, BootPath, PathSeparator);
	ArcBoot[PathSeparator] = 0;

	DPRINTM(DPRINT_WINDOWS, "ArcBoot: %s\n", ArcBoot);
	DPRINTM(DPRINT_WINDOWS, "SystemRoot: %s\n", SystemRoot);
	DPRINTM(DPRINT_WINDOWS, "Options: %s\n", Options);

	/* Fill Arc BootDevice */
	LoaderBlock->ArcBootDeviceName = MmHeapAlloc(strlen(ArcBoot)+1);
	strcpy(LoaderBlock->ArcBootDeviceName, ArcBoot);
	LoaderBlock->ArcBootDeviceName = PaToVa(LoaderBlock->ArcBootDeviceName);

	/* Fill Arc HalDevice, it matches ArcBoot path */
	LoaderBlock->ArcHalDeviceName = MmHeapAlloc(strlen(ArcBoot)+1);
	strcpy(LoaderBlock->ArcHalDeviceName, ArcBoot);
	LoaderBlock->ArcHalDeviceName = PaToVa(LoaderBlock->ArcHalDeviceName);

	/* Fill SystemRoot */
	LoaderBlock->NtBootPathName = MmHeapAlloc(strlen(SystemRoot)+1);
	strcpy(LoaderBlock->NtBootPathName, SystemRoot);
	LoaderBlock->NtBootPathName = PaToVa(LoaderBlock->NtBootPathName);

	/* Fill NtHalPathName */
	LoaderBlock->NtHalPathName = MmHeapAlloc(strlen(HalPath)+1);
	strcpy(LoaderBlock->NtHalPathName, HalPath);
	LoaderBlock->NtHalPathName = PaToVa(LoaderBlock->NtHalPathName);

	/* Fill load options */
	LoaderBlock->LoadOptions = MmHeapAlloc(strlen(Options)+1);
	strcpy(LoaderBlock->LoadOptions, Options);
	LoaderBlock->LoadOptions = PaToVa(LoaderBlock->LoadOptions);

	/* Arc devices */
	LoaderBlock->ArcDiskInformation = (PARC_DISK_INFORMATION)MmHeapAlloc(sizeof(ARC_DISK_INFORMATION));
	InitializeListHead(&LoaderBlock->ArcDiskInformation->DiskSignatureListHead);

	/* Convert ARC disk information from freeldr to a correct format */
	for (i = 0; i < reactos_disk_count; i++)
	{
		PARC_DISK_SIGNATURE ArcDiskInfo;

		/* Get the ARC structure */
		ArcDiskInfo = (PARC_DISK_SIGNATURE)MmHeapAlloc(sizeof(ARC_DISK_SIGNATURE));
		RtlZeroMemory(ArcDiskInfo, sizeof(ARC_DISK_SIGNATURE));

		/* Copy the data over */
		ArcDiskInfo->Signature = reactos_arc_disk_info[i].Signature;
		ArcDiskInfo->CheckSum = reactos_arc_disk_info[i].CheckSum;

		/* Copy the ARC Name */
		ArcDiskInfo->ArcName = (PCHAR)MmHeapAlloc(sizeof(CHAR)*256);
		strcpy(ArcDiskInfo->ArcName, reactos_arc_disk_info[i].ArcName);
		ArcDiskInfo->ArcName = (PCHAR)PaToVa(ArcDiskInfo->ArcName);

		/* Mark partition table as valid */
		ArcDiskInfo->ValidPartitionTable = TRUE; 

		/* Insert into the list */
		InsertTailList(&LoaderBlock->ArcDiskInformation->DiskSignatureListHead,
			&ArcDiskInfo->ListEntry);
	}

	/* Convert all list's to Virtual address */

	/* Convert the ArcDisks list to virtual address */
	List_PaToVa(&LoaderBlock->ArcDiskInformation->DiskSignatureListHead);
	LoaderBlock->ArcDiskInformation = PaToVa(LoaderBlock->ArcDiskInformation);

	/* Convert configuration entries to VA */
	ConvertConfigToVA(LoaderBlock->ConfigurationRoot);
	LoaderBlock->ConfigurationRoot = PaToVa(LoaderBlock->ConfigurationRoot);

	/* Convert all DTE into virtual addresses */
	List_PaToVa(&LoaderBlock->LoadOrderListHead);

	/* this one will be converted right before switching to
	   virtual paging mode */
	//List_PaToVa(&LoaderBlock->MemoryDescriptorListHead);

	/* Convert list of boot drivers */
	List_PaToVa(&LoaderBlock->BootDriverListHead);

	/* Initialize Extension now */
	Extension = MmHeapAlloc(sizeof(LOADER_PARAMETER_EXTENSION));
	if (Extension == NULL)
	{
		UiMessageBox("Failed to allocate LPB Extension!");
		return;
	}
	RtlZeroMemory(Extension, sizeof(LOADER_PARAMETER_EXTENSION));

	/* Fill LPB extension */
	Extension->Size = sizeof(LOADER_PARAMETER_EXTENSION);
	Extension->MajorVersion = (VersionToBoot & 0xFF00) >> 8;
	Extension->MinorVersion = VersionToBoot & 0xFF;
	Extension->Profile.Status = 2;

	/* Check if ACPI is present */
	if (AcpiPresent)
	{
		/* See KiRosFrldrLpbToNtLpb for details */
		Extension->AcpiTable = (PVOID)1;
	}
    
    /* Set headless block pointer */
    extern HEADLESS_LOADER_BLOCK LoaderRedirectionInformation;
    extern BOOLEAN WinLdrTerminalConnected;
    if (WinLdrTerminalConnected)
    {
        Extension->HeadlessLoaderBlock = MmHeapAlloc(sizeof(HEADLESS_LOADER_BLOCK));
        if (Extension->HeadlessLoaderBlock == NULL)
        {
            UiMessageBox("Failed to allocate HLB Extension!");
            while (TRUE);
            return;
        }
        RtlCopyMemory(
            Extension->HeadlessLoaderBlock,
            &LoaderRedirectionInformation,
            sizeof(HEADLESS_LOADER_BLOCK));
        Extension->HeadlessLoaderBlock = PaToVa(Extension->HeadlessLoaderBlock);
    }

	/* Load drivers database */
	strcpy(MiscFiles, BootPath);
	strcat(MiscFiles, "AppPatch\\drvmain.sdb");
	Extension->DrvDBImage = PaToVa(WinLdrLoadModule(MiscFiles,
		&Extension->DrvDBSize, LoaderRegistryData));

	/* Convert extension and setup block pointers */
	LoaderBlock->Extension = PaToVa(Extension);

	if (LoaderBlock->SetupLdrBlock)
		LoaderBlock->SetupLdrBlock = PaToVa(LoaderBlock->SetupLdrBlock);

}

BOOLEAN
WinLdrLoadDeviceDriver(PLOADER_PARAMETER_BLOCK LoaderBlock,
                       LPSTR BootPath,
                       PUNICODE_STRING FilePath,
                       ULONG Flags,
                       PLDR_DATA_TABLE_ENTRY *DriverDTE)
{
	CHAR FullPath[1024];
	CHAR DriverPath[1024];
	CHAR DllName[1024];
	PCHAR DriverNamePos;
	BOOLEAN Status;
	PVOID DriverBase;

	// Separate the path to file name and directory path
	_snprintf(DriverPath, sizeof(DriverPath), "%wZ", FilePath);
	DriverNamePos = strrchr(DriverPath, '\\');
	if (DriverNamePos != NULL)
	{
		// Copy the name
		strcpy(DllName, DriverNamePos+1);

		// Cut out the name from the path
		*(DriverNamePos+1) = 0;
	}
	else
	{
		// There is no directory in the path
		strcpy(DllName, DriverPath);
		DriverPath[0] = 0;
	}

	DPRINTM(DPRINT_WINDOWS, "DriverPath: %s, DllName: %s, LPB %p\n", DriverPath, DllName, LoaderBlock);


	// Check if driver is already loaded
	Status = WinLdrCheckForLoadedDll(LoaderBlock, DllName, DriverDTE);
	if (Status)
	{
		// We've got the pointer to its DTE, just return success
		return TRUE;
	}

	// It's not loaded, we have to load it
	_snprintf(FullPath, sizeof(FullPath), "%s%wZ", BootPath, FilePath);
	Status = WinLdrLoadImage(FullPath, LoaderBootDriver, &DriverBase);
	if (!Status)
		return FALSE;

	// Allocate a DTE for it
	Status = WinLdrAllocateDataTableEntry(LoaderBlock, DllName, DllName, DriverBase, DriverDTE);
	if (!Status)
	{
		DPRINTM(DPRINT_WINDOWS, "WinLdrAllocateDataTableEntry() failed\n");
		return FALSE;
	}

	// Modify any flags, if needed
	(*DriverDTE)->Flags |= Flags;

	// Look for any dependencies it may have, and load them too
	sprintf(FullPath,"%s%s", BootPath, DriverPath);
	Status = WinLdrScanImportDescriptorTable(LoaderBlock, FullPath, *DriverDTE);
	if (!Status)
	{
		DPRINTM(DPRINT_WINDOWS, "WinLdrScanImportDescriptorTable() failed for %s\n",
			FullPath);
		return FALSE;
	}

	return TRUE;
}

BOOLEAN
WinLdrLoadBootDrivers(PLOADER_PARAMETER_BLOCK LoaderBlock,
                      LPSTR BootPath)
{
	PLIST_ENTRY NextBd;
	PBOOT_DRIVER_LIST_ENTRY BootDriver;
	BOOLEAN Status;

	// Walk through the boot drivers list
	NextBd = LoaderBlock->BootDriverListHead.Flink;

	while (NextBd != &LoaderBlock->BootDriverListHead)
	{
		BootDriver = CONTAINING_RECORD(NextBd, BOOT_DRIVER_LIST_ENTRY, Link);

		DPRINTM(DPRINT_WINDOWS, "BootDriver %wZ DTE %08X RegPath: %wZ\n", &BootDriver->FilePath,
			BootDriver->LdrEntry, &BootDriver->RegistryPath);

		// Paths are relative (FIXME: Are they always relative?)

		// Load it
		Status = WinLdrLoadDeviceDriver(LoaderBlock, BootPath, &BootDriver->FilePath,
			0, &BootDriver->LdrEntry);

		// If loading failed - cry loudly
		//FIXME: Maybe remove it from the list and try to continue?
		if (!Status)
		{
			UiMessageBox("Can't load boot driver!");
			return FALSE;
		}

		// Convert the RegistryPath and DTE addresses to VA since we are not going to use it anymore
		BootDriver->RegistryPath.Buffer = PaToVa(BootDriver->RegistryPath.Buffer);
		BootDriver->FilePath.Buffer = PaToVa(BootDriver->FilePath.Buffer);
		BootDriver->LdrEntry = PaToVa(BootDriver->LdrEntry);

		NextBd = BootDriver->Link.Flink;
	}

	return TRUE;
}

PVOID WinLdrLoadModule(PCSTR ModuleName, ULONG *Size,
					   TYPE_OF_MEMORY MemoryType)
{
	ULONG FileId;
	PVOID PhysicalBase;
	FILEINFORMATION FileInfo;
	ULONG FileSize;
	ULONG Status;
	ULONG BytesRead;

	//CHAR ProgressString[256];

	/* Inform user we are loading files */
	//sprintf(ProgressString, "Loading %s...", FileName);
	//UiDrawProgressBarCenter(1, 100, ProgressString);

	DPRINTM(DPRINT_WINDOWS, "Loading module %s\n", ModuleName);
	*Size = 0;

	/* Open the image file */
	Status = ArcOpen((PCHAR)ModuleName, OpenReadOnly, &FileId);
	if (Status != ESUCCESS)
	{
		/* In case of errors, we just return, without complaining to the user */
		return NULL;
	}

	/* Get this file's size */
	Status = ArcGetFileInformation(FileId, &FileInfo);
	if (Status != ESUCCESS)
	{
		ArcClose(FileId);
		return NULL;
	}
	FileSize = FileInfo.EndingAddress.LowPart;
	*Size = FileSize;

	/* Allocate memory */
	PhysicalBase = MmAllocateMemoryWithType(FileSize, MemoryType);
	if (PhysicalBase == NULL)
	{
		ArcClose(FileId);
		return NULL;
	}

	/* Load whole file */
	Status = ArcRead(FileId, PhysicalBase, FileSize, &BytesRead);
	ArcClose(FileId);
	if (Status != ESUCCESS)
	{
		return NULL;
	}

	DPRINTM(DPRINT_WINDOWS, "Loaded %s at 0x%x with size 0x%x\n", ModuleName, PhysicalBase, FileSize);

	return PhysicalBase;
}


USHORT
WinLdrDetectVersion()
{
	LONG rc;
	FRLDRHKEY hKey;

	rc = RegOpenKey(
		NULL,
		L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Control\\Terminal Server",
		&hKey);
	if (rc != ERROR_SUCCESS)
	{
		// Key doesn't exist; assume NT 4.0
		return _WIN32_WINNT_NT4;
	}

	// We may here want to read the value of ProductVersion
	return _WIN32_WINNT_WS03;
}


VOID
LoadAndBootWindows(PCSTR OperatingSystemName,
                   PSTR SettingsValue,
                   USHORT OperatingSystemVersion)
{
	BOOLEAN HasSection;
	char  FullPath[MAX_PATH], SystemRoot[MAX_PATH], BootPath[MAX_PATH];
	CHAR  FileName[MAX_PATH];
	CHAR  BootOptions[256];
	PCHAR File;
	PCHAR PathSeparator;
	PVOID NtosBase = NULL, HalBase = NULL, KdComBase = NULL;
	BOOLEAN Status;
	ULONG_PTR SectionId;
	PLOADER_PARAMETER_BLOCK LoaderBlock, LoaderBlockVA;
	KERNEL_ENTRY_POINT KiSystemStartup;
	PLDR_DATA_TABLE_ENTRY KernelDTE, HalDTE, KdComDTE = NULL;
	// Mm-related things
	PVOID GdtIdt;
	ULONG PcrBasePage=0;
	ULONG TssBasePage=0;

	// Open the operating system section
	// specified in the .ini file
	HasSection = IniOpenSection(OperatingSystemName, &SectionId);

	UiDrawBackdrop();
	UiDrawStatusText("Detecting Hardware...");
	UiDrawProgressBarCenter(1, 100, "Loading NT...");

	/* Read the system path is set in the .ini file */
	if (!HasSection || !IniReadSettingByName(SectionId, "SystemPath", FullPath, sizeof(FullPath)))
	{
		strcpy(FullPath, OperatingSystemName);
	}

	/* Special case for LiveCD */
	if (!_strnicmp(FullPath, "LiveCD", strlen("LiveCD")))
	{
		strcpy(BootPath, FullPath + strlen("LiveCD"));
		MachDiskGetBootPath(FullPath, sizeof(FullPath));
		strcat(FullPath, BootPath);
	}

	/* Convert FullPath to SystemRoot */
	PathSeparator = strstr(FullPath, "\\");
	strcpy(SystemRoot, PathSeparator);
	strcat(SystemRoot, "\\");

	/* Read booting options */
	if (!HasSection || !IniReadSettingByName(SectionId, "Options", BootOptions, sizeof(BootOptions)))
	{
		/* Get options after the title */
		const CHAR*p = SettingsValue;
		while (*p == ' ' || *p == '"')
			p++;
		while (*p != '\0' && *p != '"')
			p++;
		strcpy(BootOptions, p);
		DPRINTM(DPRINT_WINDOWS,"BootOptions: '%s'\n", BootOptions);
	}

	/* Append boot-time options */
	AppendBootTimeOptions(BootOptions);

	//
	// Check if a ramdisk file was given
	//
	File = strstr(BootOptions, "/RDPATH=");
	if (File)
	{
		//
		// Copy the file name and everything else after it
		//
		strcpy(FileName, File + 8);

		//
		// Null-terminate
		//
		*strstr(FileName, " ") = ANSI_NULL;

		//
		// Load the ramdisk
		//
		RamDiskLoadVirtualFile(FileName);
	}

	/* Let user know we started loading */
	UiDrawStatusText("Loading...");

	/* append a backslash */
	strcpy(BootPath, FullPath);
	if ((strlen(BootPath)==0) ||
	    BootPath[strlen(BootPath)] != '\\')
		strcat(BootPath, "\\");

	DPRINTM(DPRINT_WINDOWS,"BootPath: '%s'\n", BootPath);

	/* Allocate and minimalistic-initialize LPB */
	AllocateAndInitLPB(&LoaderBlock);
    
   	/* Setup redirection support */
	extern void WinLdrSetupEms(IN PCHAR BootOptions);
	WinLdrSetupEms(BootOptions);

	/* Detect hardware */
	UseRealHeap = TRUE;
	LoaderBlock->ConfigurationRoot = MachHwDetect();

	/* Load Hive */
	Status = WinLdrInitSystemHive(LoaderBlock, BootPath);
	DPRINTM(DPRINT_WINDOWS, "SYSTEM hive loaded with status %d\n", Status);

	if (OperatingSystemVersion == 0)
		OperatingSystemVersion = WinLdrDetectVersion();

	/* Load kernel */
	strcpy(FileName, BootPath);
	strcat(FileName, "SYSTEM32\\NTOSKRNL.EXE");
	Status = WinLdrLoadImage(FileName, LoaderSystemCode, &NtosBase);
	DPRINTM(DPRINT_WINDOWS, "Ntos loaded with status %d at %p\n", Status, NtosBase);

	/* Load HAL */
	strcpy(FileName, BootPath);
	strcat(FileName, "SYSTEM32\\HAL.DLL");
	Status = WinLdrLoadImage(FileName, LoaderHalCode, &HalBase);
	DPRINTM(DPRINT_WINDOWS, "HAL loaded with status %d at %p\n", Status, HalBase);

	/* Load kernel-debugger support dll */
	if (OperatingSystemVersion > _WIN32_WINNT_WIN2K)
	{
		strcpy(FileName, BootPath);
		strcat(FileName, "SYSTEM32\\KDCOM.DLL");
		Status = WinLdrLoadImage(FileName, LoaderBootDriver, &KdComBase);
		DPRINTM(DPRINT_WINDOWS, "KdCom loaded with status %d at %p\n", Status, KdComBase);
	}

	/* Allocate data table entries for above-loaded modules */
	WinLdrAllocateDataTableEntry(LoaderBlock, "ntoskrnl.exe",
		"WINDOWS\\SYSTEM32\\NTOSKRNL.EXE", NtosBase, &KernelDTE);
	WinLdrAllocateDataTableEntry(LoaderBlock, "hal.dll",
		"WINDOWS\\SYSTEM32\\HAL.DLL", HalBase, &HalDTE);
	if (OperatingSystemVersion > _WIN32_WINNT_WIN2K)
	{
		WinLdrAllocateDataTableEntry(LoaderBlock, "kdcom.dll",
			"WINDOWS\\SYSTEM32\\KDCOM.DLL", KdComBase, &KdComDTE);
	}

	/* Load all referenced DLLs for kernel, HAL and kdcom.dll */
	strcpy(FileName, BootPath);
	strcat(FileName, "SYSTEM32\\");
	WinLdrScanImportDescriptorTable(LoaderBlock, FileName, KernelDTE);
	WinLdrScanImportDescriptorTable(LoaderBlock, FileName, HalDTE);
	if (KdComDTE)
		WinLdrScanImportDescriptorTable(LoaderBlock, FileName, KdComDTE);

	/* Load NLS data, OEM font, and prepare boot drivers list */
	Status = WinLdrScanSystemHive(LoaderBlock, BootPath);
	DPRINTM(DPRINT_WINDOWS, "SYSTEM hive scanned with status %d\n", Status);

	/* Load boot drivers */
	Status = WinLdrLoadBootDrivers(LoaderBlock, BootPath);
	DPRINTM(DPRINT_WINDOWS, "Boot drivers loaded with status %d\n", Status);

	/* Alloc PCR, TSS, do magic things with the GDT/IDT */
	WinLdrSetupForNt(LoaderBlock, &GdtIdt, &PcrBasePage, &TssBasePage);

	/* Initialize Phase 1 - no drivers loading anymore */
	WinLdrInitializePhase1(LoaderBlock, BootOptions, SystemRoot, BootPath, OperatingSystemVersion);

	/* Save entry-point pointer and Loader block VAs */
	KiSystemStartup = (KERNEL_ENTRY_POINT)KernelDTE->EntryPoint;
	LoaderBlockVA = PaToVa(LoaderBlock);

	/* "Stop all motors", change videomode */
	if (OperatingSystemVersion < _WIN32_WINNT_WIN2K)
		MachPrepareForReactOS(TRUE);
	else
		MachPrepareForReactOS(FALSE);

	/* Debugging... */
	//DumpMemoryAllocMap();

	/* Turn on paging mode of CPU*/
	WinLdrTurnOnPaging(LoaderBlock, PcrBasePage, TssBasePage, GdtIdt);

	/* Save final value of LoaderPagesSpanned */
	LoaderBlockVA->Extension->LoaderPagesSpanned = LoaderPagesSpanned;

	DPRINTM(DPRINT_WINDOWS, "Hello from paged mode, KiSystemStartup %p, LoaderBlockVA %p!\n",
		KiSystemStartup, LoaderBlockVA);

	WinLdrpDumpMemoryDescriptors(LoaderBlockVA);
	WinLdrpDumpBootDriver(LoaderBlockVA);
	WinLdrpDumpArcDisks(LoaderBlockVA);

	//FIXME: If I substitute this debugging checkpoint, GCC will "optimize away" the code below
	//while (1) {};
	/*asm(".intel_syntax noprefix\n");
		asm("test1:\n");
		asm("jmp test1\n");
	asm(".att_syntax\n");*/

	/* Pass control */
	(*KiSystemStartup)(LoaderBlockVA);

	return;
}

VOID
WinLdrpDumpMemoryDescriptors(PLOADER_PARAMETER_BLOCK LoaderBlock)
{
	PLIST_ENTRY NextMd;
	PMEMORY_ALLOCATION_DESCRIPTOR MemoryDescriptor;

	NextMd = LoaderBlock->MemoryDescriptorListHead.Flink;

	while (NextMd != &LoaderBlock->MemoryDescriptorListHead)
	{
		MemoryDescriptor = CONTAINING_RECORD(NextMd, MEMORY_ALLOCATION_DESCRIPTOR, ListEntry);

		DPRINTM(DPRINT_WINDOWS, "BP %08X PC %04X MT %d\n", MemoryDescriptor->BasePage,
			MemoryDescriptor->PageCount, MemoryDescriptor->MemoryType);

		NextMd = MemoryDescriptor->ListEntry.Flink;
	}
}

VOID
WinLdrpDumpBootDriver(PLOADER_PARAMETER_BLOCK LoaderBlock)
{
	PLIST_ENTRY NextBd;
	PBOOT_DRIVER_LIST_ENTRY BootDriver;

	NextBd = LoaderBlock->BootDriverListHead.Flink;

	while (NextBd != &LoaderBlock->BootDriverListHead)
	{
		BootDriver = CONTAINING_RECORD(NextBd, BOOT_DRIVER_LIST_ENTRY, Link);

		DPRINTM(DPRINT_WINDOWS, "BootDriver %wZ DTE %08X RegPath: %wZ\n", &BootDriver->FilePath,
			BootDriver->LdrEntry, &BootDriver->RegistryPath);

		NextBd = BootDriver->Link.Flink;
	}
}

VOID
WinLdrpDumpArcDisks(PLOADER_PARAMETER_BLOCK LoaderBlock)
{
	PLIST_ENTRY NextBd;
	PARC_DISK_SIGNATURE ArcDisk;

	NextBd = LoaderBlock->ArcDiskInformation->DiskSignatureListHead.Flink;

	while (NextBd != &LoaderBlock->ArcDiskInformation->DiskSignatureListHead)
	{
		ArcDisk = CONTAINING_RECORD(NextBd, ARC_DISK_SIGNATURE, ListEntry);

		DPRINTM(DPRINT_WINDOWS, "ArcDisk %s checksum: 0x%X, signature: 0x%X\n",
			ArcDisk->ArcName, ArcDisk->CheckSum, ArcDisk->Signature);

		NextBd = ArcDisk->ListEntry.Flink;
	}
}


