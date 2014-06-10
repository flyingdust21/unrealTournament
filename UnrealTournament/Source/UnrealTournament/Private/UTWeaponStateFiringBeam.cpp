// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.

#include "UnrealTournament.h"
#include "UTWeaponState.h"
#include "UTWeaponStateFiring.h"
#include "UTWeaponStateFiringBeam.h"

UUTWeaponStateFiringBeam::UUTWeaponStateFiringBeam(const FPostConstructInitializeProperties& PCIP)
: Super(PCIP)
{
	MinDamage = 1;
}

void UUTWeaponStateFiringBeam::FireShot()
{
	// consume ammo but don't fire from here
	GetOuterAUTWeapon()->PlayFiringEffects();
	GetOuterAUTWeapon()->ConsumeAmmo(GetOuterAUTWeapon()->GetCurrentFireMode());
}

void UUTWeaponStateFiringBeam::Tick(float DeltaTime)
{
	if (!GetOuterAUTWeapon()->FireShotOverride() && GetOuterAUTWeapon()->InstantHitInfo.IsValidIndex(GetOuterAUTWeapon()->GetCurrentFireMode()))
	{
		const FInstantHitDamageInfo& DamageInfo = GetOuterAUTWeapon()->InstantHitInfo[GetOuterAUTWeapon()->GetCurrentFireMode()];
		FHitResult Hit;
		GetOuterAUTWeapon()->FireInstantHit(false, &Hit);
		if (Hit.Actor != NULL && Hit.Actor->bCanBeDamaged)
		{
			Accumulator += float(DamageInfo.Damage) / GetOuterAUTWeapon()->GetRefireTime(GetOuterAUTWeapon()->GetCurrentFireMode()) * DeltaTime;
			if (Accumulator >= MinDamage)
			{
				int32 AppliedDamage = FMath::TruncToInt(Accumulator);
				FVector FireDir = (Hit.Location - Hit.TraceStart).SafeNormal();
				Hit.Actor->TakeDamage(AppliedDamage, FUTPointDamageEvent(AppliedDamage, Hit, FireDir, DamageInfo.DamageType, FireDir * (DamageInfo.Momentum * float(AppliedDamage) / float(DamageInfo.Damage))), GetOuterAUTWeapon()->GetUTOwner()->Controller, GetOuterAUTWeapon());
			}
		}
		// beams show a clientside beam target
		if (GetOuterAUTWeapon()->Role < ROLE_Authority)
		{
			GetOuterAUTWeapon()->GetUTOwner()->SetFlashLocation(Hit.Location, GetOuterAUTWeapon()->GetCurrentFireMode());
		}
	}
}