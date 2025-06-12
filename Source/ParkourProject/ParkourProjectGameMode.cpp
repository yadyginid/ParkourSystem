// Copyright Epic Games, Inc. All Rights Reserved.

#include "ParkourProjectGameMode.h"
#include "ParkourProjectCharacter.h"
#include "UObject/ConstructorHelpers.h"

AParkourProjectGameMode::AParkourProjectGameMode()
{
	// set default pawn class to our Blueprinted character
	static ConstructorHelpers::FClassFinder<APawn> PlayerPawnBPClass(TEXT("/Game/ThirdPerson/Blueprints/BP_ThirdPersonCharacter"));
	if (PlayerPawnBPClass.Class != NULL)
	{
		DefaultPawnClass = PlayerPawnBPClass.Class;
	}
}
