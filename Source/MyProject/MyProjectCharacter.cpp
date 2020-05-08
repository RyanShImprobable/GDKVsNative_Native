// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "MyProjectCharacter.h"
#include "MyProjectProjectile.h"
#include "Animation/AnimInstance.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/InputComponent.h"
#include "GameFramework/InputSettings.h"
#include "HeadMountedDisplayFunctionLibrary.h"
#include "Kismet/GameplayStatics.h"
#include "MotionControllerComponent.h"
#include "XRMotionControllerBase.h" // for FXRMotionControllerBase::RightHandSourceId
#include <Runtime\Engine\Public\Net\UnrealNetwork.h>

DEFINE_LOG_CATEGORY_STATIC(LogFPChar, Warning, All);

//////////////////////////////////////////////////////////////////////////
// AMyProjectCharacter

AMyProjectCharacter::AMyProjectCharacter()
{
	// Set size for collision capsule
	GetCapsuleComponent()->InitCapsuleSize(55.f, 96.0f);

	// set our turn rates for input
	BaseTurnRate = 45.f;
	BaseLookUpRate = 45.f;

	// Create a CameraComponent	
	FirstPersonCameraComponent = CreateDefaultSubobject<UCameraComponent>(TEXT("FirstPersonCamera"));
	FirstPersonCameraComponent->SetupAttachment(GetCapsuleComponent());
	FirstPersonCameraComponent->SetRelativeLocation(FVector(-39.56f, 1.75f, 64.f)); // Position the camera
	FirstPersonCameraComponent->bUsePawnControlRotation = true;

	// Create a mesh component that will be used when being viewed from a '1st person' view (when controlling this pawn)
	Mesh1P = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("CharacterMesh1P"));
	Mesh1P->SetOnlyOwnerSee(true);
	Mesh1P->SetupAttachment(FirstPersonCameraComponent);
	Mesh1P->bCastDynamicShadow = false;
	Mesh1P->CastShadow = false;
	Mesh1P->SetRelativeRotation(FRotator(1.9f, -19.19f, 5.2f));
	Mesh1P->SetRelativeLocation(FVector(-0.5f, -4.4f, -155.7f));

	// Create a gun mesh component
	FP_Gun = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("FP_Gun"));
	FP_Gun->SetOnlyOwnerSee(true);			// only the owning player will see this mesh
	FP_Gun->bCastDynamicShadow = false;
	FP_Gun->CastShadow = false;
	// FP_Gun->SetupAttachment(Mesh1P, TEXT("GripPoint"));
	FP_Gun->SetupAttachment(RootComponent);

	FP_MuzzleLocation = CreateDefaultSubobject<USceneComponent>(TEXT("MuzzleLocation"));
	FP_MuzzleLocation->SetupAttachment(FP_Gun);
	FP_MuzzleLocation->SetRelativeLocation(FVector(0.2f, 48.4f, -10.6f));

	// Default offset from the character location for projectiles to spawn
	GunOffset = FVector(100.0f, 0.0f, 10.0f);

	// Note: The ProjectileClass and the skeletal mesh/anim blueprints for Mesh1P, FP_Gun, and VR_Gun 
	// are set in the derived blueprint asset named MyCharacter to avoid direct content references in C++.

	// Create VR Controllers.
	R_MotionController = CreateDefaultSubobject<UMotionControllerComponent>(TEXT("R_MotionController"));
	R_MotionController->MotionSource = FXRMotionControllerBase::RightHandSourceId;
	R_MotionController->SetupAttachment(RootComponent);
	L_MotionController = CreateDefaultSubobject<UMotionControllerComponent>(TEXT("L_MotionController"));
	L_MotionController->SetupAttachment(RootComponent);

	// Create a gun and attach it to the right-hand VR controller.
	// Create a gun mesh component
	VR_Gun = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("VR_Gun"));
	VR_Gun->SetOnlyOwnerSee(true);			// only the owning player will see this mesh
	VR_Gun->bCastDynamicShadow = false;
	VR_Gun->CastShadow = false;
	VR_Gun->SetupAttachment(R_MotionController);
	VR_Gun->SetRelativeRotation(FRotator(0.0f, -90.0f, 0.0f));

	VR_MuzzleLocation = CreateDefaultSubobject<USceneComponent>(TEXT("VR_MuzzleLocation"));
	VR_MuzzleLocation->SetupAttachment(VR_Gun);
	VR_MuzzleLocation->SetRelativeLocation(FVector(0.000004, 53.999992, 10.000000));
	VR_MuzzleLocation->SetRelativeRotation(FRotator(0.0f, 90.0f, 0.0f));		// Counteract the rotation of the VR gun model.

	// Uncomment the following line to turn motion controllers on by default:
	//bUsingMotionControllers = true;

	Var1 = 0;
	Var2 = 0;
}

