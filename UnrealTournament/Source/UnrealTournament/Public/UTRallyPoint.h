// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UTRallyPoint.generated.h"

UCLASS(Blueprintable)
class UNREALTOURNAMENT_API AUTRallyPoint : public AUTGameObjective
{
	GENERATED_UCLASS_BODY()

	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Category = Objective)
		UCapsuleComponent* Capsule;

	UPROPERTY(BlueprintReadOnly)
		class AUTGameVolume* MyGameVolume;

	UPROPERTY(BlueprintReadOnly)
		class AUTCharacter* NearbyFC;

	/** how long FC has to be here for rally to start */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Objective)
		float RallyReadyDelay;

	UPROPERTY(ReplicatedUsing = OnAvailableEffectChanged, BlueprintReadOnly)
		bool bShowAvailableEffect;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Objective)
		UParticleSystem* AvailableEffect;

	UPROPERTY()
		UParticleSystemComponent* AvailableEffectPSC;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Objective)
		UParticleSystem* RallyEffect;

	UPROPERTY()
		UParticleSystemComponent* RallyEffectPSC;

	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = AmbientSoundUpdated, Category = "Audio")
		USoundBase* AmbientSound;

	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = AmbientSoundPitchUpdated, Category = "Audio")
		float AmbientSoundPitch;

	UPROPERTY(BlueprintReadOnly, Category = "Audio")
		UAudioComponent* AmbientSoundComp;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Objective)
		USoundBase* PoweringUpSound;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Objective)
		USoundBase* ReadyToRallySound;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Objective)
		USoundBase* FCTouchedSound;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Objective)
		USoundBase* EnabledSound;

	UFUNCTION()
		void OnOverlapBegin(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

	virtual void FlagCarrierInVolume(class AUTCharacter* NewFC);

	UFUNCTION()
	void OnAvailableEffectChanged();


	virtual void ChangeAmbientSoundPitch(USoundBase* InAmbientSound, float NewPitch);

	UFUNCTION()
		virtual void AmbientSoundPitchUpdated();

	virtual void SetAmbientSound(USoundBase* NewAmbientSound, bool bClear);

	UFUNCTION()
		void AmbientSoundUpdated();

};
