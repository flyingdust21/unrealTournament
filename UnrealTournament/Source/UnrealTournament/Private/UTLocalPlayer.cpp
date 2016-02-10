// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#include "UnrealTournament.h"
#include "UTLocalPlayer.h"
#include "UTCharacter.h"
#include "Online.h"
#include "OnlineSubsystemTypes.h"
#include "OnlineTitleFileInterface.h"
#include "UTMenuGameMode.h"
#include "UTProfileSettings.h"
#include "UTGameViewportClient.h"
#include "Base/SUTMenuBase.h"
#include "SUTMainMenu.h"
#include "SUTServerBrowserPanel.h"
#include "SUTReplayBrowserPanel.h"
#include "SUTStatsViewerPanel.h"
#include "SUTCreditsPanel.h"
#include "SUTMessageBoxDialog.h"
#include "SUWindowsStyle.h"
#include "SUTStyle.h"
#include "SUTDialogBase.h"
#include "SUTToastBase.h"
#include "SUTWindowBase.h"
#include "SUTAdminMessageToast.h"
#include "SUTInputBoxDialog.h"
#include "SUTLoginDialog.h"
#include "SUTPlayerSettingsDialog.h"
#include "SUTPlayerInfoDialog.h"
#include "SUTHUDSettingsDialog.h"
#include "SUTQuickMatchWindow.h"
#include "SUTJoinInstanceWindow.h"
#include "SUTFriendsPopupWindow.h"
#include "SUTRedirectDialog.h"
#include "SUTVideoCompressionDialog.h"
#include "SUTLoadoutWindow.h"
#include "SUTBuyWindow.h"
#include "SUTMapVoteDialog.h"
#include "SUTReplayWindow.h"
#include "Menus/SUTReplayMenu.h"
#include "SUTAdminDialog.h"
#include "SUTDownloadAllDialog.h"
#include "SUTSpectatorWindow.h"
#include "UTAnalytics.h"
#include "Runtime/Analytics/Analytics/Public/Analytics.h"
#include "Runtime/Analytics/Analytics/Public/Interfaces/IAnalyticsProvider.h"
#include "Base64.h"
#include "UTGameEngine.h"
#include "Engine/DemoNetDriver.h"
#include "UTConsole.h"
#include "Runtime/Core/Public/Features/IModularFeatures.h"
#include "UTVideoRecordingFeature.h"
#include "SUTYoutubeUploadDialog.h"
#include "SUTYoutubeConsentDialog.h"
#include "UTLobbyGameState.h"
#include "UTLobbyPC.h"
#include "StatNames.h"
#include "UTChallengeManager.h"
#include "UTCharacterContent.h"
#include "Runtime/JsonUtilities/Public/JsonUtilities.h"
#include "SUTMatchSummaryPanel.h"
#include "SUTInGameHomePanel.h"
#include "UTMcpUtils.h"

#if WITH_SOCIAL
#include "Social.h"
#endif

UUTLocalPlayer::UUTLocalPlayer(const class FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bInitialSignInAttempt = true;
	LastProfileCloudWriteTime = 0;
	ProfileCloudWriteCooldownTime = 15;
	bShowSocialNotification = false;
	ServerPingBlockSize = 30;
	bSuppressToastsInGame = false;

	DownloadStatusText = FText::GetEmpty();
	Download_CurrentFile = TEXT("");
	Download_Percentage = 0.0;

	QuickMatchLimitTime = -60.0;
	RosterUpgradeText = FText::GetEmpty();
	CurrentSessionTrustLevel = 2;
	EarnedStars = 0;

	CloudProfileMagicNumberVersion1 = 0xBEEF0001;
	CloudProfileUE4VerForUnversionedProfile = 452;
	McpProfileManager = nullptr;
	bShowingFriendsMenu = false;

	bProgressionReadFromCloud = false;
	ELOReportCount = 0;
}

UUTLocalPlayer::~UUTLocalPlayer()
{
	// Terminate the dedicated server if we started one
	if (DedicatedServerProcessHandle.IsValid() && FPlatformProcess::IsProcRunning(DedicatedServerProcessHandle))
	{
		FPlatformProcess::TerminateProc(DedicatedServerProcessHandle);
	}
}

void UUTLocalPlayer::InitializeOnlineSubsystem()
{
	OnlineSubsystem = IOnlineSubsystem::Get();
	if (OnlineSubsystem) 
	{
		OnlineIdentityInterface = OnlineSubsystem->GetIdentityInterface();
		OnlineUserCloudInterface = OnlineSubsystem->GetUserCloudInterface();
		OnlineSessionInterface = OnlineSubsystem->GetSessionInterface();
		OnlinePresenceInterface = OnlineSubsystem->GetPresenceInterface();
		OnlineFriendsInterface = OnlineSubsystem->GetFriendsInterface();
		OnlineTitleFileInterface = OnlineSubsystem->GetTitleFileInterface();
	}

	if (OnlineIdentityInterface.IsValid())
	{
		OnLoginCompleteDelegate = OnlineIdentityInterface->AddOnLoginCompleteDelegate_Handle(GetControllerId(), FOnLoginCompleteDelegate::CreateUObject(this, &UUTLocalPlayer::OnLoginComplete));
		OnLoginStatusChangedDelegate = OnlineIdentityInterface->AddOnLoginStatusChangedDelegate_Handle(GetControllerId(), FOnLoginStatusChangedDelegate::CreateUObject(this, &UUTLocalPlayer::OnLoginStatusChanged));
		OnLogoutCompleteDelegate = OnlineIdentityInterface->AddOnLogoutCompleteDelegate_Handle(GetControllerId(), FOnLogoutCompleteDelegate::CreateUObject(this, &UUTLocalPlayer::OnLogoutComplete));
	}

	if (OnlineUserCloudInterface.IsValid())
	{
		OnReadUserFileCompleteDelegate = OnlineUserCloudInterface->AddOnReadUserFileCompleteDelegate_Handle(FOnReadUserFileCompleteDelegate::CreateUObject(this, &UUTLocalPlayer::OnReadUserFileComplete));
		OnWriteUserFileCompleteDelegate = OnlineUserCloudInterface->AddOnWriteUserFileCompleteDelegate_Handle(FOnWriteUserFileCompleteDelegate::CreateUObject(this, &UUTLocalPlayer::OnWriteUserFileComplete));
		OnDeleteUserFileCompleteDelegate = OnlineUserCloudInterface->AddOnDeleteUserFileCompleteDelegate_Handle(FOnDeleteUserFileCompleteDelegate::CreateUObject(this, &UUTLocalPlayer::OnDeleteUserFileComplete));
		OnEnumerateUserFilesCompleteDelegate = OnlineUserCloudInterface->AddOnEnumerateUserFilesCompleteDelegate_Handle(FOnEnumerateUserFilesCompleteDelegate::CreateUObject(this, &UUTLocalPlayer::OnEnumerateUserFilesComplete));
	}

	if (OnlineSessionInterface.IsValid())
	{
		OnJoinSessionCompleteDelegate = OnlineSessionInterface->AddOnJoinSessionCompleteDelegate_Handle(FOnJoinSessionCompleteDelegate::CreateUObject(this, &UUTLocalPlayer::OnJoinSessionComplete));
	}

	if (OnlineTitleFileInterface.IsValid())
	{
		OnReadTitleFileCompleteDelegate = OnlineTitleFileInterface->AddOnReadFileCompleteDelegate_Handle(FOnReadFileCompleteDelegate::CreateUObject(this, &UUTLocalPlayer::OnReadTitleFileComplete));
	}
}

void UUTLocalPlayer::CleanUpOnlineSubSystyem()
{
	if (OnlineSubsystem)
	{
		if (OnlineIdentityInterface.IsValid())
		{
			OnlineIdentityInterface->ClearOnLoginCompleteDelegate_Handle(GetControllerId(), OnLoginCompleteDelegate);
			OnlineIdentityInterface->ClearOnLoginStatusChangedDelegate_Handle(GetControllerId(), OnLoginStatusChangedDelegate);
			OnlineIdentityInterface->ClearOnLogoutCompleteDelegate_Handle(GetControllerId(), OnLogoutCompleteDelegate);
		}
		if (OnlineUserCloudInterface.IsValid())
		{
			OnlineUserCloudInterface->ClearOnReadUserFileCompleteDelegate_Handle(OnReadUserFileCompleteDelegate);
			OnlineUserCloudInterface->ClearOnWriteUserFileCompleteDelegate_Handle(OnWriteUserFileCompleteDelegate);
			OnlineUserCloudInterface->ClearOnDeleteUserFileCompleteDelegate_Handle(OnDeleteUserFileCompleteDelegate);
			OnlineUserCloudInterface->ClearOnEnumerateUserFilesCompleteDelegate_Handle(OnEnumerateUserFilesCompleteDelegate);
		}
		if (OnlineSessionInterface.IsValid())
		{
			OnlineSessionInterface->ClearOnJoinSessionCompleteDelegate_Handle(OnJoinSessionCompleteDelegate);
		}
	}
}

bool UUTLocalPlayer::Exec(UWorld* InWorld, const TCHAR* Cmd, FOutputDevice& Ar)
{
	// disallow certain commands in shipping builds
#if (UE_BUILD_SHIPPING || UE_BUILD_TEST)
	if (FParse::Command(&Cmd, TEXT("SHOW")))
	{
		return true;
	}
#endif
	return Super::Exec(InWorld, Cmd, Ar);
}

bool UUTLocalPlayer::IsAFriend(FUniqueNetIdRepl PlayerId)
{
	return (PlayerId.IsValid() && OnlineFriendsInterface.IsValid() && OnlineFriendsInterface->IsFriend(0, *PlayerId, EFriendsLists::ToString(EFriendsLists::InGamePlayers)));
}

FString UUTLocalPlayer::GetNickname() const
{
	return PlayerNickname;
}

FText UUTLocalPlayer::GetAccountDisplayName() const
{
	if (OnlineIdentityInterface.IsValid() && PlayerController && PlayerController->PlayerState)
	{

		TSharedPtr<const FUniqueNetId> UserId = OnlineIdentityInterface->GetUniquePlayerId(GetControllerId());
		if (UserId.IsValid())
		{
			TSharedPtr<FUserOnlineAccount> UserAccount = OnlineIdentityInterface->GetUserAccount(*UserId);
			if (UserAccount.IsValid())
			{
				return FText::FromString(UserAccount->GetDisplayName());
			}
		}
	}
	return FText::GetEmpty();
}

FString UUTLocalPlayer::GetAccountName() const
{
	if (OnlineIdentityInterface.IsValid() && PlayerController && PlayerController->PlayerState)
	{
		TSharedPtr<const FUniqueNetId> UserId = OnlineIdentityInterface->GetUniquePlayerId(GetControllerId());
		if (UserId.IsValid())
		{
			TSharedPtr<FUserOnlineAccount> UserAccount = OnlineIdentityInterface->GetUserAccount(*UserId);
			if (UserAccount.IsValid())
			{
				FString Result;
				UserAccount->GetAuthAttribute(TEXT("email"), Result);
				return Result;
			}
		}
	}
	return TEXT("");
}

FText UUTLocalPlayer::GetAccountSummary() const
{
	if (OnlineIdentityInterface.IsValid() && PlayerController && PlayerController->PlayerState)
	{

		TSharedPtr<const FUniqueNetId> UserId = OnlineIdentityInterface->GetUniquePlayerId(GetControllerId());
		if (UserId.IsValid())
		{
			TSharedPtr<FUserOnlineAccount> UserAccount = OnlineIdentityInterface->GetUserAccount(*UserId);
			if (UserAccount.IsValid())
			{
				return FText::Format(NSLOCTEXT("UTLocalPlayer","AccountSummaryFormat","{0} # of Friends: {1}  # Online: {2}"), FText::FromString(UserAccount->GetDisplayName()), FText::AsNumber(0),FText::AsNumber(0));
			}
		}
	}
	return FText::GetEmpty();
}

void UUTLocalPlayer::PlayerAdded(class UGameViewportClient* InViewportClient, int32 InControllerID)
{
#if !UE_SERVER
	SUWindowsStyle::Initialize();
	SUTStyle::Initialize();
#endif

	Super::PlayerAdded(InViewportClient, InControllerID);

	if (FUTAnalytics::IsAvailable())
	{
		FString OSMajor;
		FString OSMinor;
		FPlatformMisc::GetOSVersions(OSMajor, OSMinor);
		TArray<FAnalyticsEventAttribute> ParamArray;
		ParamArray.Add(FAnalyticsEventAttribute(TEXT("OSMajor"), OSMajor));
		ParamArray.Add(FAnalyticsEventAttribute(TEXT("OSMinor"), OSMinor));
		ParamArray.Add(FAnalyticsEventAttribute(TEXT("CPUVendor"), FPlatformMisc::GetCPUVendor()));
		ParamArray.Add(FAnalyticsEventAttribute(TEXT("CPUBrand"), FPlatformMisc::GetCPUBrand()));
		FUTAnalytics::GetProvider().RecordEvent( TEXT("SystemInfo"), ParamArray );
	}

	if (!InViewportClient->GetWorld()->IsPlayInEditor())
	{
		if (!HasAnyFlags(RF_ClassDefaultObject))
		{
			// Initialize the Online Subsystem for this player
			InitializeOnlineSubsystem();

			if (OnlineIdentityInterface.IsValid())
			{
				// Attempt to Auto-Login to MCP
				if (!OnlineIdentityInterface->AutoLogin(GetControllerId()))
				{
					bInitialSignInAttempt = false;
				}
			}
		}
	}
}

bool UUTLocalPlayer::IsMenuGame()
{
	if (bNoMidGameMenu) return true;

	if (GetWorld()->GetNetMode() == NM_Standalone)
	{
		AUTMenuGameMode* GM = Cast<AUTMenuGameMode>(GetWorld()->GetAuthGameMode());
		return GM != NULL;
	}
	return false;
}

#if !UE_SERVER
void UUTLocalPlayer::OpenWindow(TSharedPtr<SUTWindowBase> WindowToOpen)
{
	// Make sure this window isn't already in the stack.
	if (WindowStack.Find(WindowToOpen) == INDEX_NONE)
	{
		GEngine->GameViewport->AddViewportWidgetContent(WindowToOpen.ToSharedRef(), 1);
		WindowStack.Add(WindowToOpen);
		WindowToOpen->Open();
	}
}

bool UUTLocalPlayer::CloseWindow(TSharedPtr<SUTWindowBase> WindowToClose)
{
	// Find this window in the stack.
	if (WindowStack.Find(WindowToClose) != INDEX_NONE)
	{
		return WindowToClose->Close();
	}

	return false;
}

void UUTLocalPlayer::WindowClosed(TSharedPtr<SUTWindowBase> WindowThatWasClosed)
{
	if (WindowStack.Find(WindowThatWasClosed) != INDEX_NONE)
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(WindowThatWasClosed.ToSharedRef());
		WindowStack.Remove(WindowThatWasClosed);
	}
}
#endif

void UUTLocalPlayer::ShowMenu(const FString& Parameters)
{
#if !UE_SERVER
	if (bRecordingReplay)
	{
		static const FName VideoRecordingFeatureName("VideoRecording");
		if (IModularFeatures::Get().IsModularFeatureAvailable(VideoRecordingFeatureName))
		{
			UTVideoRecordingFeature* VideoRecorder = &IModularFeatures::Get().GetModularFeature<UTVideoRecordingFeature>(VideoRecordingFeatureName);
			if (VideoRecorder)
			{
				VideoRecorder->CancelRecording();
			}
		}
	}

	// If we have a menu up, hide it before opening the new one
	if (DesktopSlateWidget.IsValid())
	{
		UE_LOG(UT,Log,TEXT("Closing Existing menu so we can open a new one"));
		HideMenu();
	}

	// Create the slate widget if it doesn't exist
	if (!DesktopSlateWidget.IsValid())
	{
		if ( IsMenuGame() )
		{
			SAssignNew(DesktopSlateWidget, SUTMainMenu).PlayerOwner(this);
		}
		else if (IsReplay())
		{
			SAssignNew(DesktopSlateWidget, SUTReplayMenu).PlayerOwner(this);
		}
		else
		{
			AGameState* GameState = GetWorld()->GetGameState<AGameState>();
			if (GameState != nullptr && GameState->GameModeClass != nullptr)
			{
				AUTBaseGameMode* UTGameMode = GameState->GameModeClass->GetDefaultObject<AUTBaseGameMode>();
				if (UTGameMode != nullptr)
				{
					DesktopSlateWidget = UTGameMode->GetGameMenu(this);
				}
			}

		}
		if (DesktopSlateWidget.IsValid())
		{
			GEngine->GameViewport->AddViewportWidgetContent( SNew(SWeakWidget).PossiblyNullContent(DesktopSlateWidget.ToSharedRef()),1);
		}

		// Make it visible.
		if ( DesktopSlateWidget.IsValid() )
		{
			// Widget is already valid, just make it visible.
			DesktopSlateWidget->SetVisibility(EVisibility::Visible);
			DesktopSlateWidget->OnMenuOpened(Parameters);

			if (PlayerController)
			{
				if (!IsMenuGame())
				{
					// If we are in a single player game, and that game is either in the player intro or the post match state, then
					// clear the menu pause.

					if (GetWorld()->GetNetMode() != NM_Client)
					{
						AUTGameMode* GameMode = GetWorld()->GetAuthGameMode<AUTGameMode>();
						if (GameMode && GameMode->GetMatchState() != MatchState::PlayerIntro && GameMode->GetMatchState() != MatchState::WaitingPostMatch)
						{
							PlayerController->SetPause(true);
						}
					}
				}
			}
		}
	}
#endif
}
void UUTLocalPlayer::HideMenu()
{
#if !UE_SERVER
	if (ContentLoadingMessage.IsValid())
	{
		UE_LOG(UT,Log,TEXT("Can't close menus during loading"));
		return; // Don't allow someone to close the menu while we are loading....
	}

	if (DesktopSlateWidget.IsValid())
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(DesktopSlateWidget.ToSharedRef());
		DesktopSlateWidget->OnMenuClosed();
		DesktopSlateWidget.Reset();
		if (PlayerController)
		{
			PlayerController->SetPause(false);
		}

		if (SpectatorWidget.IsValid())
		{
			FSlateApplication::Get().SetKeyboardFocus(SpectatorWidget, EKeyboardFocusCause::Keyboard);
		}

		if (ReplayWindow.IsValid())
		{
			FSlateApplication::Get().SetKeyboardFocus(ReplayWindow, EKeyboardFocusCause::Keyboard);
		}
	}
	else
	{
		UE_LOG(UT,Log,TEXT("Call to HideMenu() when without a menu being opened."));
	}
	CloseConnectingDialog();
#endif
}

void UUTLocalPlayer::OpenTutorialMenu()
{
#if !UE_SERVER
	if (IsMenuGame() && DesktopSlateWidget.IsValid())
	{
		StaticCastSharedPtr<SUTMainMenu>(DesktopSlateWidget)->OpenTutorialMenu();
	}
#endif
}

void UUTLocalPlayer::MessageBox(FText MessageTitle, FText MessageText)
{
#if !UE_SERVER
	ShowMessage(MessageTitle, MessageText, UTDIALOG_BUTTON_OK, NULL);
#endif
}

#if !UE_SERVER
TSharedPtr<class SUTDialogBase>  UUTLocalPlayer::ShowMessage(FText MessageTitle, FText MessageText, uint16 Buttons, const FDialogResultDelegate& Callback, FVector2D DialogSize)
{
	TSharedPtr<class SUTDialogBase> NewDialog;
	if (DialogSize.IsNearlyZero())
	{
		SAssignNew(NewDialog, SUTMessageBoxDialog)
			.PlayerOwner(this)
			.DialogTitle(MessageTitle)
			.MessageText(MessageText)
			.ButtonMask(Buttons)
			.OnDialogResult(Callback);
	}
	else
	{
		SAssignNew(NewDialog, SUTMessageBoxDialog)
			.PlayerOwner(this)
			.bDialogSizeIsRelative(true)
			.DialogSize(DialogSize)
			.DialogTitle(MessageTitle)
			.MessageText(MessageText)
			.ButtonMask(Buttons)
			.OnDialogResult(Callback);
	}

	OpenDialog( NewDialog.ToSharedRef() );
	return NewDialog;
}