void AMyProjectCharacter::BeginPlay()
{
	// Call the base class  
	Super::BeginPlay();

	//Attach gun mesh component to Skeleton, doing it here because the skeleton is not yet created in the constructor
	FP_Gun->AttachToComponent(Mesh1P, FAttachmentTransformRules(EAttachmentRule::SnapToTarget, true), TEXT("GripPoint"));

	// Show or hide the two versions of the gun based on whether or not we're using motion controllers.
	if (bUsingMotionControllers)
	{
		VR_Gun->SetHiddenInGame(false, true);
		Mesh1P->SetHiddenInGame(true, true);
	}
	else
	{
		VR_Gun->SetHiddenInGame(true, true);
		Mesh1P->SetHiddenInGame(false, true);
	}
}

//////////////////////////////////////////////////////////////////////////
// Input

void AMyProjectCharacter::SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent)
{
	// set up gameplay key bindings
	check(PlayerInputComponent);

	// Bind jump events
	PlayerInputComponent->BindAction("Jump", IE_Pressed, this, &ACharacter::Jump);
	PlayerInputComponent->BindAction("Jump", IE_Released, this, &ACharacter::StopJumping);

	// Bind fire event
	PlayerInputComponent->BindAction("Fire", IE_Pressed, this, &AMyProjectCharacter::OnFire);

	// Enable touchscreen input
	EnableTouchscreenMovement(PlayerInputComponent);

	PlayerInputComponent->BindAction("ResetVR", IE_Pressed, this, &AMyProjectCharacter::OnResetVR);

	// Bind movement events
	PlayerInputComponent->BindAxis("MoveForward", this, &AMyProjectCharacter::MoveForward);
	PlayerInputComponent->BindAxis("MoveRight", this, &AMyProjectCharacter::MoveRight);

	// We have 2 versions of the rotation bindings to handle different kinds of devices differently
	// "turn" handles devices that provide an absolute delta, such as a mouse.
	// "turnrate" is for devices that we choose to treat as a rate of change, such as an analog joystick
	PlayerInputComponent->BindAxis("Turn", this, &APawn::AddControllerYawInput);
	PlayerInputComponent->BindAxis("TurnRate", this, &AMyProjectCharacter::TurnAtRate);
	PlayerInputComponent->BindAxis("LookUp", this, &APawn::AddControllerPitchInput);
	PlayerInputComponent->BindAxis("LookUpRate", this, &AMyProjectCharacter::LookUpAtRate);

	PlayerInputComponent->BindAction("Execute", IE_Pressed, this, &AMyProjectCharacter::Execute);
	PlayerInputComponent->BindAction("Initializing_TT_REPLICATION_ROUNDTRIP", IE_Pressed, this, &AMyProjectCharacter::Initializing_TT_REPLICATION_ROUNDTRIP_Env);
}

