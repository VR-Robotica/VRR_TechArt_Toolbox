// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include <iostream>
#include <windows.h>
#include "Windows/WindowsHWrapper.h"
#include "dxgi1_4.h"

/**
 * 
 */
class VRR_EDITORTOOLS_API MemoryChecks
{
public:
	MemoryChecks();
	~MemoryChecks();
	
	void GetDiskSpeed(FString DriveLetter);
	size_t GetAvailableVRAMAmount();
	
};