TSharedPtr<class SUTDialogBase> UUTLocalPlayer::ShowSupressableConfirmation(FText MessageTitle, FText MessageText, FVector2D DialogSize, bool &InOutShouldSuppress, const FDialogResultDelegate& Callback)
{
	auto OnGetSuppressibleState = [&InOutShouldSuppress]() { return InOutShouldSuppress ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; };

	auto OnSetSuppressibleState = [&InOutShouldSuppress](ECheckBoxState CheckBoxState) 
	{ 
		InOutShouldSuppress = CheckBoxState == ECheckBoxState::Checked;
	};

	TSharedPtr<class SUTDialogBase> NewDialog;
	if (DialogSize.IsNearlyZero())
	{
		SAssignNew(NewDialog, SUTMessageBoxDialog)
			.PlayerOwner(this)
			.DialogTitle(MessageTitle)
			.MessageText(MessageText)
			.ButtonMask(UTDIALOG_BUTTON_OK)
			.OnDialogResult(Callback)
			.IsSuppressible(true)
			.SuppressibleCheckBoxState_Lambda( OnGetSuppressibleState )
			.OnSuppressibleCheckStateChanged_Lambda( OnSetSuppressibleState );
	}
	else
	{
		SAssignNew(NewDialog, SUTMessageBoxDialog)
			.PlayerOwner(this)
			.bDialogSizeIsRelative(true)
			.DialogSize(DialogSize)
			.DialogTitle(MessageTitle)
			.MessageText(MessageText)
			.ButtonMask(UTDIALOG_BUTTON_OK)
			.IsSuppressible(true)
			.SuppressibleCheckBoxState_Lambda(OnGetSuppressibleState)
			.OnSuppressibleCheckStateChanged_Lambda(OnSetSuppressibleState)
			.OnDialogResult(Callback);
	}

	OpenDialog(NewDialog.ToSharedRef());
	return NewDialog;
}

void UUTLocalPlayer::OpenDialog(TSharedRef<SUTDialogBase> Dialog, int32 ZOrder)
{
	GEngine->GameViewport->AddViewportWidgetContent(Dialog, ZOrder);
	Dialog->OnDialogOpened();
	OpenDialogs.Add(Dialog);
}

void UUTLocalPlayer::CloseDialog(TSharedRef<SUTDialogBase> Dialog)
{
	OpenDialogs.Remove(Dialog);
	Dialog->OnDialogClosed();
	GEngine->GameViewport->RemoveViewportWidgetContent(Dialog);

	if (DesktopSlateWidget.IsValid())
	{
		FSlateApplication::Get().SetKeyboardFocus(DesktopSlateWidget, EKeyboardFocusCause::Keyboard);
	}
}

TSharedPtr<class SUTServerBrowserPanel> UUTLocalPlayer::GetServerBrowser()
{
	if (!ServerBrowserWidget.IsValid())
	{
		SAssignNew(ServerBrowserWidget, SUTServerBrowserPanel, this);
	}

	return ServerBrowserWidget;
}

TSharedPtr<class SUTReplayBrowserPanel> UUTLocalPlayer::GetReplayBrowser()
{
	if (!ReplayBrowserWidget.IsValid())
	{
		SAssignNew(ReplayBrowserWidget, SUTReplayBrowserPanel, this);
	}

	return ReplayBrowserWidget;
}

TSharedPtr<class SUTStatsViewerPanel> UUTLocalPlayer::GetStatsViewer()
{
	if (!StatsViewerWidget.IsValid())
	{
		SAssignNew(StatsViewerWidget, SUTStatsViewerPanel, this);
	}

	return StatsViewerWidget;
}

TSharedPtr<class SUTCreditsPanel> UUTLocalPlayer::GetCreditsPanel()
{
	if (!CreditsPanelWidget.IsValid())
	{
		SAssignNew(CreditsPanelWidget, SUTCreditsPanel, this);
	}

	return CreditsPanelWidget;
}

bool UUTLocalPlayer::AreMenusOpen()
{
	return DesktopSlateWidget.IsValid()
		|| LoadoutMenu.IsValid()
		|| OpenDialogs.Num() > 0;
	//Add any widget thats not in the menu here
	//TODO: Should look through each active widget and determine the needed input mode EIM_UIOnly > EIM_GameAndUI > EIM_GameOnly
}

#endif

void UUTLocalPlayer::ChangeStatsViewerTarget(FString InStatsID)
{
#if !UE_SERVER
	if (StatsViewerWidget.IsValid())
	{
		StatsViewerWidget->ChangeStatsID(InStatsID);
	}
#endif
}

void UUTLocalPlayer::ShowHUDSettings()
{
#if !UE_SERVER
	if (!HUDSettings.IsValid())
	{
		SAssignNew(HUDSettings, SUTHUDSettingsDialog)
			.PlayerOwner(this);

		OpenDialog( HUDSettings.ToSharedRef() );

		if (PlayerController)
		{
			if (!IsMenuGame())
			{
				PlayerController->SetPause(true);
			}
		}
	}
#endif
}

void UUTLocalPlayer::HideHUDSettings()
{
#if !UE_SERVER

	if (HUDSettings.IsValid())
	{
		CloseDialog(HUDSettings.ToSharedRef());
		HUDSettings.Reset();

		if (!IsMenuGame())
		{
			if (PlayerController)
			{
				PlayerController->SetPause(false);
			}
		}
	}
#endif
}

bool UUTLocalPlayer::IsLoggedIn() const
{ 
	return OnlineIdentityInterface.IsValid() && OnlineIdentityInterface->GetLoginStatus(GetControllerId());
}


void UUTLocalPlayer::LoginOnline(FString EpicID, FString Auth, bool bIsRememberToken, bool bSilentlyFail)
{
	if ( !OnlineIdentityInterface.IsValid() ) return;

	FString Override;
	if ( FParse::Value(FCommandLine::Get(),TEXT("-id="),Override))
	{
		EpicID = Override;
	}

	if ( FParse::Value(FCommandLine::Get(),TEXT("-pass="),Override))
	{
		Auth=Override;
		bIsRememberToken=false;
	}

	if (EpicID == TEXT(""))
	{
		EpicID = LastEpicIDLogin;
	}

	// Save this for later.
	PendingLoginUserName = EpicID;
	bSilentLoginFail = bSilentlyFail;

	if (EpicID == TEXT("") || Auth == TEXT(""))
	{
		GetAuth();
		return;
	}

	FOnlineAccountCredentials AccountCreds(TEXT("epic"), EpicID, Auth);
	if (bIsRememberToken)
	{
		AccountCreds.Type = TEXT("refresh");
	}

	// Begin the Login Process...
	if (!OnlineIdentityInterface->Login(GetControllerId(), AccountCreds))
	{
#if !UE_SERVER
		// We should never fail here unless something has gone horribly wrong
		if (bSilentLoginFail)
		{
			UE_LOG(UT, Warning, TEXT("Could not connect to the online subsystem. Please check your connection and try again."));
		}
		else
		{
			ShowMessage(NSLOCTEXT("MCPMessages", "OnlineError", "Online Error"), NSLOCTEXT("MCPMessages", "UnknownLoginFailuire", "Could not connect to the online subsystem.  Please check your connection and try again."), UTDIALOG_BUTTON_OK, NULL);
		}
		return;
#endif
	}
}

void UUTLocalPlayer::Logout()
{
	if (IsLoggedIn() && OnlineIdentityInterface.IsValid())
	{
		// Begin the Login Process....
		if (!OnlineIdentityInterface->Logout(GetControllerId()))
		{
#if !UE_SERVER
			// We should never fail here unless something has gone horribly wrong
			ShowMessage(NSLOCTEXT("MCPMessages","OnlineError","Online Error"), NSLOCTEXT("MCPMessages","UnknownLogoutFailuire","Could not log out from the online subsystem.  Please check your connection and try again."), UTDIALOG_BUTTON_OK, NULL);
			return;
#endif
		}
	}
}


FString UUTLocalPlayer::GetOnlinePlayerNickname()
{
	return IsLoggedIn() ? OnlineIdentityInterface->GetPlayerNickname(0) : TEXT("None");
}

void UUTLocalPlayer::OnLoginComplete(int32 LocalUserNum, bool bWasSuccessful, const FUniqueNetId& UniqueID, const FString& ErrorMessage)
{
	if (bWasSuccessful)
	{
		// Save the creds for the next auto-login

		TSharedPtr<FUserOnlineAccount> Account = OnlineIdentityInterface->GetUserAccount(UniqueID);
		if (Account.IsValid())
		{
			FString RememberMeToken;
			FString Token;
			Account->GetAuthAttribute(TEXT("refresh_token"), RememberMeToken);

			if ( Account->GetAuthAttribute(TEXT("ut:developer"), Token) )			CommunityRole = EUnrealRoles::Developer;
			else if ( Account->GetAuthAttribute(TEXT("ut:contributor"), Token) )	CommunityRole = EUnrealRoles::Contributor;
			else if ( Account->GetAuthAttribute(TEXT("ut:concepter"), Token) )		CommunityRole = EUnrealRoles::Concepter;
			else if ( Account->GetAuthAttribute(TEXT("ut:prototyper"), Token) )		CommunityRole = EUnrealRoles::Prototyper;
			else if ( Account->GetAuthAttribute(TEXT("ut:marketplace"), Token) )	CommunityRole = EUnrealRoles::Marketplace;
			else if ( Account->GetAuthAttribute(TEXT("ut:ambassador"), Token) )		CommunityRole = EUnrealRoles::Ambassador;
			else 
			{
				CommunityRole = EUnrealRoles::Gamer;
			}
			
			if (CommunityRole == EUnrealRoles::Developer)
			{
				EpicFlagCheck();			
			}


			LastEpicIDLogin = PendingLoginUserName;
			LastEpicRememberMeToken = RememberMeToken;
			SaveConfig();

			// Now download initial profiles.
			TSharedPtr<const FUniqueNetId> UserId = MakeShareable(new FUniqueNetIdString(UniqueID));
#if WITH_PROFILE
			GetMcpProfileManager()->Init(UserId, UserId, Account->GetDisplayName(), FUTProfilesLoaded::CreateUObject(this, &UUTLocalPlayer::OnProfileManagerInitComplete));
#endif
		}

		PendingLoginUserName = TEXT("");

		LoadProfileSettings();
		FText WelcomeToast = FText::Format(NSLOCTEXT("MCP","MCPWelcomeBack","Welcome back {0}"), FText::FromString(*GetOnlinePlayerNickname()));
		ShowToast(WelcomeToast);
		
#if WITH_SOCIAL
		// Init the Friends And Chat system
		ISocialModule::Get().GetFriendsAndChatManager()->Login(OnlineSubsystem, true);
		ISocialModule::Get().GetFriendsAndChatManager()->SetAnalyticsProvider(FUTAnalytics::GetProviderPtr());

		if (!ISocialModule::Get().GetFriendsAndChatManager()->GetNotificationService()->OnJoinGame().IsBoundToObject(this))
 		{
			ISocialModule::Get().GetFriendsAndChatManager()->GetNotificationService()->OnJoinGame().AddUObject(this, &UUTLocalPlayer::HandleFriendsJoinGame);
 		}
		if (!ISocialModule::Get().GetFriendsAndChatManager()->AllowFriendsJoinGame().IsBoundToObject(this))
		{
			ISocialModule::Get().GetFriendsAndChatManager()->AllowFriendsJoinGame().BindUObject(this, &UUTLocalPlayer::AllowFriendsJoinGame);
		}
		if (!ISocialModule::Get().GetFriendsAndChatManager()->GetNotificationService()->OnNotificationsAvailable().IsBoundToObject(this))
 		{
 			ISocialModule::Get().GetFriendsAndChatManager()->GetNotificationService()->OnNotificationsAvailable().AddUObject(this, &UUTLocalPlayer::HandleFriendsNotificationAvail);
 		}
		if (!ISocialModule::Get().GetFriendsAndChatManager()->GetNotificationService()->OnSendNotification().IsBoundToObject(this))
 		{
			ISocialModule::Get().GetFriendsAndChatManager()->GetNotificationService()->OnSendNotification().AddUObject(this, &UUTLocalPlayer::HandleFriendsActionNotification);
 		}
#endif

#if !UE_SERVER
		// Make sure popup is created so we dont lose any messages
		GetFriendsPopup();
#endif

		// on successful auto login, attempt to join an accepted friend game invite
		if (bInitialSignInAttempt)
		{
			FString SessionId;
			FString FriendId;
			if (FParse::Value(FCommandLine::Get(), TEXT("invitesession="), SessionId) && !SessionId.IsEmpty() &&
				FParse::Value(FCommandLine::Get(), TEXT("invitefrom="), FriendId) && !FriendId.IsEmpty())
			{
				JoinFriendSession(FUniqueNetIdString(FriendId), FUniqueNetIdString(SessionId));
			}
		}

		AUTPlayerController* UTPC = Cast<AUTPlayerController>(PlayerController);
		if (UTPC)
		{
			if (OnlineIdentityInterface.IsValid() && OnlineIdentityInterface->GetLoginStatus(GetControllerId()))
			{
				TSharedPtr<const FUniqueNetId> UserId = OnlineIdentityInterface->GetUniquePlayerId(GetControllerId());
				if (UserId.IsValid())
				{
					UTPC->ServerReceiveStatsID(UserId->ToString());
					if (UTPC->PlayerState)
					{
						UTPC->PlayerState->SetUniqueId(UserId); 
					}
				}
			}
		}
	}

	// We have enough credentials to auto-login.  So try it, but silently fail if we cant.
	else if (bInitialSignInAttempt)
	{
		if (LastEpicIDLogin != TEXT("") && LastEpicRememberMeToken != TEXT(""))
		{
			bInitialSignInAttempt = false;
			LoginOnline(LastEpicIDLogin, LastEpicRememberMeToken, true, true);
		}
	}

	// Otherwise if this is the first attempt, then silently fair
	else if (!bSilentLoginFail)
	{
		// Broadcast the failure to the UI.
		PlayerOnlineStatusChanged.Broadcast(this, ELoginStatus::NotLoggedIn, UniqueID);
		GetAuth(ErrorMessage);
	}
}

void UUTLocalPlayer::GetAuth(FString ErrorMessage)
{
#if !UE_SERVER
	if (GetWorld()->IsPlayInEditor())
	{
		return;
	}

	if (LoginDialog.IsValid())
	{
		return;
	}

	bool bError = ErrorMessage != TEXT("");

	SAssignNew(LoginDialog, SUTLoginDialog)
		.OnDialogResult(FDialogResultDelegate::CreateUObject(this, &UUTLocalPlayer::AuthDialogClosed))
		.UserIDText(PendingLoginUserName)
		.ErrorText(bError ? FText::FromString(ErrorMessage) : FText::GetEmpty())
		.PlayerOwner(this);

	GEngine->GameViewport->AddViewportWidgetContent(LoginDialog.ToSharedRef(), 160);
	LoginDialog->SetInitialFocus();

#endif
}

void UUTLocalPlayer::OnLoginStatusChanged(int32 LocalUserNum, ELoginStatus::Type PreviousLoginStatus, ELoginStatus::Type LoginStatus, const FUniqueNetId& UniqueID)
{
	UE_LOG(UT,Verbose,TEXT("***[LoginStatusChanged]*** - User %i - %i"), LocalUserNum, int32(LoginStatus));

	// If we have logged out, or started using the local profile, then clear the online profile.
	if (LoginStatus == ELoginStatus::NotLoggedIn || LoginStatus == ELoginStatus::UsingLocalProfile)
	{
		// Clear out the MCP storage
		MCPPulledData.bValid = false;

		CurrentProfileSettings = NULL;
		FUTAnalytics::LoginStatusChanged(FString());

		if (bPendingLoginCreds)
		{
			bPendingLoginCreds = false;
			LoginOnline(PendingLoginName, PendingLoginPassword);
			PendingLoginPassword = TEXT("");
		}

		// If we are connected to a server, then exit back to the main menu.
		if (GetWorld()->GetNetMode() == NM_Client)
		{
			ReturnToMainMenu();		
		}
	}
	else if (LoginStatus == ELoginStatus::LoggedIn)
	{
		if (OnlineTitleFileInterface.IsValid())
		{
			OnlineTitleFileInterface->ReadFile(GetMCPStorageFilename());
		}

		ReadELOFromBackend();
		UpdatePresence(LastPresenceUpdate, bLastAllowInvites,bLastAllowInvites,bLastAllowInvites,false);
		ReadCloudFileListing();
		// query entitlements for UI
		IOnlineEntitlementsPtr EntitlementsInterface = OnlineSubsystem->GetEntitlementsInterface();
		if (EntitlementsInterface.IsValid())
		{
			EntitlementsInterface->QueryEntitlements(UniqueID, TEXT("ut"));
		}
		FUTAnalytics::LoginStatusChanged(UniqueID.ToString());

		AUTBasePlayerController* PC = Cast<AUTBasePlayerController>(PlayerController);
		if (PC)
		{
			PC->ClientGenericInitialization();
		}

		// If we have a pending session, then join it.
		JoinPendingSession();
	}


	PlayerOnlineStatusChanged.Broadcast(this, LoginStatus, UniqueID);
}

void UUTLocalPlayer::ReadCloudFileListing()
{
	if (OnlineUserCloudInterface.IsValid() && OnlineIdentityInterface.IsValid())
	{
		TSharedPtr<const FUniqueNetId> UserId = OnlineIdentityInterface->GetUniquePlayerId(GetControllerId());
		if (UserId.IsValid())
		{
			OnlineUserCloudInterface->EnumerateUserFiles(*UserId.Get());
		}
	}
}

void UUTLocalPlayer::OnEnumerateUserFilesComplete(bool bWasSuccessful, const FUniqueNetId& InUserId)
{
	UE_LOG(UT, Verbose, TEXT("OnEnumerateUserFilesComplete %d"), bWasSuccessful ? 1 : 0);
	UUTGameEngine* UTEngine = Cast<UUTGameEngine>(GEngine);
	if (UTEngine)
	{
		UTEngine->CloudContentChecksums.Empty();

		if (OnlineUserCloudInterface.IsValid() && OnlineIdentityInterface.IsValid())
		{
			TArray<FCloudFileHeader> UserFiles;
			OnlineUserCloudInterface->GetUserFileList(InUserId, UserFiles);
			for (int32 i = 0; i < UserFiles.Num(); i++)
			{
				TArray<uint8> DecodedHash;
				FBase64::Decode(UserFiles[i].Hash, DecodedHash);
				FString Hash = BytesToHex(DecodedHash.GetData(), DecodedHash.Num());
				UE_LOG(UT, Verbose, TEXT("%s %s"), *UserFiles[i].FileName, *Hash);
				UTEngine->CloudContentChecksums.Add(FPaths::GetBaseFilename(UserFiles[i].FileName), Hash);
			}
		}		
	}
}

void UUTLocalPlayer::OnLogoutComplete(int32 LocalUserNum, bool bWasSuccessful)
{
	UE_LOG(UT,Verbose,TEXT("***[Logout Complete]*** - User %i"), LocalUserNum);
	// TO-DO: Add a Toast system for displaying stuff like this

	GetWorld()->GetTimerManager().ClearTimer(ProfileWriteTimerHandle);

#if !UE_SERVER
	if (ServerBrowserWidget.IsValid())
	{
		if (DesktopSlateWidget.IsValid())
		{
			DesktopSlateWidget->ShowHomePanel();
			ServerBrowserWidget.Reset();
		}
	}
#endif

	// If we have pending login creds then try to log right back in.
	if (bPendingLoginCreds)
	{
		bPendingLoginCreds = false;
		LoginOnline(PendingLoginName, PendingLoginPassword);
		PendingLoginPassword = TEXT("");
	}
	else
	{
		ShowToast(NSLOCTEXT("UTLocalPlayer","LoggedOutMsg","You have logged out!"));
	}

}

#if !UE_SERVER

void UUTLocalPlayer::CloseAuth()
{
	GEngine->GameViewport->RemoveViewportWidgetContent(LoginDialog.ToSharedRef());
	LoginDialog.Reset();
}