void AMyProjectCharacter::OnFire()
{
	// try and fire a projectile
	if (ProjectileClass != NULL)
	{
		UWorld* const World = GetWorld();
		if (World != NULL)
		{
			if (bUsingMotionControllers)
			{
				const FRotator SpawnRotation = VR_MuzzleLocation->GetComponentRotation();
				const FVector SpawnLocation = VR_MuzzleLocation->GetComponentLocation();
				World->SpawnActor<AMyProjectProjectile>(ProjectileClass, SpawnLocation, SpawnRotation);
			}
			else
			{
				const FRotator SpawnRotation = GetControlRotation();
				// MuzzleOffset is in camera space, so transform it to world space before offsetting from the character location to find the final muzzle position
				const FVector SpawnLocation = ((FP_MuzzleLocation != nullptr) ? FP_MuzzleLocation->GetComponentLocation() : GetActorLocation()) + SpawnRotation.RotateVector(GunOffset);

				//Set Spawn Collision Handling Override
				FActorSpawnParameters ActorSpawnParams;
				ActorSpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButDontSpawnIfColliding;

				// spawn the projectile at the muzzle
				World->SpawnActor<AMyProjectProjectile>(ProjectileClass, SpawnLocation, SpawnRotation, ActorSpawnParams);
			}
		}
	}

	// try and play the sound if specified
	if (FireSound != NULL)
	{
		UGameplayStatics::PlaySoundAtLocation(this, FireSound, GetActorLocation());
	}

	// try and play a firing animation if specified
	if (FireAnimation != NULL)
	{
		// Get the animation object for the arms mesh
		UAnimInstance* AnimInstance = Mesh1P->GetAnimInstance();
		if (AnimInstance != NULL)
		{
			AnimInstance->Montage_Play(FireAnimation, 1.f);
		}
	}
}

void AMyProjectCharacter::OnResetVR()
{
	UHeadMountedDisplayFunctionLibrary::ResetOrientationAndPosition();
}

void AMyProjectCharacter::BeginTouch(const ETouchIndex::Type FingerIndex, const FVector Location)
{
	if (TouchItem.bIsPressed == true)
	{
		return;
	}
	if ((FingerIndex == TouchItem.FingerIndex) && (TouchItem.bMoved == false))
	{
		OnFire();
	}
	TouchItem.bIsPressed = true;
	TouchItem.FingerIndex = FingerIndex;
	TouchItem.Location = Location;
	TouchItem.bMoved = false;
}

void AMyProjectCharacter::EndTouch(const ETouchIndex::Type FingerIndex, const FVector Location)
{
	if (TouchItem.bIsPressed == false)
	{
		return;
	}
	TouchItem.bIsPressed = false;
}

//Commenting this section out to be consistent with FPS BP template.
//This allows the user to turn without using the right virtual joystick

//void AMyProjectCharacter::TouchUpdate(const ETouchIndex::Type FingerIndex, const FVector Location)
//{
//	if ((TouchItem.bIsPressed == true) && (TouchItem.FingerIndex == FingerIndex))
//	{
//		if (TouchItem.bIsPressed)
//		{
//			if (GetWorld() != nullptr)
//			{
//				UGameViewportClient* ViewportClient = GetWorld()->GetGameViewport();
//				if (ViewportClient != nullptr)
//				{
//					FVector MoveDelta = Location - TouchItem.Location;
//					FVector2D ScreenSize;
//					ViewportClient->GetViewportSize(ScreenSize);
//					FVector2D ScaledDelta = FVector2D(MoveDelta.X, MoveDelta.Y) / ScreenSize;
//					if (FMath::Abs(ScaledDelta.X) >= 4.0 / ScreenSize.X)
//					{
//						TouchItem.bMoved = true;
//						float Value = ScaledDelta.X * BaseTurnRate;
//						AddControllerYawInput(Value);
//					}
//					if (FMath::Abs(ScaledDelta.Y) >= 4.0 / ScreenSize.Y)
//					{
//						TouchItem.bMoved = true;
//						float Value = ScaledDelta.Y * BaseTurnRate;
//						AddControllerPitchInput(Value);
//					}
//					TouchItem.Location = Location;
//				}
//				TouchItem.Location = Location;
//			}
//		}
//	}
//}

