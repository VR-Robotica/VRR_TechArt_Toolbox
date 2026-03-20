// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"


class FTAT_MenuBehaviors
{
public:	
	void MapCommandListActions();
	
	void onClicked_button01();
	void onClicked_button02();
	void onClicked_button03();
	void onClicked_button04();
	
private:
	TSharedPtr<class FUICommandList> MenuCommandList;
};