void UUTLocalPlayer::AuthDialogClosed(TSharedPtr<SCompoundWidget> Widget, uint16 ButtonID)
{
	
	if (ButtonID != UTDIALOG_BUTTON_CANCEL)
	{
		if (LoginDialog.IsValid())
		{
			// Look to see if we are already logged in.
			if ( IsLoggedIn() )
			{
				bPendingLoginCreds = true;
				PendingLoginName = LoginDialog->GetEpicID();
				PendingLoginPassword = LoginDialog->GetPassword();

				CloseAuth();


				// If we are in an active session, warn that this will cause you to go back to the main menu.
				TSharedPtr<const FUniqueNetId> UserId = OnlineIdentityInterface->GetUniquePlayerId(0);
				if (UserId.IsValid() && OnlineSessionInterface->IsPlayerInSession(GameSessionName, *UserId))
				{
					ShowMessage(NSLOCTEXT("UTLocalPlayer", "SwitchLoginsTitle", "Change Users..."), NSLOCTEXT("UTLocalPlayer", "SwitchLoginsMsg", "Switching users will cause you to return to the main menu and leave any game you are currently in.  Are you sure you wish to do this?"), UTDIALOG_BUTTON_YES + UTDIALOG_BUTTON_NO, FDialogResultDelegate::CreateUObject(this, &UUTLocalPlayer::OnSwitchUserResult),FVector2D(0.25,0.25));					
				}
				else
				{
					Logout();
				}
				return;
			}

			else
			{
				FString UserName = LoginDialog->GetEpicID();
				FString Password = LoginDialog->GetPassword();
				CloseAuth();
				LoginOnline(UserName, Password,false);
			}
		}
	}
	else
	{
		if (LoginDialog.IsValid())
		{
			CloseAuth();
		}
		PendingLoginUserName = TEXT("");
	}
}

void UUTLocalPlayer::OnSwitchUserResult(TSharedPtr<SCompoundWidget> Widget, uint16 ButtonID)
{
	if (ButtonID == UTDIALOG_BUTTON_YES)
	{
		// If we are in an active session, then we have to force a return to the main menu.  If we are not in an active session (ie: setting at the main menu)
		// we can just logout/login..
		TSharedPtr<const FUniqueNetId> UserId = OnlineIdentityInterface->GetUniquePlayerId(0);
		if (UserId.IsValid() && OnlineSessionInterface->IsPlayerInSession(GameSessionName, *UserId))
		{
			// kill the current menu....
			HideMenu();
			ReturnToMainMenu();	
		}
		else
		{
			Logout();
		}
	}
	else
	{
		bPendingLoginCreds = false;
		PendingLoginPassword = TEXT("");
	}
}

#endif

FDelegateHandle UUTLocalPlayer::RegisterPlayerOnlineStatusChangedDelegate(const FPlayerOnlineStatusChanged::FDelegate& NewDelegate)
{
	return PlayerOnlineStatusChanged.Add(NewDelegate);
}

void UUTLocalPlayer::RemovePlayerOnlineStatusChangedDelegate(FDelegateHandle DelegateHandle)
{
	PlayerOnlineStatusChanged.Remove(DelegateHandle);
}

FDelegateHandle UUTLocalPlayer::RegisterChatArchiveChangedDelegate(const FChatArchiveChanged::FDelegate& NewDelegate)
{
	return ChatArchiveChanged.Add(NewDelegate);
}

void UUTLocalPlayer::RemoveChatArchiveChangedDelegate(FDelegateHandle DelegateHandle)
{
	ChatArchiveChanged.Remove(DelegateHandle);
}


void UUTLocalPlayer::ShowAdminMessage(FString Message)
{
#if !UE_SERVER

	// Build the Toast to Show...

	TSharedPtr<SUTAdminMessageToast> Msg;
	SAssignNew(Msg, SUTAdminMessageToast)
		.PlayerOwner(this)
		.Lifetime(10)
		.ToastText(FText::FromString(Message));

	if (Msg.IsValid())
	{
		ToastList.Add(Msg);

		// Auto show if it's the first toast..
		if (ToastList.Num() == 1)
		{
			AddToastToViewport(ToastList[0]);
		}
	}
#endif

}

void UUTLocalPlayer::ShowToast(FText ToastText)
{
#if !UE_SERVER

	if (GetWorld()->GetNetMode() == ENetMode::NM_Client && bSuppressToastsInGame) return;

	// Build the Toast to Show...

	TSharedPtr<SUTToastBase> Toast;
	SAssignNew(Toast, SUTToastBase)
		.PlayerOwner(this)
		.ToastText(ToastText);

	if (Toast.IsValid())
	{
		ToastList.Add(Toast);

		// Auto show if it's the first toast..
		if (ToastList.Num() == 1)
		{
			AddToastToViewport(ToastList[0]);
		}
	}
#endif
}

#if !UE_SERVER
void UUTLocalPlayer::AddToastToViewport(TSharedPtr<SUTToastBase> ToastToDisplay)
{
	GEngine->GameViewport->AddViewportWidgetContent( SNew(SWeakWidget).PossiblyNullContent(ToastToDisplay.ToSharedRef()),10000);
}

void UUTLocalPlayer::ToastCompleted()
{
	GEngine->GameViewport->RemoveViewportWidgetContent(ToastList[0].ToSharedRef());
	ToastList.RemoveAt(0,1);

	if (ToastList.Num() > 0)
	{
		AddToastToViewport(ToastList[0]);
	}
}

#endif

FString UUTLocalPlayer::GetProfileFilename()
{
	if (IsLoggedIn())
	{
		return TEXT("user_profile_1");
	}

	return TEXT("local_user_profile");
}

FString UUTLocalPlayer::GetProgressionFilename()
{
	return TEXT("user_progression_1");
}


/*
 *	If the player is currently logged in, trigger a load of their profile settings from the MCP.  
 */
void UUTLocalPlayer::LoadProfileSettings()
{
	if (GetWorld()->IsPlayInEditor())
	{
		return;
	}

	if (IsLoggedIn())
	{
		TSharedPtr<const FUniqueNetId> UserID = OnlineIdentityInterface->GetUniquePlayerId(GetControllerId());
		if (UserID.IsValid())
		{
			if (OnlineUserCloudInterface.IsValid())
			{
				OnlineUserCloudInterface->ReadUserFile(*UserID, GetProfileFilename());
			}
		}
	}
}

void UUTLocalPlayer::ClearProfileSettings()
{
#if !UE_SERVER
	if (IsLoggedIn())
	{
		ShowMessage(NSLOCTEXT("UUTLocalPlayer","ClearCloudWarnTitle","!!! WARNING !!!"), NSLOCTEXT("UUTLocalPlayer","ClearCloudWarnMessage","You are about to clear all of your settings and profile data in the cloud as well as clear your active game and input ini files locally. The game will then exit and wait for a restart!\n\nAre you sure you want to do this??"), UTDIALOG_BUTTON_YES + UTDIALOG_BUTTON_NO, FDialogResultDelegate::CreateUObject(this, &UUTLocalPlayer::ClearProfileWarnResults));
	}
#endif
}

void UUTLocalPlayer::ClearProfileWarnResults(TSharedPtr<SCompoundWidget> Widget, uint16 ButtonID)
{
	if (IsLoggedIn() && ButtonID == UTDIALOG_BUTTON_YES)
	{
		TSharedPtr<const FUniqueNetId> UserID = OnlineIdentityInterface->GetUniquePlayerId(GetControllerId());
		if (OnlineUserCloudInterface.IsValid() && UserID.IsValid())
		{
			OnlineUserCloudInterface->DeleteUserFile(*UserID, GetProfileFilename(), true, true);
		}
	}
}

void UUTLocalPlayer::LoadProgression()
{
	if (IsLoggedIn())
	{
		TSharedPtr<const FUniqueNetId> UserID = OnlineIdentityInterface->GetUniquePlayerId(GetControllerId());
		if (UserID.IsValid())
		{
			if (OnlineUserCloudInterface.IsValid())
			{
				OnlineUserCloudInterface->ReadUserFile(*UserID, GetProgressionFilename());
			}
		}
	}
}

void UUTLocalPlayer::OnDeleteUserFileComplete(bool bWasSuccessful, const FUniqueNetId& InUserId, const FString& FileName)
{
#if !UE_SERVER
	// We successfully cleared the cloud, rewrite everything
	if (bWasSuccessful && FileName == GetProfileFilename())
	{
		FString PlaformName = FPlatformProperties::PlatformName();
		FString Filename = FString::Printf(TEXT("%s%s/Input.ini"), *FPaths::GeneratedConfigDir(), *PlaformName);
		if (FPlatformFileManager::Get().GetPlatformFile().DeleteFile(*Filename))
		{
			UE_LOG(UT,Log,TEXT("Failed to delete Input.ini"));
		}

		Filename = FString::Printf(TEXT("%s%s/Game.ini"), *FPaths::GeneratedConfigDir(), *PlaformName);
		if (FPlatformFileManager::Get().GetPlatformFile().DeleteFile(*Filename))
		{
			UE_LOG(UT,Log,TEXT("Failed to delete Game.ini"));
		}

		Filename = FString::Printf(TEXT("%s%s/GameUserSettings.ini"), *FPaths::GeneratedConfigDir(), *PlaformName);
		if (FPlatformFileManager::Get().GetPlatformFile().DeleteFile(*Filename))
		{
			UE_LOG(UT,Log,TEXT("Failed to delete GameUserSettings.ini"));
		}


		FPlatformMisc::RequestExit( 0 );
	}
#endif
}


void UUTLocalPlayer::OnReadUserFileComplete(bool bWasSuccessful, const FUniqueNetId& InUserId, const FString& FileName)
{
	if (FileName == GetProfileFilename())
	{
		// We were attempting to read the profile.. see if it was successful.	

		if (bWasSuccessful && OnlineUserCloudInterface.IsValid())	
		{
			// After the profile is loaded it's safe to load progression so that we have
			// the fallback to the old profile based progression info
			LoadProgression();

			// Create the current profile.
			if (CurrentProfileSettings == NULL)
			{
				CurrentProfileSettings = NewObject<UUTProfileSettings>(GetTransientPackage(),UUTProfileSettings::StaticClass());
			}

			TArray<uint8> FileContents;
			OnlineUserCloudInterface->GetFileContents(InUserId, FileName, FileContents);
			
			// Serialize the object
			FMemoryReader MemoryReader(FileContents, true);
			FObjectAndNameAsStringProxyArchive Ar(MemoryReader, true);
			
			// FObjectAndNameAsStringProxyArchive does not have versioning, but we need it
			// In 4.12, Serialization has been modified and we need the FArchive to use the right serialization path
			uint32 PossibleMagicNumber;
			Ar << PossibleMagicNumber;
			if (CloudProfileMagicNumberVersion1 == PossibleMagicNumber)
			{
				int32 CloudProfileUE4Ver;
				Ar << CloudProfileUE4Ver;
				Ar.SetUE4Ver(CloudProfileUE4Ver);
			}
			else
			{
				// Very likely this is from an unversioned cloud profile, set it to the last released serialization version
				Ar.SetUE4Ver(CloudProfileUE4VerForUnversionedProfile);
				// Rewind to the beginning as the magic number was not in the archive
				Ar.Seek(0);
			}

			CurrentProfileSettings->Serialize(Ar);
			CurrentProfileSettings->VersionFixup();

			FString CmdLineSwitch = TEXT("");
			bool bClearProfile = FParse::Param(FCommandLine::Get(), TEXT("ClearProfile"));

			// Check to make sure the profile settings are valid and that we aren't forcing them
			// to be cleared.  If all is OK, then apply these settings.
			if (CurrentProfileSettings->SettingsRevisionNum >= VALID_PROFILESETTINGS_VERSION && !bClearProfile)
			{
				CurrentProfileSettings->ApplyAllSettings(this);

				// It's possible for the MCP data to get here before the profile, so we havbe to check for daily challenges 
				// in two places.
				UUTGameEngine* GameEngine = Cast<UUTGameEngine>(GEngine);
				if (GameEngine && GameEngine->GetChallengeManager().IsValid())
				{
					if ( GameEngine->GetChallengeManager()->CheckDailyChallenge(CurrentProgression) )
					{
						SaveProfileSettings();
						SaveProgression();
					}
				}

				EpicFlagCheck();
				return;
			}
			else
			{
				CurrentProfileSettings->ClearWeaponPriorities();
			}

		}
		else if (CurrentProfileSettings == NULL) // Create a new profile settings object
		{
			CurrentProfileSettings = NewObject<UUTProfileSettings>(GetTransientPackage(),UUTProfileSettings::StaticClass());

			// Set some profile defaults, should be a function call if this gets any larger
			CurrentProfileSettings->TauntPath = GetDefaultURLOption(TEXT("Taunt"));
			CurrentProfileSettings->Taunt2Path = GetDefaultURLOption(TEXT("Taunt2"));

			// Attempt to load the progression anyway since the user might have reset their profile.
			LoadProgression();


		}

		// Set the ranks/etc so the player card is right.
		AUTBasePlayerController* UTBasePlayer = Cast<AUTBasePlayerController>(PlayerController);
		if (UTBasePlayer != NULL)
		{
			UTBasePlayer->ServerReceiveRank(GetRankDuel(), GetRankCTF(), GetRankTDM(), GetRankDM(), GetRankShowdown(), GetTotalChallengeStars(), DuelEloMatches(), CTFEloMatches(), TDMEloMatches(), DMEloMatches(), ShowdownEloMatches());
			// TODO: should this be in BasePlayerController?
			AUTPlayerController* UTPC = Cast<AUTPlayerController>(UTBasePlayer);
			if (UTPC != NULL)
			{
				UTPC->ServerReceiveCountryFlag(GetCountryFlag());
			}
			else
			{
				AUTPlayerState* UTPS = Cast<AUTPlayerState>(UTBasePlayer->PlayerState);
				if (UTPS != NULL)
				{
					UTPS->CountryFlag = GetCountryFlag();
				}
			}
		}

		EpicFlagCheck();

		PlayerNickname = GetAccountDisplayName().ToString();
		SaveConfig();
		SaveProfileSettings();

#if !UE_SERVER
		FText WelcomeMessage = FText::Format(NSLOCTEXT("UTLocalPlayer","Welcome","Your player name is set to '{0}'. Would you like to change it?"), GetAccountDisplayName());
		ShowMessage(NSLOCTEXT("UTLocalPlayer", "WelcomeTitle", "Welcome to Unreal Tournament"), WelcomeMessage, UTDIALOG_BUTTON_YES + UTDIALOG_BUTTON_NO, FDialogResultDelegate::CreateUObject(this, &UUTLocalPlayer::WelcomeDialogResult),FVector2D(0.35,0.25));
		// We couldn't load our profile or it was invalid or we choose to clear it so save it out.
#endif
	}
	else if (FileName == GetProgressionFilename())
	{
		if (bWasSuccessful)
		{
			// Create the current profile.
			if (CurrentProgression == NULL)
			{
				CurrentProgression = NewObject<UUTProgressionStorage>(GetTransientPackage(),UUTProgressionStorage::StaticClass());
			}

			TArray<uint8> FileContents;
			OnlineUserCloudInterface->GetFileContents(InUserId, FileName, FileContents);
			
			// Serialize the object
			FMemoryReader MemoryReader(FileContents, true);
			FObjectAndNameAsStringProxyArchive Ar(MemoryReader, true);
			
			// FObjectAndNameAsStringProxyArchive does not have versioning, but we need it
			// In 4.12, Serialization has been modified and we need the FArchive to use the right serialization path
			uint32 PossibleMagicNumber;
			Ar << PossibleMagicNumber;
			if (CloudProfileMagicNumberVersion1 == PossibleMagicNumber)
			{
				int32 CloudProfileUE4Ver;
				Ar << CloudProfileUE4Ver;
				Ar.SetUE4Ver(CloudProfileUE4Ver);
			}
			else
			{
				// Very likely this is from an unversioned cloud file, set it to the last released serialization version
				Ar.SetUE4Ver(CloudProfileUE4VerForUnversionedProfile);
				// Rewind to the beginning as the magic number was not in the archive
				Ar.Seek(0);
			}

			CurrentProgression->Serialize(Ar);
			CurrentProgression->VersionFixup();

			// set PlayerState progressionv variables if in main menu/single player
			if (PlayerController != NULL)
			{
				AUTPlayerState* PS = Cast<AUTPlayerState>(PlayerController->PlayerState);
				if (PS != NULL)
				{
					PS->TotalChallengeStars = CurrentProgression->TotalChallengeStars;
				}
			}
		}
		else if (CurrentProfileSettings)
		{
			CurrentProgression = NewObject<UUTProgressionStorage>(GetTransientPackage(),UUTProgressionStorage::StaticClass());
			CurrentProgression->LoadFromProfile(CurrentProfileSettings);
			SaveProgression();
		}

		bProgressionReadFromCloud = true;
		CheckReportELOandStarsToServer();
	}
}

// Only send ELO and stars to the server once all the server responses are complete
void UUTLocalPlayer::CheckReportELOandStarsToServer()
{
	ELOReportCount++;

	if (ELOReportCount >= 5 && bProgressionReadFromCloud)
	{
		// Set the ranks/etc so the player card is right.
		AUTBasePlayerController* UTBasePlayer = Cast<AUTBasePlayerController>(PlayerController);
		if (UTBasePlayer) UTBasePlayer->ServerReceiveRank(GetRankDuel(), GetRankCTF(), GetRankTDM(), GetRankDM(), GetRankShowdown(), GetTotalChallengeStars(), DuelEloMatches(), CTFEloMatches(), TDMEloMatches(), DMEloMatches(), ShowdownEloMatches());
	}
}

bool UUTLocalPlayer::OwnsItemFor(const FString& Path, int32 VariantId) const
{
#if WITH_PROFILE
	if (IsLoggedIn() && GetMcpProfileManager()->GetMcpProfileAs<UUtMcpProfile>(EUtMcpProfile::Profile) != NULL)
	{
		TArray<UUTProfileItem*> ItemList;
		GetMcpProfileManager()->GetMcpProfileAs<UUtMcpProfile>(EUtMcpProfile::Profile)->GetItemsOfType<UUTProfileItem>(ItemList);
		for (UUTProfileItem* Item : ItemList)
		{
			if (Item != NULL && Item->Grants(Path, VariantId))
			{
				return true;
			}
		}
	}
#endif
	return false;
}

#if !UE_SERVER
void UUTLocalPlayer::WelcomeDialogResult(TSharedPtr<SCompoundWidget> Widget, uint16 ButtonID)
{
	if (ButtonID == UTDIALOG_BUTTON_YES)
	{
		OpenDialog(SNew(SUTPlayerSettingsDialog).PlayerOwner(this));			
	}
}

#endif

void UUTLocalPlayer::SaveProfileSettings()
{
	if ( CurrentProfileSettings != NULL && IsLoggedIn() )
	{
		CurrentProfileSettings->GatherAllSettings(this);
		CurrentProfileSettings->SettingsRevisionNum = CURRENT_PROFILESETTINGS_VERSION;

		CurrentProfileSettings->bNeedProfileWriteOnLevelChange = false;

		// Build a blob of the profile contents
		TArray<uint8> FileContents;
		FMemoryWriter MemoryWriter(FileContents, true);
		FObjectAndNameAsStringProxyArchive Ar(MemoryWriter, false);
		Ar << CloudProfileMagicNumberVersion1;
		int32 UE4Ver = Ar.UE4Ver();
		Ar << UE4Ver;
		CurrentProfileSettings->Serialize(Ar);

		if (FApp::GetCurrentTime() - LastProfileCloudWriteTime < ProfileCloudWriteCooldownTime)
		{
			GetWorld()->GetTimerManager().SetTimer(ProfileWriteTimerHandle, this, &UUTLocalPlayer::SaveProfileSettings, ProfileCloudWriteCooldownTime - (FApp::GetCurrentTime() - LastProfileCloudWriteTime), false);
		}
		else
		{
			// Save the blob to the cloud
			TSharedPtr<const FUniqueNetId> UserID = OnlineIdentityInterface->GetUniquePlayerId(GetControllerId());
			if (OnlineUserCloudInterface.IsValid() && UserID.IsValid())
			{
				LastProfileCloudWriteTime = FApp::GetCurrentTime();
				OnlineUserCloudInterface->WriteUserFile(*UserID, GetProfileFilename(), FileContents);
			}
		}
	}
}