void AMyProjectCharacter::MoveForward(float Value)
{
	if (Value != 0.0f)
	{
		// add movement in that direction
		AddMovementInput(GetActorForwardVector(), Value);
	}
}

void AMyProjectCharacter::MoveRight(float Value)
{
	if (Value != 0.0f)
	{
		// add movement in that direction
		AddMovementInput(GetActorRightVector(), Value);
	}
}

void AMyProjectCharacter::TurnAtRate(float Rate)
{
	// calculate delta for this frame from the rate information
	AddControllerYawInput(Rate * BaseTurnRate * GetWorld()->GetDeltaSeconds());
}

void AMyProjectCharacter::LookUpAtRate(float Rate)
{
	// calculate delta for this frame from the rate information
	AddControllerPitchInput(Rate * BaseLookUpRate * GetWorld()->GetDeltaSeconds());
}

bool AMyProjectCharacter::EnableTouchscreenMovement(class UInputComponent* PlayerInputComponent)
{
	if (FPlatformMisc::SupportsTouchInput() || GetDefault<UInputSettings>()->bUseMouseForTouch)
	{
		PlayerInputComponent->BindTouch(EInputEvent::IE_Pressed, this, &AMyProjectCharacter::BeginTouch);
		PlayerInputComponent->BindTouch(EInputEvent::IE_Released, this, &AMyProjectCharacter::EndTouch);

		//Commenting this out to be more consistent with FPS BP template.
		//PlayerInputComponent->BindTouch(EInputEvent::IE_Repeat, this, &AMyProjectCharacter::TouchUpdate);
		return true;
	}
	
	return false;
}

// Called every frame
void AMyProjectCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (HasAuthority())
	{
		if (varModCount > 0)
		{
			if (varModCount++ % 1 == 0)		// yunjie: one modification every frames which means 30 times per second
			{
				Var1 += 1;
				Var2 += 1;
			}

			if (varModCount >= (30 * 10))
			{
				varModCount = 0;		// yunjie: disable it

				int ball = 0;
				pingpongTestClient(ball);
			}
		}
	}

}

void AMyProjectCharacter::Execute()
{
	if (gTestType == TT_REPLICATION_ROUNDTRIP)
	{
		start_time = FDateTime::UtcNow().GetSecond() * 1000 + FDateTime::UtcNow().GetMillisecond();
		Interact_S2C_VarRep();
	}
}

void AMyProjectCharacter::Interact_S2C_VarRep()
{
	if (!HasAuthority())
	{
		UE_LOG(LogTemp, Warning, TEXT("AMyCharacter::Interact_S2C_VarRep"));

		FString s = TEXT("jsdkfjsklfja;klsdf");
		ServerInteract_S2C_VarRep(s);
	}
}

void AMyProjectCharacter::NotifyClientRoundTripDone_Implementation(const int& var1_server, const int& var2_server)
{
	if (HasAuthority())
		return;
	UE_LOG(LogTemp, Warning, TEXT("RyanIsComing!!!!!!!!!!!!!!!!!!!!!!"));
	UE_LOG(LogTemp, Warning, TEXT("NotifyClientRoundTripDone_Implementation Var1 From server:[%d], Var2 From server:[%d]"), var1_server, var2_server);
	UE_LOG(LogTemp, Warning, TEXT("NotifyClientRoundTripDone_Implementation Var1 CLient:[%d], Var2 Client:[%d]"), Var1, Var2);
	int end_time = 0;
	end_time = FDateTime::UtcNow().GetSecond() * 1000 + FDateTime::UtcNow().GetMillisecond();
	UE_LOG(LogTemp, Warning, TEXT("Start time is [%d], End time is [%d]"), start_time, end_time);
	UE_LOG(LogTemp, Warning, TEXT("AMyCharacter::NotifyClientRoundTripDone_Implementation Total_time:[%d]"), end_time
		- start_time);
}

