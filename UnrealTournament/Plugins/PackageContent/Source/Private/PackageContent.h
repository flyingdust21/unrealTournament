// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "LevelEditor.h"
#include "Editor/ClassViewer/Public/ClassViewerModule.h"
#include "Editor/ClassViewer/Public/ClassViewerFilter.h"
#include "UTWeapon.h"
#include "UTHat.h"

class FPackageContent : public TSharedFromThis< FPackageContent >
{
public:
	~FPackageContent();

	static TSharedRef< FPackageContent > Create();

	void PackageWeapon(UClass* WeaponClass);
	void PackageHat(UClass* HatClass);

private:
	FPackageContent(const TSharedRef< FUICommandList >& InActionList);
	void Initialize();


	void OpenPackageLevelWindow();
	void OpenPackageWeaponWindow();
	void OpenPackageHatWindow();

	void CreatePackageContentMenu(FToolBarBuilder& Builder);
	TSharedRef<FExtender> OnExtendLevelEditorViewMenu(const TSharedRef<FUICommandList> CommandList);
	TSharedRef< SWidget > GenerateOpenPackageMenuContent();

	TSharedRef< FUICommandList > ActionList;

	TSharedPtr< FExtender > MenuExtender;

	void CreateUATTask(const FString& CommandLine, const FText& TaskName, const FText &TaskShortName, const FSlateBrush* TaskIcon);

	struct EventData
	{
		FString EventName;
		double StartTime;
	};

	// Handles clicking the packager notification item's Cancel button.
	static void HandleUatCancelButtonClicked(TSharedPtr<FMonitoredProcess> PackagerProcess);

	// Handles clicking the hyper link on a packager notification item.
	static void HandleUatHyperlinkNavigate();

	// Handles canceled packager processes.
	static void HandleUatProcessCanceled(TWeakPtr<class SNotificationItem> NotificationItemPtr, FText TaskName, EventData Event);

	// Handles the completion of a packager process.
	static void HandleUatProcessCompleted(int32 ReturnCode, TWeakPtr<class SNotificationItem> NotificationItemPtr, FText TaskName, EventData Event);

	// Handles packager process output.
	static void HandleUatProcessOutput(FString Output, TWeakPtr<class SNotificationItem> NotificationItemPtr, FText TaskName);

	/** A default window size for the package dialog */
	static const FVector2D DEFAULT_WINDOW_SIZE;

	TAttribute<FText> PackageDialogTitle;
};

class SPackageContentDialog : public SCompoundWidget
{
public:
	enum EPackageContentDialogMode
	{
		PACKAGE_Weapon,
		PACKAGE_Hat,
	};

	SLATE_BEGIN_ARGS(SPackageContentDialog)
		: _PackageContent(),
		_DialogMode(PACKAGE_Weapon)
		{}
		SLATE_ARGUMENT(TSharedPtr<FPackageContent>, PackageContent)
		SLATE_ARGUMENT(EPackageContentDialogMode, DialogMode)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, TSharedPtr<SWindow> InParentWindow);

	void ClassChosen(UClass* ChosenClass);

protected:
	TSharedPtr<FPackageContent> PackageContent;

	TSharedPtr<SWindow> ParentWindow;
	EPackageContentDialogMode DialogMode;
};

class FHatClassFilter : public IClassViewerFilter
{
public:

	virtual bool IsClassAllowed(const FClassViewerInitializationOptions& InInitOptions, const UClass* InClass, TSharedRef< FClassViewerFilterFuncs > InFilterFuncs) override
	{
		if (NULL != InClass)
		{
			const bool bWeaponBased = InClass->IsChildOf(AUTHat::StaticClass());
			const bool bBlueprintType = InClass->HasAnyClassFlags(CLASS_CompiledFromBlueprint);
			return bWeaponBased && bBlueprintType;
		}
		return false;
	}

	virtual bool IsUnloadedClassAllowed(const FClassViewerInitializationOptions& InInitOptions, const TSharedRef< const IUnloadedBlueprintData > InUnloadedClassData, TSharedRef< FClassViewerFilterFuncs > InFilterFuncs) override
	{
		const bool bWeaponBased = InUnloadedClassData->IsChildOf(AUTHat::StaticClass());
		const bool bBlueprintType = InUnloadedClassData->HasAnyClassFlags(CLASS_CompiledFromBlueprint);
		return bWeaponBased && bBlueprintType;
	}
};

class FWeaponClassFilter : public IClassViewerFilter
{
public:

	virtual bool IsClassAllowed(const FClassViewerInitializationOptions& InInitOptions, const UClass* InClass, TSharedRef< FClassViewerFilterFuncs > InFilterFuncs) override
	{
		if (NULL != InClass)
		{
			const bool bWeaponBased = InClass->IsChildOf(AUTWeapon::StaticClass());
			const bool bBlueprintType = InClass->HasAnyClassFlags(CLASS_CompiledFromBlueprint);
			return bWeaponBased && bBlueprintType;
		}
		return false;
	}

	virtual bool IsUnloadedClassAllowed(const FClassViewerInitializationOptions& InInitOptions, const TSharedRef< const IUnloadedBlueprintData > InUnloadedClassData, TSharedRef< FClassViewerFilterFuncs > InFilterFuncs) override
	{
		const bool bWeaponBased = InUnloadedClassData->IsChildOf(AUTWeapon::StaticClass());
		const bool bBlueprintType = InUnloadedClassData->HasAnyClassFlags(CLASS_CompiledFromBlueprint);
		return bWeaponBased && bBlueprintType;
	}
};