void UUTLocalPlayer::SaveProgression()
{
	if ( CurrentProgression != NULL && IsLoggedIn() )
	{
		CurrentProgression->Updated();

		// Build a blob of the profile contents
		TArray<uint8> FileContents;
		FMemoryWriter MemoryWriter(FileContents, true);
		FObjectAndNameAsStringProxyArchive Ar(MemoryWriter, false);
		Ar << CloudProfileMagicNumberVersion1;
		int32 UE4Ver = Ar.UE4Ver();
		Ar << UE4Ver;
		CurrentProgression->Serialize(Ar);

		// Save the blob to the cloud
		TSharedPtr<const FUniqueNetId> UserID = OnlineIdentityInterface->GetUniquePlayerId(GetControllerId());
		if (OnlineUserCloudInterface.IsValid() && UserID.IsValid())
		{
			LastProfileCloudWriteTime = FApp::GetCurrentTime();
			OnlineUserCloudInterface->WriteUserFile(*UserID, GetProgressionFilename(), FileContents);
		}
	}
}


void UUTLocalPlayer::OnWriteUserFileComplete(bool bWasSuccessful, const FUniqueNetId& InUserId, const FString& FileName)
{
	// Make sure this was our filename
	if (FileName == GetProfileFilename())
	{
		if (bWasSuccessful)
		{
			LastProfileCloudWriteTime = FApp::GetCurrentTime();
			FText Saved = NSLOCTEXT("MCP", "ProfileSaved", "Profile Saved");
			ShowToast(Saved);
		}
		else
		{
			LastProfileCloudWriteTime = GetClass()->GetDefaultObject<UUTLocalPlayer>()->LastProfileCloudWriteTime;
	#if !UE_SERVER
			// Should give a warning here if it fails.
			ShowMessage(NSLOCTEXT("MCPMessages", "ProfileSaveErrorTitle", "An error has occured"), NSLOCTEXT("MCPMessages", "ProfileSaveErrorText", "UT could not save your profile with the MCP.  Your settings may be lost."), UTDIALOG_BUTTON_OK, NULL);
	#endif
		}
	}
	else if (FileName == GetProgressionFilename())
	{
		if (bWasSuccessful)
		{
			FText Saved = NSLOCTEXT("MCP", "ProgressionSaved", "Progression Saved");
			ShowToast(Saved);
		}
		else
		{
	#if !UE_SERVER
			// Should give a warning here if it fails.
			ShowMessage(NSLOCTEXT("MCPMessages", "ProgressionSaveErrorTitle", "An error has occured"), NSLOCTEXT("MCPMessages", "ProgressionSaveErrorText", "UT could not save your progression with the MCP.  Your progress may be lost."), UTDIALOG_BUTTON_OK, NULL);
	#endif
		}
	}
}

void UUTLocalPlayer::SetNickname(FString NewName)
{
	PlayerNickname = NewName;
	SaveConfig();
	if (PlayerController) 
	{
		PlayerController->ServerChangeName(NewName);
	}
}

void UUTLocalPlayer::SaveChat(FName Type, FString Sender, FString Message, FLinearColor Color, bool bMyChat, uint8 TeamNum)
{
	TSharedPtr<FStoredChatMessage> ArchiveMessage = FStoredChatMessage::Make(Type, Sender, Message, Color, int32(GetWorld()->GetRealTimeSeconds()), bMyChat, TeamNum );
	ChatArchive.Add( ArchiveMessage );
	ChatArchiveChanged.Broadcast(this, ArchiveMessage );
}

FName UUTLocalPlayer::TeamStyleRef(FName InName)
{
	if (PlayerController)
	{
		AUTPlayerController* PC = Cast<AUTPlayerController>(PlayerController);
		if (PC && PC->GetTeamNum() == 0)
		{
			return FName( *(TEXT("Red.") + InName.ToString()));
		}
	}

	return FName( *(TEXT("Blue.") + InName.ToString()));
}

void UUTLocalPlayer::ReadELOFromBackend()
{
	ELOReportCount = 0;

	// get MCP Utils
	UUTMcpUtils* McpUtils = UUTMcpUtils::Get(GetWorld(), OnlineIdentityInterface->GetUniquePlayerId(GetControllerId()));
	if (McpUtils == nullptr)
	{
		UE_LOG(UT, Warning, TEXT("Unable to load McpUtils. Will not be able to read ELO from MCP"));
		return;
	}	

	McpUtils->GetAccountElo(NAME_SkillRating.ToString(), [this](const FOnlineError& Result, const FAccountElo& Response)
	{
		if (!Result.bSucceeded)
		{
			// best we can do is log an error
			UE_LOG(UT, Warning, TEXT("Failed to read ELO from the server. (%d) %s %s"), Result.HttpResult, *Result.ErrorCode, *Result.ErrorMessage.ToString());
		}
		else
		{
			UE_LOG(UT, Display, TEXT("Duel ELO read %d, %d matches"), Response.Rating, Response.NumGamesPlayed);
			DUEL_ELO = Response.Rating;
			DuelMatchesPlayed = Response.NumGamesPlayed;
		}
		CheckReportELOandStarsToServer();
	});

	McpUtils->GetAccountElo(NAME_TDMSkillRating.ToString(), [this](const FOnlineError& Result, const FAccountElo& Response) 
	{
		if (!Result.bSucceeded)
		{
			// best we can do is log an error
			UE_LOG(UT, Warning, TEXT("Failed to read ELO from the server. (%d) %s %s"), Result.HttpResult, *Result.ErrorCode, *Result.ErrorMessage.ToString());
		}
		else
		{
			UE_LOG(UT, Display, TEXT("TDM ELO read %d, %d matches"), Response.Rating, Response.NumGamesPlayed);
			TDM_ELO = Response.Rating;
			TDMMatchesPlayed = Response.NumGamesPlayed;
		}
		CheckReportELOandStarsToServer();
	});

	McpUtils->GetAccountElo(NAME_DMSkillRating.ToString(), [this](const FOnlineError& Result, const FAccountElo& Response)
	{
		if (!Result.bSucceeded)
		{
			// best we can do is log an error
			UE_LOG(UT, Warning, TEXT("Failed to read ELO from the server. (%d) %s %s"), Result.HttpResult, *Result.ErrorCode, *Result.ErrorMessage.ToString());
		}
		else
		{
			UE_LOG(UT, Display, TEXT("DM ELO read %d, %d matches"), Response.Rating, Response.NumGamesPlayed);
			FFA_ELO = Response.Rating;
			FFAMatchesPlayed = Response.NumGamesPlayed;
		}
		CheckReportELOandStarsToServer();
	});

	McpUtils->GetAccountElo(NAME_CTFSkillRating.ToString(), [this](const FOnlineError& Result, const FAccountElo& Response)
	{
		if (!Result.bSucceeded)
		{
			// best we can do is log an error
			UE_LOG(UT, Warning, TEXT("Failed to read ELO from the server. (%d) %s %s"), Result.HttpResult, *Result.ErrorCode, *Result.ErrorMessage.ToString());
		}
		else
		{
			UE_LOG(UT, Display, TEXT("CTF ELO read %d, %d matches"), Response.Rating, Response.NumGamesPlayed);
			CTF_ELO = Response.Rating;
			CTFMatchesPlayed = Response.NumGamesPlayed;
		}
		CheckReportELOandStarsToServer();
	});

	McpUtils->GetAccountElo(NAME_ShowdownSkillRating.ToString(), [this](const FOnlineError& Result, const FAccountElo& Response)
	{
		if (!Result.bSucceeded)
		{
			// best we can do is log an error
			UE_LOG(UT, Warning, TEXT("Failed to read ELO from the server. (%d) %s %s"), Result.HttpResult, *Result.ErrorCode, *Result.ErrorMessage.ToString());
		}
		else
		{
			UE_LOG(UT, Display, TEXT("Showdown ELO read %d, %d matches"), Response.Rating, Response.NumGamesPlayed);
			Showdown_ELO = Response.Rating;
			ShowdownMatchesPlayed = Response.NumGamesPlayed;
		}
		CheckReportELOandStarsToServer();
	});
}

int32 UUTLocalPlayer::GetBaseELORank()
{
	// let UTGame do it if have PlayerState
	AUTPlayerController* PC = Cast<AUTPlayerController>(PlayerController);
	if (PC && PC->UTPlayerState)
	{
		AUTGameMode* UTGame = AUTGameMode::StaticClass()->GetDefaultObject<AUTGameMode>();
		if (UTGame)
		{
			return UTGame->GetEloFor(PC->UTPlayerState);
		}
	}
	return 1500;
}

void UUTLocalPlayer::GetStarsFromXP(int32 XPValue, int32& Star)
{
	Star = (XPValue > 0) ? int32(FMath::Clamp<float>((XPValue / 10.f), 0.f, 5.f)) : -1;
}

int32 UUTLocalPlayer::GetHatVariant() const
{
	return (CurrentProfileSettings != NULL) ? CurrentProfileSettings->HatVariant : FCString::Atoi(*GetDefaultURLOption(TEXT("HatVar")));
}

void UUTLocalPlayer::SetHatVariant(int32 NewVariant)
{
	if (CurrentProfileSettings != NULL)
	{
		CurrentProfileSettings->HatVariant = NewVariant;
	}
	SetDefaultURLOption(TEXT("HatVar"), FString::FromInt(NewVariant));
	if (PlayerController != NULL)
	{
		AUTPlayerState* PS = Cast<AUTPlayerState>(PlayerController->PlayerState);
		if (PS != NULL)
		{
			PS->ServerReceiveHatVariant(NewVariant);
		}
	}
}

int32 UUTLocalPlayer::GetEyewearVariant() const
{
	return (CurrentProfileSettings != NULL) ? CurrentProfileSettings->EyewearVariant : FCString::Atoi(*GetDefaultURLOption(TEXT("EyewearVar")));
}

void UUTLocalPlayer::SetEyewearVariant(int32 NewVariant)
{
	if (CurrentProfileSettings != NULL)
	{
		CurrentProfileSettings->EyewearVariant = NewVariant;
	}
	SetDefaultURLOption(TEXT("EyewearVar"), FString::FromInt(NewVariant));
	if (PlayerController != NULL)
	{
		AUTPlayerState* PS = Cast<AUTPlayerState>(PlayerController->PlayerState);
		if (PS != NULL)
		{
			PS->ServerReceiveEyewearVariant(NewVariant);
		}
	}
}

FString UUTLocalPlayer::GetHatPath() const
{
	return (CurrentProfileSettings != NULL) ? CurrentProfileSettings->HatPath : GetDefaultURLOption(TEXT("Hat"));
}

void UUTLocalPlayer::SetHatPath(const FString& NewHatPath)
{
	if (CurrentProfileSettings != NULL)
	{
		CurrentProfileSettings->HatPath = NewHatPath;
	}
	SetDefaultURLOption(TEXT("Hat"), NewHatPath);
	if (PlayerController != NULL)
	{
		AUTPlayerState* PS = Cast<AUTPlayerState>(PlayerController->PlayerState);
		if (PS != NULL)
		{
			if (FUTAnalytics::IsAvailable())
			{
				TArray<FAnalyticsEventAttribute> ParamArray;
				ParamArray.Add(FAnalyticsEventAttribute(TEXT("PlayerID"), PS->UniqueId.ToString()));
				ParamArray.Add(FAnalyticsEventAttribute(TEXT("HatPath"), NewHatPath));
				FUTAnalytics::GetProvider().RecordEvent( TEXT("HatChanged"), ParamArray );
			}
			PS->ServerReceiveHatClass(NewHatPath);
		}
	}
}

FString UUTLocalPlayer::GetLeaderHatPath() const
{
	return (CurrentProfileSettings != NULL) ? CurrentProfileSettings->LeaderHatPath : GetDefaultURLOption(TEXT("LeaderHat"));
}

void UUTLocalPlayer::SetLeaderHatPath(const FString& NewLeaderHatPath)
{
	if (CurrentProfileSettings != NULL)
	{
		CurrentProfileSettings->LeaderHatPath = NewLeaderHatPath;
	}
	SetDefaultURLOption(TEXT("LeaderHat"), NewLeaderHatPath);
	if (PlayerController != NULL)
	{
		AUTPlayerState* PS = Cast<AUTPlayerState>(PlayerController->PlayerState);
		if (PS != NULL)
		{
			if (FUTAnalytics::IsAvailable())
			{
				TArray<FAnalyticsEventAttribute> ParamArray;
				ParamArray.Add(FAnalyticsEventAttribute(TEXT("PlayerID"), PS->UniqueId.ToString()));
				ParamArray.Add(FAnalyticsEventAttribute(TEXT("LeaderHatPath"), NewLeaderHatPath));
				FUTAnalytics::GetProvider().RecordEvent( TEXT("LeaderHatChanged"), ParamArray );
			}
			PS->ServerReceiveLeaderHatClass(NewLeaderHatPath);
		}
	}
}

FString UUTLocalPlayer::GetEyewearPath() const
{
	return (CurrentProfileSettings != NULL) ? CurrentProfileSettings->EyewearPath : GetDefaultURLOption(TEXT("Eyewear"));
}

void UUTLocalPlayer::SetEyewearPath(const FString& NewEyewearPath)
{
	if (CurrentProfileSettings != NULL)
	{
		CurrentProfileSettings->EyewearPath = NewEyewearPath;
	}
	SetDefaultURLOption(TEXT("Eyewear"), NewEyewearPath);
	if (PlayerController != NULL)
	{
		AUTPlayerState* PS = Cast<AUTPlayerState>(PlayerController->PlayerState);
		if (PS != NULL)
		{
			if (FUTAnalytics::IsAvailable())
			{
				TArray<FAnalyticsEventAttribute> ParamArray;
				ParamArray.Add(FAnalyticsEventAttribute(TEXT("PlayerID"), PS->UniqueId.ToString()));
				ParamArray.Add(FAnalyticsEventAttribute(TEXT("EyewearPath"), NewEyewearPath));
				FUTAnalytics::GetProvider().RecordEvent( TEXT("CharacterChanged"), ParamArray );
			}

			PS->ServerReceiveEyewearClass(NewEyewearPath);
		}
	}
}

FString UUTLocalPlayer::GetCharacterPath() const
{
	return (CurrentProfileSettings != NULL) ? CurrentProfileSettings->CharacterPath : GetDefaultURLOption(TEXT("Character"));
}

void UUTLocalPlayer::SetCharacterPath(const FString& NewCharacterPath)
{
	AUTPlayerState* PS = Cast<AUTPlayerState>((PlayerController != NULL) ? PlayerController->PlayerState : NULL);
	if (PS != NULL && FUTAnalytics::IsAvailable())
	{
		TArray<FAnalyticsEventAttribute> ParamArray;
		ParamArray.Add(FAnalyticsEventAttribute(TEXT("PlayerID"), PS->UniqueId.ToString()));
		ParamArray.Add(FAnalyticsEventAttribute(TEXT("CharacterPath"), NewCharacterPath));
		FUTAnalytics::GetProvider().RecordEvent( TEXT("CharacterChanged"), ParamArray );
	}

	if (CurrentProfileSettings != NULL)
	{
		CurrentProfileSettings->CharacterPath = NewCharacterPath;
	}
	SetDefaultURLOption(TEXT("Character"), NewCharacterPath);
	if (PS != NULL)
	{
		PS->ServerSetCharacter(NewCharacterPath);
	}
}

FString UUTLocalPlayer::GetTauntPath() const
{
	return (CurrentProfileSettings != NULL) ? CurrentProfileSettings->TauntPath : GetDefaultURLOption(TEXT("Taunt"));
}

void UUTLocalPlayer::SetTauntPath(const FString& NewTauntPath)
{
	if (CurrentProfileSettings != NULL)
	{
		CurrentProfileSettings->TauntPath = NewTauntPath;
	}
	SetDefaultURLOption(TEXT("Taunt"), NewTauntPath);
	if (PlayerController != NULL)
	{
		AUTPlayerState* PS = Cast<AUTPlayerState>(PlayerController->PlayerState);
		if (PS != NULL)
		{
			PS->ServerReceiveTauntClass(NewTauntPath);
		}
	}
}

FString UUTLocalPlayer::GetTaunt2Path() const
{
	return (CurrentProfileSettings != NULL) ? CurrentProfileSettings->Taunt2Path : GetDefaultURLOption(TEXT("Taunt2"));
}

void UUTLocalPlayer::SetTaunt2Path(const FString& NewTauntPath)
{
	if (CurrentProfileSettings != NULL)
	{
		CurrentProfileSettings->Taunt2Path = NewTauntPath;
	}
	SetDefaultURLOption(TEXT("Taunt2"), NewTauntPath);
	if (PlayerController != NULL)
	{
		AUTPlayerState* PS = Cast<AUTPlayerState>(PlayerController->PlayerState);
		if (PS != NULL)
		{
			PS->ServerReceiveTaunt2Class(NewTauntPath);
		}
	}
}

FString UUTLocalPlayer::GetDefaultURLOption(const TCHAR* Key) const
{
	FURL DefaultURL;
	DefaultURL.LoadURLConfig(TEXT("DefaultPlayer"), GGameIni);
	FString Op = DefaultURL.GetOption(Key, TEXT(""));
	FString Result;
	Op.Split(TEXT("="), NULL, &Result);
	return Result;
}

void UUTLocalPlayer::SetDefaultURLOption(const FString& Key, const FString& Value)
{
	FURL DefaultURL;
	DefaultURL.LoadURLConfig(TEXT("DefaultPlayer"), GGameIni);
	DefaultURL.AddOption(*FString::Printf(TEXT("%s=%s"), *Key, *Value));
	DefaultURL.SaveURLConfig(TEXT("DefaultPlayer"), *Key, GGameIni);
}

void UUTLocalPlayer::ClearDefaultURLOption(const FString& Key)
{
	FURL DefaultURL;
	DefaultURL.LoadURLConfig(TEXT("DefaultPlayer"), GGameIni);
	// doing it manually instead of RemoveOption() as the latter doesn't properly handle longer keys that have the same starting characters
	int32 KeyLen = Key.Len();
	for (int32 i = DefaultURL.Op.Num() - 1; i >= 0; i--)
	{
		if (DefaultURL.Op[i].Left(KeyLen) == Key)
		{
			const TCHAR* s = *DefaultURL.Op[i];
			if (s[KeyLen - 1] == '=' || s[KeyLen] == '=' || s[KeyLen] == '\0')
			{
				FConfigSection* Sec = GConfig->GetSectionPrivate(TEXT("DefaultPlayer"), 0, 0, GGameIni);
				if (Sec != NULL && Sec->Remove(*Key) > 0)
				{
					GConfig->Flush(0, GGameIni);
				}

				DefaultURL.Op.RemoveAt(i);
			}
		}
	}
}

#if !UE_SERVER
void UUTLocalPlayer::ShowContentLoadingMessage()
{
	if (!ContentLoadingMessage.IsValid())
	{
		SAssignNew(ContentLoadingMessage, SOverlay)
		+SOverlay::Slot()
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Fill)
		[
			SNew(SHorizontalBox)
			+SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.FillWidth(1.0)
			[
				SNew(SVerticalBox)
				+SVerticalBox::Slot()
				.HAlign(HAlign_Center)
				.AutoHeight()
				[
					SNew(SBox)
					.WidthOverride(700)
					.HeightOverride(64)
					[
						SNew(SOverlay)
						+SOverlay::Slot()
						[
							SNew(SVerticalBox)
							+SVerticalBox::Slot()
							.VAlign(VAlign_Fill)
							.HAlign(HAlign_Fill)
							[
								SNew(SImage)
								.Image(SUWindowsStyle::Get().GetBrush("UWindows.Standard.Dialog.Background"))
							]
						]
						+SOverlay::Slot()
						[
							SNew(SVerticalBox)
							+SVerticalBox::Slot()
							.VAlign(VAlign_Center)
							.HAlign(HAlign_Center)
							[
								SNew(STextBlock)
								.Text(NSLOCTEXT("MenuMessages","InitMenu","Initializing Menus"))
								.TextStyle(SUWindowsStyle::Get(), "UT.TopMenu.Button.TextStyle")
							]
						]
					]
				]
			]
		];
	}

	if (ContentLoadingMessage.IsValid())
	{
		GEngine->GameViewport->AddViewportWidgetContent(ContentLoadingMessage.ToSharedRef(), 255);
	}
}