bool AMyProjectCharacter::ServerInitializing_TT_REPLICATION_ROUNDTRIP_Env_Validate(const FServerInitData& data)
{
	return true;
}

void AMyProjectCharacter::ServerInitializing_TT_REPLICATION_ROUNDTRIP_Env_Implementation(const FServerInitData& data)
{
	gTestType = data.testType;
	//InitGlobalVariables();

	UE_LOG(LogTemp, Warning, TEXT("AMyCharacter::ServerInitializing_TT_REPLICATION_ROUNDTRIP_Env_Implementation, testType:[%d]"), data.testType);
}

void AMyProjectCharacter::Initializing_TT_REPLICATION_ROUNDTRIP_Env()
{
	if (!HasAuthority())
	{
		gTestType = TT_REPLICATION_ROUNDTRIP;
		//InitGlobalVariables();

		FServerInitData data;
		data.testType = gTestType;
		ServerInitializing_TT_REPLICATION_ROUNDTRIP_Env(data);
	}
}

void AMyProjectCharacter::ServerInteract_S2C_VarRep_Implementation(const FString& s)
{
	if (HasAuthority())
	{
		UE_LOG(LogTemp, Warning, TEXT("AMyCharacter::ServerInteract_S2C_VarRep_Implementation"));
		varModCount = 1;		// yunjie: enable tick to do the variables modification
	}
}

bool AMyProjectCharacter::ServerInteract_S2C_VarRep_Validate(const FString& s)
{
	return true;
}

void AMyProjectCharacter::OnRepVar1()
{
	UE_LOG(LogTemp, Error, TEXT("Ryan OnRepVar1:[%d]"), Var1);
	if (Var1 == 299) {
		UE_LOG(LogTemp, Warning, TEXT("Variable Replication is done!"));
		int end_time = 0;
		end_time = FDateTime::UtcNow().GetSecond() * 1000 + FDateTime::UtcNow().GetMillisecond();
		UE_LOG(LogTemp, Warning, TEXT("Ayunjie_gdk_testCharacter::Replication Total_time:[%d]"), end_time
			- start_time);
	}
}

void AMyProjectCharacter::OnRepVar2()
{
	//UE_LOG(LogTemp, Error, TEXT("Ryan OnRepVar2:[%d]"), Var2);
}
void AMyProjectCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AMyProjectCharacter, Var1);
	DOREPLIFETIME(AMyProjectCharacter, Var2);
}

void AMyProjectCharacter::pingpongTestServer_Implementation(int ball)
{
	if (!HasAuthority())
		return;

	ball += 1;
	pingpongTestClient(ball);
	UE_LOG(LogTemp, Warning, TEXT("Ayunjie_gdk_testCharacter::pingpongTestServer_Implementation ball is %d."), ball);

}

void AMyProjectCharacter::pingpongTestClient_Implementation(int ball)
{
	if (HasAuthority())
		return;

	if (ball == 0) {
		rpc_start_time = FDateTime::UtcNow().GetSecond() * 1000 + FDateTime::UtcNow().GetMillisecond();
	}

	if (ball == 500) {
		int rpc_end_time = FDateTime::UtcNow().GetSecond() * 1000 + FDateTime::UtcNow().GetMillisecond();
		UE_LOG(LogTemp, Warning, TEXT("Ayunjie_gdk_testCharacter::pingpongTestClient_Implementation ball Reached %d !!!!"), ball);
		UE_LOG(LogTemp, Error, TEXT("Ayunjie_gdk_testCharacter::pingpongTestClient_Implementation rpc time lapse is %d !!!!"), rpc_end_time - rpc_start_time);
	}
	else {
		ball += 1;
		pingpongTestServer(ball);
		UE_LOG(LogTemp, Warning, TEXT("Ayunjie_gdk_testCharacter::pingpongTestClient_Implementation ball is %d."), ball);
	}
}
