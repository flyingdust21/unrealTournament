// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.
#include "UnrealTournament.h"
#include "UTHUDWidget_SCTFStatus.h"
#include "UTSCTFGameState.h"
#include "UTSCTFFlag.h"

UUTHUDWidget_SCTFStatus::UUTHUDWidget_SCTFStatus(const FObjectInitializer& ObjectInitializer)
: Super(ObjectInitializer)
{
	OffensePrimaryMessage	= NSLOCTEXT("SCTFGame","YouAreOnOffenseA","!! YOU ARE ON OFFENSE !!");
	OffenseSecondaryMessage	= NSLOCTEXT("SCTFGame","YouAreOnOffenseB","Assault the enemy base!");
	DefensePrimaryMessage	= NSLOCTEXT("SCTFGame","YouAreOnDefenseA","!! YOU ARE ON DEFENSE !!");
	DefenseSecondaryMessage	= NSLOCTEXT("SCTFGame","YouAreOnDefenseB","Protect your base from the enemy!");

	FlagDownPrimary						= NSLOCTEXT("SCTFGame","FlagDownA","!! THE FLAG IS DOWN !!");
	FlagDownOffenseSecondaryMessage		= NSLOCTEXT("SCTFGame","FlagDownOB","You have {0} seconds to get the flag!  Hurry!");
	FlagDownDefenseSecondaryMessage		= NSLOCTEXT("SCTFGame","FlagDownDB","Defend the flag for {0} seconds to gain control!");	

	FlagHomePrimary						= NSLOCTEXT("SCTFGame","FlagHomeA","!! THE FLAG HAS BEEN RESET !!");
	FlagHomeOffenseSecondaryMessage		= NSLOCTEXT("SCTFGame","FlagHomeOB","Get the flag and assault the base!");
	FlagHomeDefenseSecondaryMessage		= NSLOCTEXT("SCTFGame","FlagHomeDB","Get ready to defend your base!");	
}

void UUTHUDWidget_SCTFStatus::DrawStatusMessage(float DeltaTime)
{
	AUTSCTFGameState* GameState = Cast<AUTSCTFGameState>(UTGameState);
	if (GameState && GameState->Flag && GameState->AttackingTeam != 255)
	{
		PrimaryMessage.bHidden = false;
		SecondaryMessage.bHidden = false;
		bool bOnOffense = GameState->AttackingTeam == UTPlayerOwner->GetTeamNum();

		if ( GameState->Flag->ObjectState == CarriedObjectState::Held )
		{
			PrimaryMessage.Text = bOnOffense ? OffensePrimaryMessage : DefensePrimaryMessage;
			SecondaryMessage.Text = bOnOffense ? OffenseSecondaryMessage : DefenseSecondaryMessage;
		}
		else if ( GameState->Flag->ObjectState == CarriedObjectState::Dropped )
		{
			PrimaryMessage.Text = FlagDownPrimary;
			SecondaryMessage.Text = FText::Format((bOnOffense ? FlagDownOffenseSecondaryMessage: FlagDownDefenseSecondaryMessage), FText::AsNumber(int32(GameState->Flag->SwapTimer))); 
		}
		else
		{
			PrimaryMessage.Text = FlagHomePrimary;
			SecondaryMessage.Text = bOnOffense ? FlagHomeOffenseSecondaryMessage: FlagHomeDefenseSecondaryMessage; 
		}
	}
	else
	{
		PrimaryMessage.bHidden = true;
		SecondaryMessage.bHidden = true;
	}

	RenderObj_Text(PrimaryMessage);
	RenderObj_Text(SecondaryMessage);
}

void UUTHUDWidget_SCTFStatus::DrawIndicators(AUTCTFGameState* GameState, FVector PlayerViewPoint, FRotator PlayerViewRotation)
{
	if (GameState)
	{
		uint8 OwnerTeam = UTHUDOwner->UTPlayerOwner->GetTeamNum();

		TArray<AUTCTFFlag*> Flags;
		GameState->GetImportantFlag(OwnerTeam, Flags);

		TArray<AUTCTFFlagBase*> FlagBases;
		GameState->GetImportantFlagBase(OwnerTeam, FlagBases); 

		bool bDrawBasesInWorld = true;
		// Flag is there and in the world.
		if (Flags.Num() > 0)
		{
			AUTCTFFlag* Flag = Flags[0];
			AUTCTFFlagBase* FlagBase = FlagBases.Num() > 0 ? FlagBases[0] : nullptr;

			if (Flag->ObjectState != CarriedObjectState::Home)
			{
				uint8 TeamNum = Flag->GetTeamNum();
				DrawFlagStatus(GameState, PlayerViewPoint, PlayerViewRotation, TeamNum, FVector2D(0.0f,0.0f), FlagBase, Flag, Flag->Holder);
				DrawFlagWorld(GameState, PlayerViewPoint, PlayerViewRotation, TeamNum, FlagBase, Flag, Flag->Holder);
				if (FlagBase)
				{
					DrawFlagBaseWorld(GameState, PlayerViewPoint, PlayerViewRotation, FlagBase->GetTeamNum(), FlagBase, Flag, Flag->Holder);
					bDrawBasesInWorld = false;
				}
			}
		}
		
		if (FlagBases.Num() > 0 && bDrawBasesInWorld)
		{
			for (int32 i = 0; i < FlagBases.Num(); i++)
			{
				DrawFlagBaseWorld(GameState, PlayerViewPoint, PlayerViewRotation, FlagBases[i]->GetTeamNum(), FlagBases[i], nullptr, nullptr);
			}
		}
	}
}

FText UUTHUDWidget_SCTFStatus::GetFlagReturnTime(AUTCTFFlag* Flag)
{	
	AUTSCTFGameState* GameState = Cast<AUTSCTFGameState>(UTGameState);
	AUTSCTFFlag* SFlag = Cast<AUTSCTFFlag>(Flag);
	if (GameState && SFlag)
	{
		if (SFlag->ObjectState == CarriedObjectState::Dropped && SFlag->bPendingTeamSwitch)
		{
			return FText::AsNumber(int32(GameState->Flag->SwapTimer));
		}
	}

	return Super::GetFlagReturnTime(Flag);
}


bool UUTHUDWidget_SCTFStatus::ShouldDrawFlag(AUTCTFFlag* Flag, bool bIsEnemyFlag)
{
	return (Flag->ObjectState == CarriedObjectState::Dropped) || Flag->bCurrentlyPinged || !bIsEnemyFlag;
}