void UUTLocalPlayer::HideContentLoadingMessage()
{
	if (ContentLoadingMessage.IsValid())
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(ContentLoadingMessage.ToSharedRef());			
		ContentLoadingMessage.Reset();
	}
}

TSharedPtr<SUTFriendsPopupWindow> UUTLocalPlayer::GetFriendsPopup()
{
	if (!FriendsMenu.IsValid())
	{
		SAssignNew(FriendsMenu, SUTFriendsPopupWindow)
			.PlayerOwner(this);
	}
	return FriendsMenu;
}

void UUTLocalPlayer::SetShowingFriendsPopup(bool bShowing)
{
	bShowingFriendsMenu = bShowing;
}
#endif

void UUTLocalPlayer::ReturnToMainMenu()
{
	HideMenu();

#if !UE_SERVER
	// Under certain situations (when we fail to load a replay immediately after starting watching it), 
	//	the replay menu will show up at the last second, and nothing will close it.
	// This is to make absolutely sure the replay menu doesn't persist into the main menu
	if (ReplayWindow.IsValid())
	{
		CloseReplayWindow();
	}
#endif

	if ( GetWorld() != nullptr )
	{
		Exec( GetWorld(), TEXT( "disconnect" ), *GLog );
	}
	else
	{
		// If we don't have a world, we likely failed to load one during replays
		// Recover by loading default map
		FURL DefaultURL;
		DefaultURL.LoadURLConfig( TEXT( "DefaultPlayer" ), GGameIni );

		FURL URL( &DefaultURL, TEXT( "" ), TRAVEL_Partial );

		FString Error;

		GEngine->Browse( *GetGameInstance()->GetWorldContext(), URL, Error );
	}
}

void UUTLocalPlayer::InvalidateLastSession()
{
	// Reset the last session so we don't have anything to join.
	LastSession.Session.OwningUserId.Reset();
	LastSession.Session.SessionInfo.Reset();
}

bool UUTLocalPlayer::JoinSession(const FOnlineSessionSearchResult& SearchResult, bool bSpectate, int32 DesiredTeam, FString InstanceId)
{
	UE_LOG(UT,Log, TEXT("##########################"));
	UE_LOG(UT,Log, TEXT("Joining a New Session"));
	UE_LOG(UT,Log, TEXT("##########################"));

	PendingInstanceID = InstanceId;
	bWantsToConnectAsSpectator = bSpectate;
	ConnectDesiredTeam = DesiredTeam;
	bCancelJoinSession = false;
	FUniqueNetIdRepl UniqueId = OnlineIdentityInterface->GetUniquePlayerId(0);
	if (!UniqueId.IsValid())
	{
		return false;
	}
	else
	{
		PendingSession = SearchResult;
		if (OnlineSessionInterface->IsPlayerInSession(GameSessionName, *UniqueId))
		{
			UE_LOG(UT, Log, TEXT("--- Already in a Session -- Deferring while I clean it up"));
			bDelayedJoinSession = true;
			LeaveSession();
		}
		else
		{
			SearchResult.Session.SessionSettings.Get(SETTING_TRUSTLEVEL, CurrentSessionTrustLevel);
			OnlineSessionInterface->JoinSession(0, GameSessionName, SearchResult);
		}
		return true;
	}
}

void UUTLocalPlayer::JoinPendingSession()
{
	if (bDelayedJoinSession)
	{
		bDelayedJoinSession = false;
		PendingSession.Session.SessionSettings.Get(SETTING_TRUSTLEVEL, CurrentSessionTrustLevel);
		OnlineSessionInterface->JoinSession(0, GameSessionName, PendingSession);
	}
}

void UUTLocalPlayer::CancelJoinSession()
{
	// There currently isn't a way to cancel a join session call.  So we just flag it as we are not joining and ignore any successful JoinSessionComplete calls

	bCancelJoinSession = true;
#if !UE_SERVER
	if (ServerBrowserWidget.IsValid())
	{
		ServerBrowserWidget->SetBrowserState(EBrowserState::BrowserIdle);
	}
#endif
}

void UUTLocalPlayer::OnJoinSessionComplete(FName SessionName, EOnJoinSessionCompleteResult::Type Result)
{
	bDelayedJoinSession = false;

#if !UE_SERVER
	if (ServerBrowserWidget.IsValid())
	{
		ServerBrowserWidget->SetBrowserState(EBrowserState::BrowserIdle);
	}
#endif

	UE_LOG(UT,Log, TEXT("----------- [OnJoinSessionComplete %i"), (Result == EOnJoinSessionCompleteResult::Success));

	// If we are trying to be crammed in to an existing session, we can just exit.
	if (bAttemptingForceJoin)
	{
		bCancelJoinSession = false;
		bAttemptingForceJoin = false;
		return;
	}

	ChatArchive.Empty();

	// If we succeed, nothing else needs to be done.
	if (Result == EOnJoinSessionCompleteResult::Success)
	{
		if (bCancelJoinSession)
		{
			InvalidateLastSession();
			bCancelJoinSession = false;
			return;
		}

		// Cache the last session.
		LastSession = PendingSession;

		FString ConnectionString;
		if ( OnlineSessionInterface->GetResolvedConnectString(SessionName, ConnectionString) )
		{
			int32 Index = 0;
			FString HostAddress = ConnectionString.FindChar(':',Index) ? ConnectionString.Left(Index) : ConnectionString;
			FString Password = bWantsToConnectAsSpectator ? "?specpassword=" : "?password=";
			Password = Password + RetrievePassword(HostAddress, bWantsToConnectAsSpectator);
			ConnectionString += Password;

			if (PendingFriendInviteFriendId != TEXT(""))
			{
				ConnectionString += FString::Printf(TEXT("?Friend=%s"), *PendingFriendInviteFriendId);
				PendingFriendInviteFriendId = TEXT("");
			}

			ConnectionString += FString::Printf(TEXT("?Rank=%i"), GetBaseELORank());

			ConnectionString += FString::Printf(TEXT("?SpectatorOnly=%i"), bWantsToConnectAsSpectator ? 1 : 0);

			if (ConnectDesiredTeam >= 0)
			{
				ConnectionString += FString::Printf(TEXT("?Team=%i"), ConnectDesiredTeam);
			}

			if (!PendingInstanceID.IsEmpty())
			{
				ConnectionString += FString::Printf(TEXT("?Session=%s"), *PendingInstanceID);
				PendingInstanceID.Empty();
			}

			FWorldContext &Context = GEngine->GetWorldContextFromWorldChecked(GetWorld());
			Context.LastURL.RemoveOption(TEXT("QuickMatch"));
			Context.LastURL.RemoveOption(TEXT("Friend"));
			
			PlayerController->ClientTravel(ConnectionString, ETravelType::TRAVEL_Partial,false);

			bWantsToConnectAsSpectator = false;
			return;
		}
	}

	bCancelJoinSession = false;

	// Any failures, return to the main menu.
	bWantsToConnectAsSpectator = false;

	if (Result == EOnJoinSessionCompleteResult::AlreadyInSession)
	{
		MessageBox(NSLOCTEXT("MCPMessages", "OnlineError", "Online Error"), NSLOCTEXT("MCPMessages", "AlreadyInSession", "You are already in a session and can't join another."));
	}
	if (Result == EOnJoinSessionCompleteResult::SessionIsFull)
	{
		MessageBox(NSLOCTEXT("MCPMessages", "OnlineError", "Online Error"), NSLOCTEXT("MCPMessages", "SessionFull", "The session you are attempting to join is full."));
	}

	CloseConnectingDialog();
#if !UE_SERVER
	if (GetWorld()->bIsDefaultLevel && !DesktopSlateWidget.IsValid())
	{
		ReturnToMainMenu();
	}
#endif
}

void UUTLocalPlayer::LeaveSession()
{
	if (OnlineIdentityInterface.IsValid())
	{
		TSharedPtr<const FUniqueNetId> UserId = OnlineIdentityInterface->GetUniquePlayerId(0);
		if (UserId.IsValid() && OnlineSessionInterface.IsValid() && OnlineSessionInterface->IsPlayerInSession(GameSessionName, *UserId))
		{
			OnEndSessionCompleteDelegate = OnlineSessionInterface->AddOnEndSessionCompleteDelegate_Handle(FOnEndSessionCompleteDelegate::CreateUObject(this, &UUTLocalPlayer::OnEndSessionComplete));
			OnlineSessionInterface->EndSession(GameSessionName);
		}
		else if (bPendingLoginCreds)
		{
			Logout();
		}
	}
}

void UUTLocalPlayer::OnEndSessionComplete(FName SessionName, bool bWasSuccessful)
{
	OnlineSessionInterface->ClearOnEndSessionCompleteDelegate_Handle(OnEndSessionCompleteDelegate);
	OnDestroySessionCompleteDelegate = OnlineSessionInterface->AddOnDestroySessionCompleteDelegate_Handle(FOnDestroySessionCompleteDelegate::CreateUObject(this, &UUTLocalPlayer::OnDestroySessionComplete));
	OnlineSessionInterface->DestroySession(GameSessionName);
}

void UUTLocalPlayer::OnDestroySessionComplete(FName SessionName, bool bWasSuccessful)
{
	UE_LOG(UT,Warning, TEXT("----------- [OnDestroySessionComplete %i"), bDelayedJoinSession);
	
	OnlineSessionInterface->ClearOnDestroySessionCompleteDelegate_Handle(OnDestroySessionCompleteDelegate);

	if (bPendingLoginCreds)
	{
		Logout();
	}
	else
	{
		JoinPendingSession();
	}
}

void UUTLocalPlayer::UpdatePresence(FString NewPresenceString, bool bAllowInvites, bool bAllowJoinInProgress, bool bAllowJoinViaPresence, bool bAllowJoinViaPresenceFriendsOnly)
{
	// XMPP code isn't working correctly on linux
#if PLATFORM_LINUX
	return;
#endif

	if (OnlineIdentityInterface.IsValid() && OnlineSessionInterface.IsValid() && OnlinePresenceInterface.IsValid())
	{
		TSharedPtr<const FUniqueNetId> UserId = OnlineIdentityInterface->GetUniquePlayerId(GetControllerId());
		if (UserId.IsValid())
		{
			FOnlineSessionSettings* GameSettings = OnlineSessionInterface->GetSessionSettings(TEXT("Game"));
			if (GameSettings != NULL)
			{
				GameSettings->bAllowInvites = bAllowInvites;
				GameSettings->bAllowJoinInProgress = bAllowJoinInProgress;
				GameSettings->bAllowJoinViaPresence = bAllowJoinViaPresence;
				GameSettings->bAllowJoinViaPresenceFriendsOnly = bAllowJoinViaPresenceFriendsOnly;
				OnlineSessionInterface->UpdateSession(TEXT("Game"), *GameSettings, false);
			}

			TSharedPtr<FOnlineUserPresence> CurrentPresence;
			OnlinePresenceInterface->GetCachedPresence(*UserId, CurrentPresence);
			if (CurrentPresence.IsValid())
			{
				CurrentPresence->Status.StatusStr = NewPresenceString;
				OnlinePresenceInterface->SetPresence(*UserId, CurrentPresence->Status, IOnlinePresence::FOnPresenceTaskCompleteDelegate::CreateUObject(this, &UUTLocalPlayer::OnPresenceUpdated));
			}
			else
			{
				FOnlineUserPresenceStatus NewStatus;
				NewStatus.State = EOnlinePresenceState::Online;
				NewStatus.StatusStr = NewPresenceString;
				OnlinePresenceInterface->SetPresence(*UserId, NewStatus, IOnlinePresence::FOnPresenceTaskCompleteDelegate::CreateUObject(this, &UUTLocalPlayer::OnPresenceUpdated));
			}
		}
		else
		{
			LastPresenceUpdate = NewPresenceString;
			bLastAllowInvites = bAllowInvites;
		}
	}
}

bool UUTLocalPlayer::IsPlayerShowingSocialNotification() const
{
	return bShowSocialNotification;
}

void UUTLocalPlayer::OnPresenceUpdated(const FUniqueNetId& UserId, const bool bWasSuccessful)
{
	UE_LOG(UT,Verbose,TEXT("OnPresenceUpdated %s"), (bWasSuccessful ? TEXT("Successful") : TEXT("Failed")));
}

void UUTLocalPlayer::OnPresenceReceived(const FUniqueNetId& UserId, const TSharedRef<FOnlineUserPresence>& Presence)
{
	UE_LOG(UT,Verbose,TEXT("Presence Received %s %i %i"), *UserId.ToString(), Presence->bIsJoinable);
}

void UUTLocalPlayer::HandleFriendsJoinGame(const FUniqueNetId& FriendId, const FUniqueNetId& SessionId)
{
	JoinFriendSession(FriendId, SessionId);
}

bool UUTLocalPlayer::AllowFriendsJoinGame()
{
	// determine when to disable "join game" option in friends/chat UI
	return true;
}

void UUTLocalPlayer::HandleFriendsNotificationAvail(bool bAvailable)
{
	bShowSocialNotification = bAvailable;
}

void UUTLocalPlayer::HandleFriendsActionNotification(TSharedRef<FFriendsAndChatMessage> FriendsAndChatMessage)
{
#if WITH_SOCIAL
	if (FriendsAndChatMessage->GetMessageType() == EMessageType::GameInvite ||
		FriendsAndChatMessage->GetMessageType() == EMessageType::FriendAccepted ||
		FriendsAndChatMessage->GetMessageType() == EMessageType::FriendInvite ||
		(FriendsAndChatMessage->GetMessageType() == EMessageType::ChatMessage && !bShowingFriendsMenu))
	{
		ShowToast(FText::FromString(FriendsAndChatMessage->GetMessage()));
	}
#endif
}

void UUTLocalPlayer::JoinFriendSession(const FUniqueNetId& FriendId, const FUniqueNetId& SessionId)
{
	UE_LOG(UT, Log, TEXT("##########################"));
	UE_LOG(UT, Log, TEXT("Joining a Friend Session"));
	UE_LOG(UT, Log, TEXT("##########################"));

	//@todo samz - use FindSessionById instead of FindFriendSession with a pending SessionId
	PendingFriendInviteSessionId = SessionId.ToString();
	PendingFriendInviteFriendId = FriendId.ToString();
	OnFindFriendSessionCompleteDelegate = OnlineSessionInterface->AddOnFindFriendSessionCompleteDelegate_Handle(0, FOnFindFriendSessionCompleteDelegate::CreateUObject(this, &UUTLocalPlayer::OnFindFriendSessionComplete));
	OnlineSessionInterface->FindFriendSession(0, FriendId);
}

void UUTLocalPlayer::OnFindFriendSessionComplete(int32 LocalUserNum, bool bWasSuccessful, const FOnlineSessionSearchResult& SearchResult)
{
	OnlineSessionInterface->ClearOnFindFriendSessionCompleteDelegate_Handle(LocalUserNum, OnFindFriendSessionCompleteDelegate);
	if (bWasSuccessful)
	{
		if (SearchResult.Session.SessionInfo.IsValid())
		{
			JoinSession(SearchResult, false);
		}
		else
		{
			PendingFriendInviteFriendId = TEXT("");
			MessageBox(NSLOCTEXT("MCPMessages", "OnlineError", "Online Error"), NSLOCTEXT("MCPMessages", "InvalidFriendSession", "Friend no longer in session."));
		}
	}
	else
	{
		PendingFriendInviteFriendId = TEXT("");
		MessageBox(NSLOCTEXT("MCPMessages", "OnlineError", "Online Error"), NSLOCTEXT("MCPMessages", "NoFriendSession", "Couldn't find friend session to join."));
	}
	PendingFriendInviteSessionId = FString();
}

FName UUTLocalPlayer::GetCountryFlag()
{
	if (CurrentProfileSettings)
	{
		return CurrentProfileSettings->CountryFlag;
	}
	if (PlayerController)
	{
		AUTPlayerState* PS = Cast<AUTPlayerState>(PlayerController->PlayerState);
		if (PS)
		{
			return PS->CountryFlag;
		}
	}
	return NAME_None;
}

void UUTLocalPlayer::SetCountryFlag(FName NewFlag, bool bSave)
{
	if (CurrentProfileSettings)
	{
		CurrentProfileSettings->CountryFlag = NewFlag;
		if (bSave)
		{
			SaveProfileSettings();
		}
	}

	AUTPlayerController* PC = Cast<AUTPlayerController>(PlayerController);
	if (PC != NULL)
	{
		PC->ServerReceiveCountryFlag(NewFlag);
	}
}

FName UUTLocalPlayer::GetAvatar()
{
	if (CurrentProfileSettings)
	{
		return CurrentProfileSettings->Avatar;
	}

	if (PlayerController)
	{
		AUTPlayerState* PS = Cast<AUTPlayerState>(PlayerController->PlayerState);
		if (PS) return PS->Avatar;
	}
	return NAME_None;
}

void UUTLocalPlayer::SetAvatar(FName NewAvatar, bool bSave)
{
	if (CurrentProfileSettings)
	{
		CurrentProfileSettings->Avatar = NewAvatar;
		if (bSave)
		{
			SaveProfileSettings();
		}
	}

	AUTBasePlayerController * BasePC = Cast<AUTBasePlayerController>(PlayerController);
	if (BasePC != NULL)
	{
		BasePC->ServerSetAvatar(NewAvatar);
	}
}

#if !UE_SERVER
void UUTLocalPlayer::StartQuickMatch(FString QuickMatchType)
{
	if (IsLoggedIn() && OnlineSessionInterface.IsValid())
	{
		if (QuickMatchDialog.IsValid())
		{
			return;
		}
		if (GetWorld()->GetTimeSeconds() < QuickMatchLimitTime)
		{
			MessageBox(NSLOCTEXT("Generic","CantStartQuickMatchTitle","Please Wait"), NSLOCTEXT("Generic","CantStartQuickMatchText","You need to wait for at least 1 minute before you can attempt to quickmatch again."));
			return;
		}
		if ( ServerBrowserWidget.IsValid() && ServerBrowserWidget->IsRefreshing())
		{
			MessageBox(NSLOCTEXT("Generic","RequestInProgressTitle","Busy"), NSLOCTEXT("Generic","RequestInProgressText","A server list request is already in progress.  Please wait for it to finish before attempting to quickmatch."));
			return;
		}
		if (OnlineSessionInterface.IsValid())
		{
			OnlineSessionInterface->CancelFindSessions();				
		}

		SAssignNew(QuickMatchDialog, SUTQuickMatchWindow, this)
			.QuickMatchType(QuickMatchType);
		if (QuickMatchDialog.IsValid())
		{
			OpenWindow(QuickMatchDialog);
			QuickMatchDialog->TellSlateIWantKeyboardFocus();
		}
	}
	else
	{
		MessageBox(NSLOCTEXT("Generic","LoginNeededTitle","Login Needed"), NSLOCTEXT("Generic","LoginNeededMessage","You need to login before you can do that."));
	}
}
void UUTLocalPlayer::CloseQuickMatch()
{
	if (QuickMatchDialog.IsValid())
	{
		CloseWindow(QuickMatchDialog);
		QuickMatchDialog.Reset();
	}
}

#endif

