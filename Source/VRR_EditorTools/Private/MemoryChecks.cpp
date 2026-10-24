// Fill out your copyright notice in the Description page of Project Settings.

/*
 * Windows-only hardware checks, not used by the Toolbox pages yet.
 *
 *  - GetDiskSpeed: times reading 1 MB straight from a drive (QueryPerformanceCounter) and prints MB/s to stdout
 *    (not the Output Log). Note: it opens drive C: whatever DriveLetter is passed.
 *  - GetAvailableVRAMAmount: asks DXGI (IDXGIAdapter3::QueryVideoMemoryInfo) about the first adapter's local
 *    video memory. Despite the name it returns the memory in use (CurrentUsage), in MB.
 */
#include "MemoryChecks.h"

MemoryChecks::MemoryChecks()
{
}

MemoryChecks::~MemoryChecks()
{
}

void MemoryChecks::GetDiskSpeed(FString DriveLetter)
{
	DWORD bytesRead;
	LARGE_INTEGER startTime, endTime;
	char buffer[1024 * 1024]; // 1 MB buffer
	DriveLetter = "\\\\.\\" + DriveLetter + ":";
	HANDLE hDrive = CreateFile(TEXT("\\\\.\\C:"), 
								GENERIC_READ, 
								FILE_SHARE_READ | FILE_SHARE_WRITE, 
								NULL, 
								OPEN_EXISTING, 
								0, 
								NULL);

	if (hDrive == INVALID_HANDLE_VALUE)
	{
		std::cerr << "Error opening drive." << std::endl;
		return;
	}

	// Start timing
	QueryPerformanceCounter(&startTime);
	ReadFile(hDrive, buffer, sizeof(buffer), &bytesRead, NULL);
	QueryPerformanceCounter(&endTime);

	CloseHandle(hDrive);

	// Calculate time taken
	double elapsedTime = static_cast<double>(endTime.QuadPart - startTime.QuadPart) / 
						 static_cast<double>(10000000); // Convert to seconds

	// Calculate speed in MB/s
	double speed = (bytesRead / (1024.0 * 1024.0)) / elapsedTime;
	std::cout << "Disk Speed: " << speed << " MB/s" << std::endl;
}

size_t MemoryChecks::GetAvailableVRAMAmount()
{
	IDXGIFactory4* pFactory;
	CreateDXGIFactory1(__uuidof(IDXGIFactory4), (void**)&pFactory);

	IDXGIAdapter3* adapter;
	pFactory->EnumAdapters(0, reinterpret_cast<IDXGIAdapter**>(&adapter));

	DXGI_QUERY_VIDEO_MEMORY_INFO videoMemoryInfo;
	adapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &videoMemoryInfo);

	size_t usedVRAM = videoMemoryInfo.CurrentUsage / 1024 / 1024; // Convert to MB
	
	return usedVRAM;
	
	// You can also get:
	// videoMemoryInfo.AvailableForReservation
	// videoMemoryInfo.Budget
}