void UUTLocalPlayer::ShowConnectingDialog()
{
#if !UE_SERVER
	if (!ConnectingDialog.IsValid())
	{
		FDialogResultDelegate Callback;
		Callback.BindUObject(this, &UUTLocalPlayer::ConnectingDialogCancel);

		TSharedPtr<SUTDialogBase> NewDialog; // important to make sure the ref count stays until OpenDialog()
		SAssignNew(NewDialog, SUTMessageBoxDialog)
			.PlayerOwner(this)
			.DialogTitle(NSLOCTEXT("UT", "ConnectingTitle", "Connecting..."))
			.MessageText(NSLOCTEXT("UT", "ConnectingText", "Connecting to server, please wait..."))
			.ButtonMask(UTDIALOG_BUTTON_CANCEL)
			.OnDialogResult(Callback);

		ConnectingDialog = NewDialog;
		OpenDialog(NewDialog.ToSharedRef());
	}
#endif
}
void UUTLocalPlayer::CloseConnectingDialog()
{
#if !UE_SERVER
	if (ConnectingDialog.IsValid())
	{
		CloseDialog(ConnectingDialog.Pin().ToSharedRef());
	}
#endif
}
void UUTLocalPlayer::ConnectingDialogCancel(TSharedPtr<SCompoundWidget> Dialog, uint16 ButtonID)
{
#if !UE_SERVER
	GEngine->Exec(GetWorld(), TEXT("Cancel"));
#endif
}

bool UUTLocalPlayer::IsInSession()
{ 
	TSharedPtr<const FUniqueNetId> UserId = OnlineIdentityInterface->GetUniquePlayerId(0);
	return (UserId.IsValid() && OnlineSessionInterface.IsValid() && OnlineSessionInterface->IsPlayerInSession(GameSessionName,*UserId));
}

#if !UE_SERVER
TSharedPtr<SUTMatchSummaryPanel> UUTLocalPlayer::GetSummaryPanel()
{
	TSharedPtr<SUTMatchSummaryPanel> MatchSummary;
	// If we have a menu open, have the menu try to show the player info.
	if (DesktopSlateWidget.IsValid())
	{
		TSharedPtr<SUTPanelBase> ActivePanel = DesktopSlateWidget->GetActivePanel();
		if (ActivePanel.IsValid() && ActivePanel->Tag == FName(TEXT("InGameHomePanel")))
		{
			TSharedPtr<SUTInGameHomePanel> HomePanel = StaticCastSharedPtr<SUTInGameHomePanel>(ActivePanel);
			if (HomePanel.IsValid())
			{
				return HomePanel->GetSummaryPanel();
			}
		}
	}

	MatchSummary.Reset();
	return MatchSummary;
}
#endif

void UUTLocalPlayer::ShowPlayerInfo(TWeakObjectPtr<AUTPlayerState> Target, bool bAllowLogout)
{
#if !UE_SERVER
	TSharedPtr<SUTMatchSummaryPanel> MatchSummary = GetSummaryPanel();
	if (MatchSummary.IsValid() && Target.IsValid())
	{
		MatchSummary->SelectPlayerState(Target.Get());
	}
	else
	{
		if (DesktopSlateWidget.IsValid() && !IsMenuGame() && Cast<AUTLobbyGameState>(GetWorld()->GameState) == nullptr && GetWorld()->GetNetMode() != NM_Standalone)
		{
			HideMenu();
		}
		OpenDialog(SNew(SUTPlayerInfoDialog).PlayerOwner(this).TargetPlayerState(Target).bAllowLogout(bAllowLogout));
	}
#endif
}

int32 UUTLocalPlayer::GetFriendsList(TArray< FUTFriend >& OutFriendsList)
{
	OutFriendsList.Empty();
	int32 RetVal = 0;

	// GetFilteredFriendsList is not exposed
#if 0
	TArray< TSharedPtr< IFriendItem > > FriendsList;
	RetVal = ISocialModule::Get().GetFriendsAndChatManager()->GetFilteredFriendsList(FriendsList);
	for (auto Friend : FriendsList)
	{
		OutFriendsList.Add(FUTFriend(Friend->GetUniqueID()->ToString(), Friend->GetName(), true));
	}

	OutFriendsList.Sort([](const FUTFriend& A, const FUTFriend& B) -> bool
	{
		return A.DisplayName < B.DisplayName;
	});
#endif

	return RetVal;
}

int32 UUTLocalPlayer::GetRecentPlayersList(TArray< FUTFriend >& OutRecentPlayersList)
{
	OutRecentPlayersList.Empty();

	int32 RetVal = 0;

	// GetRecentPlayersList is not exposed
#if 0
	TArray< TSharedPtr< IFriendItem > > RecentPlayersList;
	RetVal = ISocialModule::Get().GetFriendsAndChatManager()->GetRecentPlayersList(RecentPlayersList);
	for (auto RecentPlayer : RecentPlayersList)
	{
		OutRecentPlayersList.Add(FUTFriend(RecentPlayer->GetUniqueID()->ToString(), RecentPlayer->GetName(), false));
	}
	OutRecentPlayersList.Sort([](const FUTFriend& A, const FUTFriend& B) -> bool
	{
		return A.DisplayName < B.DisplayName;
	});
#endif

	return RetVal;
}


void UUTLocalPlayer::OnTauntPlayed(AUTPlayerState* PS, TSubclassOf<AUTTaunt> TauntToPlay, float EmoteSpeed)
{
#if !UE_SERVER
	TSharedPtr<SUTMatchSummaryPanel> MatchSummary = GetSummaryPanel();
	if (MatchSummary.IsValid())
	{
		MatchSummary->PlayTauntByClass(PS, TauntToPlay, EmoteSpeed);
	}
#endif
}

void UUTLocalPlayer::OnEmoteSpeedChanged(AUTPlayerState* PS, float EmoteSpeed)
{
#if !UE_SERVER
	TSharedPtr<SUTMatchSummaryPanel> MatchSummary = GetSummaryPanel();
	if (MatchSummary.IsValid())
	{
		MatchSummary->SetEmoteSpeed(PS, EmoteSpeed);
	}
#endif
}

void UUTLocalPlayer::RequestFriendship(TSharedPtr<const FUniqueNetId> FriendID)
{
	if (OnlineFriendsInterface.IsValid() && FriendID.IsValid())
	{
		OnlineFriendsInterface->SendInvite(0, *FriendID.Get(), EFriendsLists::ToString(EFriendsLists::Default));
	}
}

void UUTLocalPlayer::UpdateRedirect(const FString& FileURL, int32 NumBytes, float Progress, int32 NumFilesLeft)
{
	FString FName = FPaths::GetBaseFilename(FileURL);
	DownloadStatusText = FText::Format(NSLOCTEXT("UTLocalPlayer","DownloadStatusFormat","Downloading {0} Files: {1} ({2} / {3}) ...."), FText::AsNumber(NumFilesLeft), FText::FromString(FName), FText::AsNumber(NumBytes), FText::AsPercent(Progress));
	Download_NumBytes = NumBytes;
	Download_CurrentFile = FName;
	Download_Percentage = Progress;
	Download_NumFilesLeft = NumFilesLeft;

	UE_LOG(UT,Verbose,TEXT("Redirect: %s %i [%f%%]"), *FileURL, NumBytes, Progress);
}

bool UUTLocalPlayer::ContentExists(const FPackageRedirectReference& Redirect)
{
	FString Path = FPaths::Combine(*FPaths::GameSavedDir(), TEXT("Paks"), TEXT("DownloadedPaks"), *Redirect.PackageName) + TEXT(".pak");
	UUTGameEngine* UTEngine = Cast<UUTGameEngine>(GEngine);
	if (UTEngine)
	{
		if (UTEngine->LocalContentChecksums.Contains(Redirect.PackageName))
		{
			if (UTEngine->LocalContentChecksums[Redirect.PackageName] == Redirect.PackageChecksum)
			{
				return true;
			}
			else
			{
				// Local content has a non-matching md5, not sure if we should try to unmount/delete it
				return false;
			}
		}

		if (UTEngine->MountedDownloadedContentChecksums.Contains(Redirect.PackageName))
		{
			if (UTEngine->MountedDownloadedContentChecksums[Redirect.PackageName] == Redirect.PackageChecksum)
			{
				// We've already mounted the content needed and the checksum matches
				return true;
			}
			else
			{
				// Unmount the pak
				if (FCoreDelegates::OnUnmountPak.IsBound())
				{
					FCoreDelegates::OnUnmountPak.Execute(Path);
				}

				// Remove the CRC entry
				UTEngine->MountedDownloadedContentChecksums.Remove(Redirect.PackageName);
				UTEngine->DownloadedContentChecksums.Remove(Redirect.PackageName);

				// Delete the original file
				FPlatformFileManager::Get().GetPlatformFile().DeleteFile(*Path);
			}
		}

		if (UTEngine->DownloadedContentChecksums.Contains(Redirect.PackageName))
		{
			if (UTEngine->DownloadedContentChecksums[Redirect.PackageName] == Redirect.PackageChecksum)
			{
				// Mount the pak
				if (FCoreDelegates::OnMountPak.IsBound())
				{
					FCoreDelegates::OnMountPak.Execute(Path, 0);
					UTEngine->MountedDownloadedContentChecksums.Add(Redirect.PackageName, Redirect.PackageChecksum);
				}

				return true;
			}
			else
			{
				UTEngine->DownloadedContentChecksums.Remove(Redirect.PackageName);

				// Delete the original file
				FPlatformFileManager::Get().GetPlatformFile().DeleteFile(*Path);
			}
		}
		return false;
	}
	return true;
}

void UUTLocalPlayer::AccquireContent(TArray<FPackageRedirectReference>& Redirects)
{
	UUTGameViewportClient* UTGameViewport = Cast<UUTGameViewportClient>(ViewportClient);
	if (UTGameViewport)
	{
		for (int32 i = 0; i < Redirects.Num(); i++)
		{
			if (!Redirects[i].PackageName.IsEmpty() && !ContentExists(Redirects[i]))
			{
				UTGameViewport->DownloadRedirect(Redirects[i]);
			}
		}
	}
}

FText UUTLocalPlayer::GetDownloadStatusText()
{
	return IsDownloadInProgress() ? DownloadStatusText : FText::GetEmpty();
}

bool UUTLocalPlayer::IsDownloadInProgress()
{
	UUTGameViewportClient* UTGameViewport = Cast<UUTGameViewportClient>(ViewportClient);
	return UTGameViewport ? UTGameViewport->IsDownloadInProgress() : false;
}

void UUTLocalPlayer::CancelDownload()
{
	UUTGameViewportClient* UTGameViewport = Cast<UUTGameViewportClient>(ViewportClient);
	if (UTGameViewport && UTGameViewport->IsDownloadInProgress())
	{
		UTGameViewport->CancelAllRedirectDownloads();		
	}
}

void UUTLocalPlayer::HandleNetworkFailureMessage(enum ENetworkFailure::Type FailureType, const FString& ErrorString)
{
	AUTBasePlayerController* BasePlayerController = Cast<AUTBasePlayerController>(PlayerController);
	if (BasePlayerController)
	{
		BasePlayerController->HandleNetworkFailureMessage(FailureType, ErrorString);
	}
}
void UUTLocalPlayer::OpenLoadout(bool bBuyMenu)
{
#if !UE_SERVER
	// Create the slate widget if it doesn't exist
	if (!LoadoutMenu.IsValid())
	{
		if (bBuyMenu)
		{
			SAssignNew(LoadoutMenu, SUTBuyWindow).PlayerOwner(this);
		}
		else
		{
			SAssignNew(LoadoutMenu, SUTLoadoutWindow).PlayerOwner(this);
		}

		if (LoadoutMenu.IsValid())
		{
			GEngine->GameViewport->AddViewportWidgetContent( SNew(SWeakWidget).PossiblyNullContent(LoadoutMenu.ToSharedRef()),60);
		}

		// Make it visible.
		if (LoadoutMenu.IsValid())
		{
			// Widget is already valid, just make it visible.
			LoadoutMenu->SetVisibility(EVisibility::Visible);
			LoadoutMenu->OnMenuOpened(TEXT(""));
		}
	}
#endif
}
void UUTLocalPlayer::CloseLoadout()
{
#if !UE_SERVER
	if (LoadoutMenu.IsValid())
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(LoadoutMenu.ToSharedRef());
		LoadoutMenu->OnMenuClosed();
		LoadoutMenu.Reset();
		if (PlayerController)
		{
			PlayerController->SetPause(false);
		}
	}
#endif
}

void UUTLocalPlayer::OpenMapVote(AUTGameState* GameState)
{
#if !UE_SERVER
	if (!MapVoteMenu.IsValid())
	{

		if (GameState == NULL)
		{
			GameState = GetWorld()->GetGameState<AUTGameState>();
			if (GameState == NULL) return;
		}

		SAssignNew(MapVoteMenu,SUTMapVoteDialog).PlayerOwner(this).GameState(GameState);
		OpenDialog( MapVoteMenu.ToSharedRef(), 200 );
	}
#endif
}
void UUTLocalPlayer::CloseMapVote()
{
#if !UE_SERVER
	if (MapVoteMenu.IsValid())
	{
		CloseDialog(MapVoteMenu.ToSharedRef());		
		MapVoteMenu.Reset();
	}
#endif
}

void UUTLocalPlayer::OpenMatchSummary(AUTGameState* GameState)
{
	ShowMenu(TEXT(""));

/*
#if !UE_SERVER
	PlayerController->DisableInput(PlayerController);
	if (MatchSummaryWindow.IsValid())
	{
		CloseMatchSummary();
	}
	SAssignNew(MatchSummaryWindow, SUTMatchSummaryPanel, this).GameState(GameState);
	
	UUTGameViewportClient* UTGVC = Cast<UUTGameViewportClient>(GEngine->GameViewport);
	if (MatchSummaryWindow.IsValid() && UTGVC != nullptr)
	{
		OpenWindow(MatchSummaryWindow);
		FSlateApplication::Get().SetKeyboardFocus(MatchSummaryWindow.ToSharedRef(), EKeyboardFocusCause::Keyboard);
	}
#endif
*/
}
void UUTLocalPlayer::CloseMatchSummary()
{
/*
#if !UE_SERVER
	UUTGameViewportClient* UTGVC = Cast<UUTGameViewportClient>(GEngine->GameViewport);
	if (MatchSummaryWindow.IsValid() && UTGVC != nullptr)
	{
		CloseWindow(MatchSummaryWindow);
		MatchSummaryWindow.Reset();
		
		//Since we use SUTInGameHomePanel for the time being for chat, we need to clear bForceScores
		AUTPlayerController* PC = Cast<AUTPlayerController>(PlayerController);
		if (PC && PC->MyUTHUD)
		{
			PC->MyUTHUD->bForceScores = false;
		}

		PlayerController->FlushPressedKeys();
		PlayerController->EnableInput(PlayerController);
	}
#endif
*/	
}

void UUTLocalPlayer::OpenReplayWindow()
{
#if !UE_SERVER
	UDemoNetDriver* DemoDriver = GetWorld()->DemoNetDriver;
	if (DemoDriver)
	{
		// If the demo net driver changed, reopen the window, we're in a different replay.
		if (ReplayWindow.IsValid() && ReplayWindow->GetDemoNetDriver() != DemoDriver)
		{
			CloseReplayWindow();
		}

		if (!ReplayWindow.IsValid())
		{
			SAssignNew(ReplayWindow, SUTReplayWindow)
				.PlayerOwner(this)
				.DemoNetDriver(DemoDriver);

			UUTGameViewportClient* UTGVC = Cast<UUTGameViewportClient>(GEngine->GameViewport);
			if (ReplayWindow.IsValid() && UTGVC != nullptr)
			{
				UTGVC->AddViewportWidgetContent_NoAspect(ReplayWindow.ToSharedRef(), 0);
				ReplayWindow->SetVisibility(EVisibility::SelfHitTestInvisible);
				ReplayWindow->GrabKeyboardFocus();
			}
		}
	}
#endif
}

void UUTLocalPlayer::CloseReplayWindow()
{
#if !UE_SERVER
	UUTGameViewportClient* UTGVC = Cast<UUTGameViewportClient>(GEngine->GameViewport);
	if (ReplayWindow.IsValid() && UTGVC != nullptr)
	{
		UTGVC->RemoveViewportWidgetContent_NoAspect(ReplayWindow.ToSharedRef());
		ReplayWindow.Reset();
	}
#endif
}

void UUTLocalPlayer::ToggleReplayWindow()
{
#if !UE_SERVER
	if (IsReplay())
	{
		if (!ReplayWindow.IsValid())
		{
			OpenReplayWindow();
		}
		else
		{
			CloseReplayWindow();
		}
	}
#endif
}

bool UUTLocalPlayer::IsReplay()
{
	return (GetWorld()->DemoNetDriver != nullptr);
}

#if !UE_SERVER

void UUTLocalPlayer::RecordReplay(float RecordTime)
{
	if (!bRecordingReplay)
	{
		CloseReplayWindow();

		bRecordingReplay = true;

		static const FName VideoRecordingFeatureName("VideoRecording");
		if (IModularFeatures::Get().IsModularFeatureAvailable(VideoRecordingFeatureName))
		{
			UTVideoRecordingFeature* VideoRecorder = &IModularFeatures::Get().GetModularFeature<UTVideoRecordingFeature>(VideoRecordingFeatureName);
			if (VideoRecorder)
			{
				VideoRecorder->OnRecordingComplete().AddUObject(this, &UUTLocalPlayer::RecordingReplayComplete);
				VideoRecorder->StartRecording(RecordTime);
			}
		}
	}
}

void UUTLocalPlayer::RecordingReplayComplete()
{
	bRecordingReplay = false;

	static const FName VideoRecordingFeatureName("VideoRecording");
	if (IModularFeatures::Get().IsModularFeatureAvailable(VideoRecordingFeatureName))
	{
		UTVideoRecordingFeature* VideoRecorder = &IModularFeatures::Get().GetModularFeature<UTVideoRecordingFeature>(VideoRecordingFeatureName);
		if (VideoRecorder)
		{
			VideoRecorder->OnRecordingComplete().RemoveAll(this);
		}
	}

	// Pause the replay streamer
	AWorldSettings* const WorldSettings = GetWorld()->GetWorldSettings();
	if (WorldSettings->Pauser == nullptr)
	{
		WorldSettings->Pauser = (PlayerController != nullptr) ? PlayerController->PlayerState : nullptr;
	}

	// Show a dialog asking player if they want to compress
	ShowMessage(NSLOCTEXT("VideoMessages", "CompressNowTitle", "Compress now?"),
				NSLOCTEXT("VideoMessages", "CompressNow", "Your video recorded successfully.\nWould you like to compress the video now? It may take several minutes."),
				UTDIALOG_BUTTON_YES | UTDIALOG_BUTTON_NO, FDialogResultDelegate::CreateUObject(this, &UUTLocalPlayer::ShouldVideoCompressDialogResult));
}

void UUTLocalPlayer::ShouldVideoCompressDialogResult(TSharedPtr<SCompoundWidget> Widget, uint16 ButtonID)
{
	if (ButtonID == UTDIALOG_BUTTON_YES)
	{
		// Pick a proper filename for the video
		FString BasePath = FPaths::ScreenShotDir();
		if (IFileManager::Get().MakeDirectory(*FPaths::ScreenShotDir(), true))
		{
			RecordedReplayFilename = BasePath / TEXT("anim.webm");
			static int32 WebMIndex = 0;
			const int32 MaxTestWebMIndex = 65536;
			for (int32 TestWebMIndex = WebMIndex + 1; TestWebMIndex < MaxTestWebMIndex; ++TestWebMIndex)
			{
				const FString TestFileName = BasePath / FString::Printf(TEXT("UTReplay%05i.webm"), TestWebMIndex);
				if (IFileManager::Get().FileSize(*TestFileName) < 0)
				{
					WebMIndex = TestWebMIndex;
					RecordedReplayFilename = TestFileName;
					break;
				}
			}

			static const FName VideoRecordingFeatureName("VideoRecording");
			if (IModularFeatures::Get().IsModularFeatureAvailable(VideoRecordingFeatureName))
			{
				UTVideoRecordingFeature* VideoRecorder = &IModularFeatures::Get().GetModularFeature<UTVideoRecordingFeature>(VideoRecordingFeatureName);
				if (VideoRecorder)
				{
					// Open a dialog that shows a nice progress bar of the compression
					OpenDialog(SNew(SUTVideoCompressionDialog)
								.OnDialogResult(FDialogResultDelegate::CreateUObject(this, &UUTLocalPlayer::VideoCompressDialogResult))
								.DialogTitle(NSLOCTEXT("VideoMessages", "Compressing", "Compressing"))
								.PlayerOwner(this)
								.VideoRecorder(VideoRecorder)
								.VideoFilename(RecordedReplayFilename)
								);
				}
			}
		}
	}
	else
	{
		OpenReplayWindow();
	}
}

void UUTLocalPlayer::VideoCompressDialogResult(TSharedPtr<SCompoundWidget> Widget, uint16 ButtonID)
{
	if (ButtonID == UTDIALOG_BUTTON_OK)
	{
		OpenDialog(SNew(SUTInputBoxDialog)
			.OnDialogResult(FDialogResultDelegate::CreateUObject(this, &UUTLocalPlayer::ShouldVideoUploadDialogResult))
			.PlayerOwner(this)
			.DialogSize(FVector2D(700, 400))
			.bDialogSizeIsRelative(false)
			.DefaultInput(TEXT("UT Automated Upload"))
			.DialogTitle(NSLOCTEXT("VideoMessages", "UploadNowTitle", "Upload to YouTube?"))
			.MessageText(NSLOCTEXT("VideoMessages", "UploadNow", "Your video compressed successfully.\nWould you like to upload the video to YouTube now?\n\nPlease enter a video title in the text box."))
			.ButtonMask(UTDIALOG_BUTTON_YES | UTDIALOG_BUTTON_NO)
			);
	}
	else
	{
		OpenReplayWindow();
	}
}

void UUTLocalPlayer::ShouldVideoUploadDialogResult(TSharedPtr<SCompoundWidget> Widget, uint16 ButtonID)
{
	if (ButtonID == UTDIALOG_BUTTON_YES)
	{
		TSharedPtr<SUTInputBoxDialog> Box = StaticCastSharedPtr<SUTInputBoxDialog>(Widget);
		if (Box.IsValid())
		{
			RecordedReplayTitle = Box->GetInputText();
		}

		if (YoutubeAccessToken.IsEmpty())
		{
			GetYoutubeConsentForUpload();
		}
		else if (!YoutubeRefreshToken.IsEmpty())
		{
			// Show a dialog here to stop the user for doing anything
			YoutubeDialog = ShowMessage(NSLOCTEXT("VideoMessages", "YoutubeTokenTitle", "AccessingYoutube"),
				NSLOCTEXT("VideoMessages", "YoutubeToken", "Contacting YouTube..."), 0);

			FHttpRequestPtr YoutubeTokenRefreshRequest = FHttpModule::Get().CreateRequest();
			YoutubeTokenRefreshRequest->SetURL(TEXT("https://accounts.google.com/o/oauth2/token"));
			YoutubeTokenRefreshRequest->OnProcessRequestComplete().BindUObject(this, &UUTLocalPlayer::YoutubeTokenRefreshComplete);
			YoutubeTokenRefreshRequest->SetVerb(TEXT("POST"));
			YoutubeTokenRefreshRequest->SetHeader(TEXT("Content-Type"), TEXT("application/x-www-form-urlencoded"));

			// ClientID and ClientSecret UT Youtube app on PLK google account
			FString ClientID = TEXT("465724645978-10npjjgfbb03p4ko12ku1vq1ioshts24.apps.googleusercontent.com");
			FString ClientSecret = TEXT("kNKauX2DKUq_5cks86R8rD5E");
			FString TokenRequest = TEXT("client_id=") + ClientID + TEXT("&client_secret=") + ClientSecret + 
				                   TEXT("&refresh_token=") + YoutubeRefreshToken + TEXT("&grant_type=refresh_token");

			YoutubeTokenRefreshRequest->SetContentAsString(TokenRequest);
			YoutubeTokenRefreshRequest->ProcessRequest();
		}
	}
	else
	{
		OpenReplayWindow();
	}
}

void UUTLocalPlayer::YoutubeConsentResult(TSharedPtr<SCompoundWidget> Widget, uint16 ButtonID)
{
	if (ButtonID == UTDIALOG_BUTTON_OK)
	{
		if (!YoutubeDialog.IsValid())
		{
			// Show a dialog here to stop the user for doing anything
			YoutubeDialog = ShowMessage(NSLOCTEXT("VideoMessages", "YoutubeTokenTitle", "AccessingYoutube"),
				NSLOCTEXT("VideoMessages", "YoutubeToken", "Contacting YouTube..."), 0);

			FHttpRequestPtr YoutubeTokenRequest = FHttpModule::Get().CreateRequest();
			YoutubeTokenRequest->SetURL(TEXT("https://accounts.google.com/o/oauth2/token"));
			YoutubeTokenRequest->OnProcessRequestComplete().BindUObject(this, &UUTLocalPlayer::YoutubeTokenRequestComplete);
			YoutubeTokenRequest->SetVerb(TEXT("POST"));
			YoutubeTokenRequest->SetHeader(TEXT("Content-Type"), TEXT("application/x-www-form-urlencoded"));

			// ClientID and ClientSecret UT Youtube app on PLK google account
			FString ClientID = TEXT("465724645978-10npjjgfbb03p4ko12ku1vq1ioshts24.apps.googleusercontent.com");
			FString ClientSecret = TEXT("kNKauX2DKUq_5cks86R8rD5E");
			FString TokenRequest = TEXT("code=") + YoutubeConsentDialog->UniqueCode + TEXT("&client_id=") + ClientID
				+ TEXT("&client_secret=") + ClientSecret + TEXT("&redirect_uri=urn:ietf:wg:oauth:2.0:oob&grant_type=authorization_code");

			YoutubeTokenRequest->SetContentAsString(TokenRequest);
			YoutubeTokenRequest->ProcessRequest();
		}
		else
		{
			UE_LOG(UT, Warning, TEXT("Already getting Youtube Consent"));
		}
	}
	else
	{
		UE_LOG(UT, Warning, TEXT("Failed to get Youtube consent"));
	}
}

void UUTLocalPlayer::YoutubeTokenRequestComplete(FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded)
{
	if (YoutubeDialog.IsValid())
	{
		CloseDialog(YoutubeDialog.ToSharedRef());
		YoutubeDialog.Reset();
	}

	if (HttpResponse.IsValid())
	{
		if (HttpResponse->GetResponseCode() == 200)
		{
			TSharedPtr<FJsonObject> YoutubeTokenJson;
			TSharedRef<TJsonReader<>> JsonReader = TJsonReaderFactory<>::Create(HttpResponse->GetContentAsString());
			if (FJsonSerializer::Deserialize(JsonReader, YoutubeTokenJson) && YoutubeTokenJson.IsValid())
			{
				YoutubeTokenJson->TryGetStringField(TEXT("access_token"), YoutubeAccessToken);
				YoutubeTokenJson->TryGetStringField(TEXT("refresh_token"), YoutubeRefreshToken);

				UE_LOG(UT, Log, TEXT("YoutubeTokenRequestComplete %s %s"), *YoutubeAccessToken, *YoutubeRefreshToken);

				SaveConfig();

				UploadVideoToYoutube();
			}
		}
		else
		{
			UE_LOG(UT, Warning, TEXT("Failed to get token from Youtube\n%s"), *HttpResponse->GetContentAsString());
		}
	}
	else
	{
		UE_LOG(UT, Warning, TEXT("Failed to get token from Youtube. Request failed."));
	}
}

void UUTLocalPlayer::YoutubeTokenRefreshComplete(FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded)
{
	if (YoutubeDialog.IsValid())
	{
		CloseDialog(YoutubeDialog.ToSharedRef());
		YoutubeDialog.Reset();
	}

	if (HttpResponse.IsValid() && HttpResponse->GetResponseCode() == 200)
	{
		UE_LOG(UT, Log, TEXT("YouTube Token refresh succeeded"));

		TSharedPtr<FJsonObject> YoutubeTokenJson;
		TSharedRef<TJsonReader<>> JsonReader = TJsonReaderFactory<>::Create(HttpResponse->GetContentAsString());
		if (FJsonSerializer::Deserialize(JsonReader, YoutubeTokenJson) && YoutubeTokenJson.IsValid())
		{
			YoutubeTokenJson->TryGetStringField(TEXT("access_token"), YoutubeAccessToken);
			SaveConfig();

			UploadVideoToYoutube();
		}
	}
	else
	{
		if (HttpResponse.IsValid())
		{
			UE_LOG(UT, Log, TEXT("YouTube Token might've expired, doing full consent\n%s"), *HttpResponse->GetContentAsString());
		}
		else
		{
			UE_LOG(UT, Log, TEXT("YouTube Token might've expired, doing full consent"));
		}

		// Refresh token might have been expired
		YoutubeAccessToken.Empty();
		YoutubeRefreshToken.Empty();

		GetYoutubeConsentForUpload();
	}
}

void UUTLocalPlayer::GetYoutubeConsentForUpload()
{
	// Get youtube consent
	OpenDialog(
		SAssignNew(YoutubeConsentDialog, SUTYoutubeConsentDialog)
		.PlayerOwner(this)
		.DialogSize(FVector2D(0.8f, 0.8f))
		.DialogPosition(FVector2D(0.5f, 0.5f))
		.DialogTitle(NSLOCTEXT("UUTLocalPlayer", "YoutubeConsent", "Allow UT to post to YouTube?"))
		.ButtonMask(UTDIALOG_BUTTON_CANCEL)
		.OnDialogResult(FDialogResultDelegate::CreateUObject(this, &UUTLocalPlayer::YoutubeConsentResult))
		);
}

void UUTLocalPlayer::UploadVideoToYoutube()
{
	// Get youtube consent
	OpenDialog(
		SNew(SUTYoutubeUploadDialog)
		.PlayerOwner(this)
		.ButtonMask(UTDIALOG_BUTTON_CANCEL)
		.VideoFilename(RecordedReplayFilename)
		.AccessToken(YoutubeAccessToken)
		.VideoTitle(RecordedReplayTitle)
		.DialogTitle(NSLOCTEXT("UUTLocalPlayer", "YoutubeUpload", "Uploading To Youtube"))
		.OnDialogResult(FDialogResultDelegate::CreateUObject(this, &UUTLocalPlayer::YoutubeUploadResult))
		);
}

void UUTLocalPlayer::YoutubeUploadResult(TSharedPtr<SCompoundWidget> Widget, uint16 ButtonID)
{
	if (ButtonID == UTDIALOG_BUTTON_OK)
	{
		ShowMessage(NSLOCTEXT("UUTLocalPlayer", "YoutubeUploadCompleteTitle", "Upload To Youtube Complete"),
					NSLOCTEXT("UUTLocalPlayer", "YoutubeUploadComplete", "Your upload to Youtube completed successfully. It will be available in a few minutes."),
					UTDIALOG_BUTTON_OK, FDialogResultDelegate::CreateUObject(this, &UUTLocalPlayer::YoutubeUploadCompleteResult));
	}
	else
	{
		SUTYoutubeUploadDialog* UploadDialog = (SUTYoutubeUploadDialog*)Widget.Get();
		TSharedRef< TJsonReader<> > JsonReader = TJsonReaderFactory<>::Create(UploadDialog->UploadFailMessage);
		TSharedPtr< FJsonObject > JsonObject;
		FJsonSerializer::Deserialize(JsonReader, JsonObject);
		const TSharedPtr<FJsonObject>* ErrorObject;
		bool bNeedsYoutubeSignup = false;
		if (JsonObject->TryGetObjectField(TEXT("error"), ErrorObject))
		{
			const TArray<TSharedPtr<FJsonValue>>* ErrorArray;
			if ((*ErrorObject)->TryGetArrayField(TEXT("errors"), ErrorArray))
			{
				for (int32 Idx = 0; Idx < ErrorArray->Num(); Idx++)
				{
					FString ErrorReason;
					if ((*ErrorArray)[Idx]->AsObject()->TryGetStringField(TEXT("reason"), ErrorReason))
					{
						if (ErrorReason == TEXT("youtubeSignupRequired"))
						{
							bNeedsYoutubeSignup = true;
						}
					}
				}
			}
		}

		if (bNeedsYoutubeSignup)
		{
			ShowMessage(NSLOCTEXT("UUTLocalPlayer", "YoutubeUploadNeedSignupTitle", "Upload To Youtube Failed"),
						NSLOCTEXT("UUTLocalPlayer", "YoutubeUploadNeedSignup", "Your account does not currently have a YouTube channel.\nPlease create one and try again."),
						UTDIALOG_BUTTON_OK, FDialogResultDelegate::CreateUObject(this, &UUTLocalPlayer::YoutubeUploadCompleteResult));
		}
		else
		{		
			ShowMessage(NSLOCTEXT("UUTLocalPlayer", "YoutubeUploadCompleteFailedTitle", "Upload To Youtube Failed"),
						NSLOCTEXT("UUTLocalPlayer", "YoutubeUploadCompleteFailed", "Your upload to Youtube did not complete successfully."),
						UTDIALOG_BUTTON_OK, FDialogResultDelegate::CreateUObject(this, &UUTLocalPlayer::YoutubeUploadCompleteResult));
		}
	}
}

void UUTLocalPlayer::YoutubeUploadCompleteResult(TSharedPtr<SCompoundWidget> Widget, uint16 ButtonID)
{
	OpenReplayWindow();
}

#endif

void UUTLocalPlayer::VerifyGameSession(const FString& ServerSessionId)
{
	if (IsReplay())
	{
		return;
	}

	if (OnlineSessionInterface.IsValid())
	{
		// Get our current Session Id.
		FNamedOnlineSession* Session = OnlineSessionInterface->GetNamedSession(FName(TEXT("Game")));
		if (Session == NULL || !Session->SessionInfo.IsValid() || Session->SessionInfo->GetSessionId().ToString() != ServerSessionId)
		{
			TSharedPtr<const FUniqueNetId> UserId = OnlineIdentityInterface->GetUniquePlayerId(GetControllerId());
			if (UserId.IsValid())
			{
				TSharedPtr<const FUniqueNetId> ServerId = MakeShareable(new FUniqueNetIdString(ServerSessionId));		
				TSharedPtr<const FUniqueNetId> EmptyId = MakeShareable(new FUniqueNetIdString(""));				
				FOnSingleSessionResultCompleteDelegate CompletionDelegate;
				CompletionDelegate.BindUObject(this, &UUTLocalPlayer::OnFindSessionByIdComplete);
				OnlineSessionInterface->FindSessionById(*UserId, *ServerId, *EmptyId, CompletionDelegate);
			}
		}
	}
}

void UUTLocalPlayer::OnFindSessionByIdComplete(int32 LocalUserNum, bool bWasSucessful, const FOnlineSessionSearchResult& SearchResult)
{
	if (bWasSucessful)
	{
		bAttemptingForceJoin = true;
		OnlineSessionInterface->JoinSession(0, GameSessionName, SearchResult);
	}
}

void UUTLocalPlayer::CloseAllUI(bool bExceptDialogs)
{
	ChatArchive.Empty();

	if (GetWorld()->WorldType == EWorldType::Game)
	{
		GEngine->GameViewport->RemoveAllViewportWidgets();
	}
#if !UE_SERVER
	if (bExceptDialogs)
	{
		if (GetWorld()->WorldType == EWorldType::Game)
		{
			// restore dialogs to the viewport
			for (TSharedPtr<SUTDialogBase> Dialog : OpenDialogs)
			{
				if ( Dialog.IsValid() && (!MapVoteMenu.IsValid() || Dialog.Get() != MapVoteMenu.Get()) && (!DownloadAllDialog.IsValid() || Dialog.Get() != DownloadAllDialog.Get()) )
				{
					GEngine->GameViewport->AddViewportWidgetContent(Dialog.ToSharedRef(), 255);
				}
			}
		}
	}
	else
	{
		OpenDialogs.Empty();
	}
	
	// These should all be proper closes
	DesktopSlateWidget.Reset();
	ServerBrowserWidget.Reset();
	ReplayBrowserWidget.Reset();
	StatsViewerWidget.Reset();
	CreditsPanelWidget.Reset();
	QuickMatchDialog.Reset();
	LoginDialog.Reset();
	HUDSettings.Reset();
	ContentLoadingMessage.Reset();
	FriendsMenu.Reset();
	RedirectDialog.Reset();
	LoadoutMenu.Reset();
	ReplayWindow.Reset();
	YoutubeDialog.Reset();
	YoutubeConsentDialog.Reset();
	DownloadAllDialog.Reset();

	AdminDialogClosed();
	CloseMapVote();
	CloseMatchSummary();
	CloseSpectatorWindow();

	if (ToastList.Num() > 0)
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(ToastList[0].ToSharedRef());
		ToastList.Empty();
	}

#endif
}

void UUTLocalPlayer::AttemptJoinInstance(TSharedPtr<FServerData> ServerData, FString InstanceId, bool bSpectate)
{
#if !UE_SERVER

	SAssignNew(JoinInstanceDialog, SUTJoinInstanceWindow, this)
		.ServerData(ServerData)
		.InstanceId(InstanceId)
		.bSpectator(bSpectate);

	if (JoinInstanceDialog.IsValid())
	{
		OpenWindow(JoinInstanceDialog);
		JoinInstanceDialog->TellSlateIWantKeyboardFocus();
	}
#endif
}
void UUTLocalPlayer::CloseJoinInstanceDialog()
{
#if !UE_SERVER
	if (JoinInstanceDialog.IsValid())
	{
		CloseWindow(JoinInstanceDialog);
		JoinInstanceDialog.Reset();
	}
#endif

}


int32 UUTLocalPlayer::GetTotalChallengeStars()
{
	int32 TotalStars = 0;
	if (CurrentProgression)
	{
		for (int32 i = 0 ; i < CurrentProgression->ChallengeResults.Num(); i++)
		{
			TotalStars += CurrentProgression->ChallengeResults[i].Stars;
		}
	}

	return TotalStars;
}

int32 UUTLocalPlayer::GetChallengeStars(FName ChallengeTag)
{
	if (CurrentProgression)
	{
		for (int32 i = 0 ; i < CurrentProgression->ChallengeResults.Num(); i++)
		{
			if (CurrentProgression->ChallengeResults[i].Tag == ChallengeTag)
			{
				return CurrentProgression->ChallengeResults[i].Stars;
			}
		}
	}

	return 0;
}

int32 UUTLocalPlayer::GetRewardStars(FName RewardTag)
{
	// Count all of the stars for this reward type.

	int32 TotalStars = 0;

	UUTGameEngine* UTGameEngine = Cast<UUTGameEngine>(GEngine);
	if (UTGameEngine )
	{
		TWeakObjectPtr<UUTChallengeManager> ChallengeManager = UTGameEngine->GetChallengeManager();
		if (ChallengeManager.IsValid())
		{
			for (auto It = ChallengeManager->Challenges.CreateConstIterator(); It; ++It)
			{
				const FUTChallengeInfo Challenge = It.Value();
				if (Challenge.RewardTag == RewardTag)
				{
					FName ChallengeTag = It.Key();
					TotalStars += GetChallengeStars(ChallengeTag);
				}
			}
		}
	}
	return TotalStars;
}

FString UUTLocalPlayer::GetChallengeDate(FName ChallengeTag)
{
	if (CurrentProgression)
	{
		for (int32 i = 0 ; i < CurrentProgression->ChallengeResults.Num(); i++)
		{
			if (CurrentProgression->ChallengeResults[i].Tag == ChallengeTag)
			{
				FDateTime LastUpdate = CurrentProgression->ChallengeResults[i].LastUpdate;
				return LastUpdate.ToString(TEXT("%m.%d.%y @ %h:%M:%S%a"));
			}
		}
	}
	return TEXT("Never");
}

void UUTLocalPlayer::AwardAchievement(FName AchievementName)
{
	static FName NAME_RequiredAchievement(TEXT("RequiredAchievement"));
	static FName NAME_CosmeticName(TEXT("CosmeticName"));
	static FName NAME_DisplayName(TEXT("DisplayName"));
	if (CurrentProgression != NULL && !CurrentProgression->Achievements.Contains(AchievementName))
	{
		CurrentProgression->Achievements.Add(AchievementName);

		TArray<FAssetData> PossibleUnlocks;
		GetAllBlueprintAssetData(AUTCosmetic::StaticClass(), PossibleUnlocks, true);
		GetAllBlueprintAssetData(AUTCharacterContent::StaticClass(), PossibleUnlocks, true);
		for (const FAssetData& Item : PossibleUnlocks)
		{
			const FString* ReqAchievement = Item.TagsAndValues.Find(NAME_RequiredAchievement);
			if (ReqAchievement != NULL && FName(**ReqAchievement) == AchievementName)
			{
				const FString* DisplayName = Item.TagsAndValues.Find(NAME_DisplayName);
				if (DisplayName == NULL)
				{
					DisplayName = Item.TagsAndValues.Find(NAME_CosmeticName);
				}
				if (DisplayName != NULL)
				{
					ShowToast(FText::Format(NSLOCTEXT("UT", "AchievementAward", "Unlocked {0}"), FText::FromString(*DisplayName)));
				}
			}
		}
	}
}

void UUTLocalPlayer::SkullPickedUp()
{
	if (CurrentProgression)
	{
		CurrentProgression->SetSkullCount(CurrentProgression->SkullCount++);
		if (CurrentProgression->SkullCount > 200)
		{
			AwardAchievement(AchievementIDs::PumpkinHead2015Level1);
		}
		if (CurrentProgression->SkullCount > 1000)
		{
			AwardAchievement(AchievementIDs::PumpkinHead2015Level2);
		}
		if (CurrentProgression->SkullCount > 5000)
		{
			AwardAchievement(AchievementIDs::PumpkinHead2015Level3);
		}
	}
}

void UUTLocalPlayer::ChallengeCompleted(FName ChallengeTag, int32 Stars)
{
	EarnedStars = 0;
	if (CurrentProgression && Stars > 0)
	{
		bool bFound = false;
		for (int32 i = 0 ; i < CurrentProgression->ChallengeResults.Num(); i++)
		{
			if (CurrentProgression->ChallengeResults[i].Tag == ChallengeTag)
			{
				if (CurrentProgression->ChallengeResults[i].Stars < Stars)
				{
					EarnedStars = Stars - CurrentProgression->ChallengeResults[i].Stars;
					CurrentProgression->ChallengeResults[i].Update(Stars);
					CurrentProgression->NeedsUpdate();
				}

				bFound = true;
				break;
			}
		}
		if (!bFound)
		{
			EarnedStars = Stars;
			CurrentProgression->ChallengeResults.Add(FUTChallengeResult(ChallengeTag, Stars));
			CurrentProgression->NeedsUpdate();
		}

		// Look up the Challenge info for this challenge...

		UUTGameEngine* UTGameEngine = Cast<UUTGameEngine>(GEngine);
		if (UTGameEngine )
		{
			TWeakObjectPtr<UUTChallengeManager> ChallengeManager = UTGameEngine->GetChallengeManager();
			if (ChallengeManager.IsValid())
			{
				const FUTChallengeInfo* Challenge = ChallengeManager->GetChallenge(ChallengeTag);
				if (Challenge)
				{
					int32 TotalStars = GetRewardStars(Challenge->RewardTag);
					FText ChallengeToast = FText::Format(NSLOCTEXT("Challenge", "GainedStars", "Challenge Completed!  You earned {0} stars."), FText::AsNumber(Stars));
					ShowToast(ChallengeToast);

					if (Challenge->RewardTag == NAME_REWARD_GoldStars)
					{
						if (TotalStars >= 5)
						{
							AwardAchievement(AchievementIDs::ChallengeStars5);
						}
						if (TotalStars >= 15)
						{
							AwardAchievement(AchievementIDs::ChallengeStars15);
						}
						if (TotalStars >= 25)
						{
							AwardAchievement(AchievementIDs::ChallengeStars25);
						}
						if (TotalStars >= 35)
						{
							AwardAchievement(AchievementIDs::ChallengeStars35);
						}
						if (TotalStars >= 45)
						{
							AwardAchievement(AchievementIDs::ChallengeStars45);
						}

						bool bEarnedRosterUpgrade = (TotalStars / 5 != (TotalStars - EarnedStars) / 5) && UUTChallengeManager::StaticClass()->GetDefaultObject<UUTChallengeManager>()->PlayerTeamRoster.Roster.IsValidIndex(4 + (TotalStars - Stars) / 5);
						if (bEarnedRosterUpgrade)
						{
							FText OldTeammate = FText::FromName(UUTChallengeManager::StaticClass()->GetDefaultObject<UUTChallengeManager>()->PlayerTeamRoster.Roster[(TotalStars - Stars) / 5]);
							FText NewTeammate = FText::FromName(UUTChallengeManager::StaticClass()->GetDefaultObject<UUTChallengeManager>()->PlayerTeamRoster.Roster[4 + (TotalStars - Stars) / 5]);
							RosterUpgradeText = FText::Format(NSLOCTEXT("Challenge", "RosterUpgrade", "Roster Upgrade!  {0} replaces {1}."), NewTeammate, OldTeammate);
							ShowToast(RosterUpgradeText);
						}
					}
					else if (Challenge->RewardTag == NAME_REWARD_HalloweenStars)
					{
						if (TotalStars >= 5)
						{
							AwardAchievement(AchievementIDs::ChallengePumpkins5);
						}
						if (TotalStars >= 10)
						{
							AwardAchievement(AchievementIDs::ChallengePumpkins10);
						}
						if (TotalStars >= 15)
						{
							AwardAchievement(AchievementIDs::ChallengePumpkins15);
						}
					}
				}
			}
		}

		int32 AllStars = GetTotalChallengeStars();
		CurrentProgression->TotalChallengeStars = AllStars;
		AUTPlayerState* PS = PlayerController ? Cast<AUTPlayerState>(PlayerController->PlayerState) : NULL;
		if (PS)
		{
			PS->TotalChallengeStars = AllStars;
		}

		SaveProgression();

		if (FUTAnalytics::IsAvailable())
		{
			TArray<FAnalyticsEventAttribute> ParamArray;
			ParamArray.Add(FAnalyticsEventAttribute(TEXT("ChallengeTag"), ChallengeTag.ToString()));
			ParamArray.Add(FAnalyticsEventAttribute(TEXT("Stars"), Stars));
			ParamArray.Add(FAnalyticsEventAttribute(TEXT("TotalStars"), AllStars));
			FUTAnalytics::GetProvider().RecordEvent(TEXT("ChallengeComplete"), ParamArray);
		}
	}
}

bool UUTLocalPlayer::QuickMatchCheckFull()
{
#if !UE_SERVER
	if (QuickMatchDialog.IsValid())
	{
		FTimerHandle TmpHandle;
		GetWorld()->GetTimerManager().SetTimer(TmpHandle, this, &UUTLocalPlayer::RestartQuickMatch, 0.5f, false);
		return true;
	}
#endif
	return false;
}

void UUTLocalPlayer::RestartQuickMatch()
{
#if !UE_SERVER
	if (QuickMatchDialog.IsValid())
	{
		// Restart the quickmatch attempt.
		QuickMatchDialog->FindHUBToJoin();
	}
#endif
}

void UUTLocalPlayer::OnReadTitleFileComplete(bool bWasSuccessful, const FString& Filename)
{
	if (Filename == GetMCPStorageFilename())
	{
		FString JsonString = TEXT("");
		if (bWasSuccessful)
		{

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
			if (FParse::Param(FCommandLine::Get(), TEXT("localUTMCPS")))
			{
				FString Path = FPaths::GameContentDir() + TEXT("EpicInternal/MCP/UnrealTournmentMCPStorage.json");
				FFileHelper::LoadFileToString(JsonString, *Path);
			}
#endif
			if (JsonString.IsEmpty())
			{
				TArray<uint8> FileContents;
				OnlineTitleFileInterface->GetFileContents(GetMCPStorageFilename(), FileContents);
				FileContents.Add(0);
				JsonString = ANSI_TO_TCHAR((char*)FileContents.GetData());
			}
		}

		if (JsonString != TEXT(""))
		{
			FMCPPulledData PulledData;
			if ( FJsonObjectConverter::JsonObjectStringToUStruct(JsonString, &PulledData, 0,0) )
			{
				MCPPulledData = PulledData;
				MCPPulledData.bValid = true;

				UUTGameEngine* GameEngine = Cast<UUTGameEngine>(GEngine);
				if ( GameEngine && GameEngine->GetChallengeManager().IsValid() )
				{
					GameEngine->GetChallengeManager()->UpdateChallengeFromMCP(MCPPulledData);

					// Check to see if we have a new daily challenge.  And if we do, update the profile.
					if (GameEngine->GetChallengeManager()->CheckDailyChallenge(CurrentProgression))
					{
						SaveProfileSettings();
					}
				}
			}
		}
	}
}


void UUTLocalPlayer::ShowAdminDialog(AUTRconAdminInfo* AdminInfo)
{
#if !UE_SERVER

	if (ViewportClient->ViewportConsole)
	{
		ViewportClient->ViewportConsole->FakeGotoState(NAME_None);
	}
	
	if (!AdminDialog.IsValid())
	{
		SAssignNew(AdminDialog,SUTAdminDialog)
			.PlayerOwner(this)
			.AdminInfo(AdminInfo);

		if ( AdminDialog.IsValid() ) 
		{
			OpenDialog(AdminDialog.ToSharedRef(),200);
		}
	}
#endif
}

void UUTLocalPlayer::AdminDialogClosed()
{
#if !UE_SERVER
	if (AdminDialog.IsValid())
	{
		AdminDialog.Reset();
	}
#endif
}

bool UUTLocalPlayer::ShowDownloadDialog(bool bTransitionWhenDone)
{
#if !UE_SERVER
	if (!DownloadAllDialog.IsValid())
	{
		SAssignNew(DownloadAllDialog, SUTDownloadAllDialog)			
			.PlayerOwner(this)
			.bTransitionWhenDone(bTransitionWhenDone);

		if (DownloadAllDialog.IsValid())
		{
			OpenDialog(DownloadAllDialog.ToSharedRef(),210);
			return true;
		}
	}
#endif

	return false;
}

void UUTLocalPlayer::DownloadAll()
{
#if !UE_SERVER
	AUTLobbyPC* PC = Cast<AUTLobbyPC>(PlayerController);
	if (PC)
	{
		PC->ServerSendRedirectCount();

		if ( ShowDownloadDialog(false) )
		{
			PC->GetAllRedirects(DownloadAllDialog);
		}

	}
#endif
}
// I need to revist closing dialogs and make it require less code.
void UUTLocalPlayer::CloseDownloadAll()
{
#if !UE_SERVER
	if (DownloadAllDialog.IsValid())
	{
		DownloadAllDialog.Reset();
	}
#endif
}

#if !UE_SERVER

int32 UUTLocalPlayer::NumDialogsOpened()
{
	int32 Num = OpenDialogs.Num();
	if (LoginDialog.IsValid()) Num++;
	return Num;
}
#endif

bool UUTLocalPlayer::SkipWorldRender()
{
#if !UE_SERVER

	if (DesktopSlateWidget.IsValid() && DesktopSlateWidget->SkipWorldRender())
	{
		return true;
	}

	if (GetSummaryPanel().IsValid())
	{
		return true;
	}
	for (auto& Dialog : OpenDialogs)
	{
		if (Dialog.IsValid() && Dialog.Get()->bSkipWorldRender)
		{
			return true;
		}
	}
#endif
	return false;
}

void UUTLocalPlayer::OpenSpectatorWindow()
{
#if !UE_SERVER
	if (!SpectatorWidget.IsValid())
	{
		SAssignNew(SpectatorWidget, SUTSpectatorWindow, this)
			.bShadow(false);

		if (SpectatorWidget.IsValid())
		{
			OpenWindow(SpectatorWidget);
		}
	}
#endif
}
void UUTLocalPlayer::CloseSpectatorWindow()
{
#if !UE_SERVER
	if (SpectatorWidget.IsValid())
	{
		CloseWindow(SpectatorWidget);
		SpectatorWidget.Reset();
	}
#endif
}

bool UUTLocalPlayer::IsFragCenterNew()
{
	if (MCPPulledData.bValid)
	{
		return FragCenterCounter != MCPPulledData.FragCenterCounter;
	}

	return false;
}

void UUTLocalPlayer::UpdateFragCenter()
{
	if (IsFragCenterNew())
	{
		FragCenterCounter = MCPPulledData.FragCenterCounter;
		SaveConfig();
	}
}

FUniqueNetIdRepl UUTLocalPlayer::GetGameAccountId() const
{
	if (OnlineIdentityInterface.IsValid())
	{
		// Not multi-screen compatible
		return FUniqueNetIdRepl(OnlineIdentityInterface->GetUniquePlayerId(0));
	}
	else
	{
		check(0);
		return FUniqueNetIdRepl();
	}
}

bool UUTLocalPlayer::IsEarningXP() const
{
	return true; // we rely on the backend to cap or disallow XP as appropriate
}

void UUTLocalPlayer::PostInitProperties()
{
	Super::PostInitProperties();
	if (!IsTemplate())
	{
#if WITH_PROFILE
		if (McpProfileManager == nullptr)
		{
			McpProfileManager = NewObject<UUtMcpProfileManager>(this);
			ActiveMcpProfileManager = McpProfileManager;
		}
#endif
	}
}

#if WITH_PROFILE

UUtMcpProfileManager* UUTLocalPlayer::GetMcpProfileManager(const FString& AccountId)
{
	if ((AccountId == GetMcpProfileManager()->GetAccountId()) || AccountId.IsEmpty())
	{
		return GetMcpProfileManager();
	}

	// Decided to not use map because not expecting very many shared profiles, plus maps are not GC'd.
	for (auto Manager : SharedMcpProfileManager)
	{
		if (AccountId == Cast<UUtMcpProfileManager>(Manager)->GetAccountId())
		{
			return Cast<UUtMcpProfileManager>(Manager);
		}
	}

	/*
	// To prevent many checks from happening while testing non MCP maps, this is used to create a dummy profile manager.
	if (NullProfileManager == nullptr)
	{
		UE_LOG(UT, Error, TEXT("UUtLocalPlayer: Invalid Profile Manager request %s"), *AccountId);
		NullProfileManager = NewObject<UUtMcpProfileManager>(this);
		NullProfileManager->OfflineInit();
		ActiveMcpProfileManager = NullProfileManager;
	}
	return NullProfileManager;
	*/

	return nullptr;
}

#endif

void UUTLocalPlayer::OnProfileManagerInitComplete(bool bSuccess, const FText& ErrorText)
{
	if (bSuccess)
	{
		UpdateSharedProfiles();
	}
	else
	{
		// If failure, it is handled here, otherwise success callback when done is in UpdateSharedProfiles() - so that it can be call independently.
		UpdateSharedProfilesComplete.ExecuteIfBound(false, ErrorText);
		UpdateSharedProfilesComplete.Unbind();
	}
}

void UUTLocalPlayer::UpdateSharedProfiles()
{
#if WITH_PROFILE
	UUtMcpProfile* McpProfile = GetMcpProfileManager()->GetMcpProfileAs<UUtMcpProfile>(EUtMcpProfile::Profile);
	TSharedPtr<const FJsonValue> SharedAccounts = McpProfile->GetStat(TEXT("sharedAccounts"));
	if (SharedAccounts.IsValid())
	{
		const TArray< TSharedPtr<FJsonValue> >& AccountsArray = SharedAccounts->AsArray();

		int32 SharedProfileIndex = SharedMcpProfileManager.Num();
		if (AccountsArray.Num() > SharedProfileIndex)
		{
			FString McpAccountId = AccountsArray[SharedProfileIndex]->AsString();
			
			UUtMcpProfileManager* NewManager = NewObject<UUtMcpProfileManager>(this);
			SharedMcpProfileManager.Add(NewManager);
			TSharedPtr< const FUniqueNetId > ProfileNetID = MakeShareable(new FUniqueNetIdString(McpAccountId));

			TSharedPtr<const FUniqueNetId> UserId = nullptr;
			if (OnlineIdentityInterface.IsValid())
			{
				UserId = OnlineIdentityInterface->GetUniquePlayerId(GetControllerId());
			}

			NewManager->Init(ProfileNetID, UserId, TEXT("Clan"), FUTProfilesLoaded::CreateUObject(this, &UUTLocalPlayer::OnProfileManagerInitComplete));
			return;

		}
	}

	// bind the delegate that will tell us about profile notifications
	McpProfile->OnHandleNotification().BindUObject(this, &UUTLocalPlayer::HandleProfileNotification);

	// All profiles are loaded, not sure what to stuff into LoginResults here
	FText LoginResults;
	UpdateSharedProfilesComplete.ExecuteIfBound(true, LoginResults);
	UpdateSharedProfilesComplete.Unbind();
#endif
}

void UUTLocalPlayer::UpdateSharedProfiles(const FUTProfilesLoaded& Callback)
{
	UpdateSharedProfilesComplete = Callback;
	UpdateSharedProfiles();
}

void UUTLocalPlayer::HandleProfileNotification(const FOnlineNotification& Notification)
{
	if (Notification.TypeStr == TEXT("XPProgress"))
	{
		FXPProgressNotifyPayload Payload;
		Notification.ParsePayload(Payload, Notification.TypeStr);
		AUTPlayerController* PC = Cast<AUTPlayerController>(PlayerController);
		if (PC != NULL)
		{
			PC->XPBreakdown = FNewScoreXP(float(Payload.XP - Payload.PrevXP));
		}
	}
	else if (Notification.TypeStr == TEXT("LevelUpReward"))
	{
#if WITH_PROFILE
		FLevelUpRewardNotifyPayload Payload;
		Notification.ParsePayload(Payload, Notification.TypeStr);
		const UUTProfileItem* ProfileItem = Cast<UUTProfileItem>(GetMcpProfileManager()->GetMcpProfileAs<UUtMcpProfile>(EUtMcpProfile::Profile)->GetItemTemplateObject(UUTProfileItem::StaticClass(), Payload.RewardID));
		if (ProfileItem != NULL)
		{
			AUTPlayerController* PC = Cast<AUTPlayerController>(PlayerController);
			if (PC != NULL)
			{
				PC->LevelRewards.Add(ProfileItem);
			}
			else
			{
				ShowToast(FText::Format(NSLOCTEXT("UTLocalPlayer", "GotItem", "Received item {0}"), ProfileItem->DisplayName));
			}
		}
#endif
	}
}

void UUTLocalPlayer::CachePassword(FString HostAddress, FString Password, bool bSpectator)
{
	if (bSpectator)
	{
		if (CachedSpecPasswords.Contains(HostAddress))
		{
			CachedSpecPasswords[HostAddress] = Password;
		}
		else
		{
			CachedSpecPasswords.Add(HostAddress, Password);
		}
	}
	else
	{
		if (CachedPasswords.Contains(HostAddress))
		{
			CachedPasswords[HostAddress] = Password;
		}
		else
		{
			CachedPasswords.Add(HostAddress, Password);
		}
	}
}

FString UUTLocalPlayer::RetrievePassword(FString HostAddress, bool bSpectator)
{
	if (bSpectator)
	{
		if (CachedSpecPasswords.Contains(HostAddress))
		{
			return FString::Printf(TEXT("%s"), *CachedSpecPasswords[HostAddress]);
		}
	}
	else if (CachedPasswords.Contains(HostAddress))
	{
		return FString::Printf(TEXT("%s"), *CachedPasswords[HostAddress]);
	}
	return TEXT("");
}


void UUTLocalPlayer::Reconnect(bool bSpectator)
{
	if (LastSession.IsValid())
	{
		JoinSession(LastSession, bSpectator);
	}
	else
	{
		FString Password = bWantsToConnectAsSpectator ? "?specpassword=" : "?password=";
		Password = Password + RetrievePassword(LastConnectToIP, bSpectator);
		ConsoleCommand(TEXT("open ") + LastConnectToIP + Password);
	}
}

void UUTLocalPlayer::EpicFlagCheck()
{
	if (CurrentProfileSettings != NULL && CommunityRole == EUnrealRoles::Developer && 
			CurrentProfileSettings->CountryFlag == FName(TEXT("Unreal")) && !CurrentProfileSettings->bForcedToEpicAtLeastOnce)
	{
		CurrentProfileSettings->CountryFlag = FName(TEXT("Epic"));
		CurrentProfileSettings->bForcedToEpicAtLeastOnce = true;
		SaveProfileSettings();
	}
}


