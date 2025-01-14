// Fill out your copyright notice in the Description page of Project Settings.


#include "PlayerCharacter.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/CharacterMovementComponent.h"

#include "Components/InputComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Roomba.h"
#include "GameFramework/SpringArmComponent.h"
#include "Kismet/GameplayStatics.h"
#include "GrabbableInterface.h"
#include "Net/UnrealNetwork.h"
#include "EngineUtils.h"
#include "MyGameState.h"
#include "AudioManager.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"
#include "DSP/Chorus.h"
#include "PhysicsEngine/PhysicsHandleComponent.h"

// Sets default values
APlayerCharacter::APlayerCharacter()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
    
	// Create a camera boom...
	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->SetUsingAbsoluteRotation(true);
	CameraBoom->TargetArmLength = 500.f;
	CameraBoom->SetRelativeRotation(FRotator(-60.f, 0.f, 0.f));
	CameraBoom->bDoCollisionTest = false;
   
	// Create a camera...
	TopDownCameraComponent = CreateDefaultSubobject<UCameraComponent>(TEXT("TopDownCamera"));
	TopDownCameraComponent->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	TopDownCameraComponent->bUsePawnControlRotation = false;

	// Don't rotate character to camera direction
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = true;
	bUseControllerRotationRoll = false;

	// Configure character movement
	GetCharacterMovement()->bOrientRotationToMovement = true;
	GetCharacterMovement()->RotationRate = FRotator(0.f, 640.f, 0.f);
	GetCharacterMovement()->bConstrainToPlane = true;
	GetCharacterMovement()->bSnapToPlaneAtStart = true;
	
	GetMesh()->SetRelativeLocation(FVector(0.0f, 0.0f, -90.0f));

	/***/
	RightHandSphereCollision = CreateDefaultSubobject<USphereComponent>(TEXT("RightHandSphereCollision"));
	RightHandSphereCollision->SetupAttachment(GetMesh(), "hand_r");
	RightHandSphereCollision->InitSphereRadius(10.0f);

	LeftHandSphereCollision = CreateDefaultSubobject<USphereComponent>(TEXT("LeftHandSphereCollision"));
	LeftHandSphereCollision->SetupAttachment(GetMesh(), "hand_l");
	LeftHandSphereCollision->InitSphereRadius(10.0f);

	LeftPhysicsHandle = CreateDefaultSubobject<UPhysicsHandleComponent>(TEXT("LeftPhysicsHandle"));
	LeftPhysicsHandle->SetTargetLocation(LeftHandSphereCollision->GetComponentLocation());
	RightPhysicsHandle = CreateDefaultSubobject<UPhysicsHandleComponent>(TEXT("RightPhysicsHandle"));
	RightPhysicsHandle->SetTargetLocation(RightHandSphereCollision->GetComponentLocation());

	ConstraintComponent = CreateDefaultSubobject<UPhysicsConstraintComponent>(TEXT("ConstraintComponent"));
	/***/
}

APlayerCharacter::~APlayerCharacter()
{
	//Detach(WidgetInstance);
}

// Called when the game starts or when spawned
void APlayerCharacter::BeginPlay()
{
	Super::BeginPlay();

	UWorld* World = GetWorld();
	
	// GetCharacterMovement()->AirControl = 0.2f;
	GetCharacterMovement()->JumpZVelocity = JumpVelocity;
	bCanJump = true;
	
	RoomManagerVariable = Cast<ARoomsManager>(UGameplayStatics::GetActorOfClass(GetWorld(), ARoomsManager::StaticClass()));

	SlideDoorLeftRef = FindSlideDoorsByTag(TEXT("SlideDoorL"));
	SlideDoorRightRef = FindSlideDoorsByTag(TEXT("SlideDoorR"));
	
	GarageHandlerLeftRef = FindGarageHandlerByTag(TEXT("GarageHandlerL"));
	GarageHandlerRightRef = FindGarageHandlerByTag(TEXT("GarageHandlerR"));

	PowerSystem = Cast<APowerSystem>(UGameplayStatics::GetActorOfClass(GetWorld(), APowerSystem::StaticClass()));
	
	for(ATheTwins* Twin : TActorRange<ATheTwins>(World))
	{
		Twins.Add(Twin);
	}

	if(APlayerController* PlayerController = Cast<APlayerController>(GetController()))
	{
		if(UEnhancedInputLocalPlayerSubsystem* Subsystem =
			ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PlayerController->GetLocalPlayer()))
		{
			Subsystem->ClearAllMappings();
			Subsystem->AddMappingContext(PlayerMappingContext, 0);
		}
	}

	// Bind function to event of Roomba
	ARoomba* Roomba = Cast<ARoomba>(UGameplayStatics::GetActorOfClass(GetWorld(), ARoomba::StaticClass()));
	if (Roomba)
	{
		FScriptDelegate RoombaAttachmentDelegate;
		FScriptDelegate RoombaDetachmentDelegate;
		RoombaAttachmentDelegate.BindUFunction(this, "HandleRoombaAttachedEvent");
		RoombaDetachmentDelegate.BindUFunction(this, "HandleRoombaDetachedEvent");
		Roomba->OnRoombaAttachedEvent.Add(RoombaAttachmentDelegate);
		Roomba->OnRoombaDetachedEvent.Add(RoombaDetachmentDelegate);

		Roomba->GetCharacters();
	}

	// Bind Event to activate UI Widget
	AMyGameState* GameState = Cast<AMyGameState>(UGameplayStatics::GetActorOfClass(GetWorld(), AMyGameState::StaticClass()));
	if (GameState)
	{
		FScriptDelegate ActivateUIDelegate;
		ActivateUIDelegate.BindUFunction(this, "ActivateWidgetEvent");
		//ActivateUIDelegate.BindUFunction(this, "ActivateWidgetEvent");
		GameState->OnActivateUIEvent.Add(ActivateUIDelegate);
	}

	// Create and Add Widget to Viewport
	if (Widget)
	{
		APlayerController* PlayerController = Cast<APlayerController>(GetController());
		WidgetInstance = CreateWidget<UEndOfGameUI>(PlayerController, Widget);

		if (WidgetInstance)
		{
			// Add widget to observer list
			Attach(WidgetInstance);
			
			// Add Widget to Viewport
			WidgetInstance->AddToViewport();
			// Hide Widget
			WidgetInstance->SetVisibility(ESlateVisibility::Hidden);
		}
	}
}

// Called every frame
void APlayerCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}

void APlayerCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(APlayerCharacter, MovementSpeed);
}

void APlayerCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	if (UEnhancedInputComponent* PlayerEnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		if (MoveInputAction)
		{
			PlayerEnhancedInputComponent->BindAction(MoveInputAction, ETriggerEvent::Triggered,this, &APlayerCharacter::OnMove);
		}

		if (JumpInputAction)
		{
			PlayerEnhancedInputComponent->BindAction(JumpInputAction, ETriggerEvent::Started, this, &ACharacter::Jump);
			PlayerEnhancedInputComponent->BindAction(JumpInputAction, ETriggerEvent::Completed, this, &ACharacter::StopJumping);
		}
		
		/*
		if (GrabInputAction)
		{
			PlayerEnhancedInputComponent->BindAction(GrabInputAction, ETriggerEvent::Triggered, this, &APlayerCharacter::OnGrab);
		}
		*/
		// if (LeftHandGrabInputAction)
		// {
		// 	PlayerEnhancedInputComponent->BindAction(LeftHandGrabInputAction, ETriggerEvent::Triggered, this, &APlayerCharacter::Grabbing);
		// 	PlayerEnhancedInputComponent->BindAction(LeftHandGrabInputAction, ETriggerEvent::Completed, this, &APlayerCharacter::Release);
		// }
		
		if (RightHandGrabInputAction)
		{
			PlayerEnhancedInputComponent->BindAction(RightHandGrabInputAction, ETriggerEvent::Triggered, this, &APlayerCharacter::Grabbing);
			PlayerEnhancedInputComponent->BindAction(RightHandGrabInputAction, ETriggerEvent::Completed, this, &APlayerCharacter::Release);
		}
		
		if (TestDebugInputAction) 
		{
			//PlayerEnhancedInputComponent->BindAction(TestDebugInputAction, ETriggerEvent::Triggered, this, &APlayerCharacter::CallRoomManagerDebugFunctions);
		}

		if (CloseLeftSlideDoor)
		{
			PlayerEnhancedInputComponent->BindAction(CloseLeftSlideDoor, ETriggerEvent::Started, this, &APlayerCharacter::OnCloseLeftSlideDoor);
			PlayerEnhancedInputComponent->BindAction(CloseLeftSlideDoor, ETriggerEvent::Completed, this, &APlayerCharacter::OnCloseLeftSlideDoorEnd);
		}

		if (CloseRightSlideDoor)
		{
			PlayerEnhancedInputComponent->BindAction(CloseRightSlideDoor, ETriggerEvent::Started, this, &APlayerCharacter::OnCloseRightSlideDoor);
			PlayerEnhancedInputComponent->BindAction(CloseRightSlideDoor, ETriggerEvent::Completed, this, &APlayerCharacter::OnCloseRightSlideDoorEnd);
		}
		
		if (UseLeftHandler)
		{
			PlayerEnhancedInputComponent->BindAction(UseLeftHandler, ETriggerEvent::Started, this, &APlayerCharacter::OnUseLeftHandler);
			PlayerEnhancedInputComponent->BindAction(UseLeftHandler, ETriggerEvent::Completed, this, &APlayerCharacter::OnUseLeftHandlerEnd);
		}

		if (UseRightHandler)
		{
			PlayerEnhancedInputComponent->BindAction(UseRightHandler, ETriggerEvent::Started, this, &APlayerCharacter::OnUseRightHandler);
			PlayerEnhancedInputComponent->BindAction(UseRightHandler, ETriggerEvent::Completed, this, &APlayerCharacter::OnUseRightHandlerEnd);
		}

		if (IncreasePowerAction)
		{
			PlayerEnhancedInputComponent->BindAction(IncreasePowerAction, ETriggerEvent::Started, this, &APlayerCharacter::OnIncreasePower);
		}

		if (StopTwins)
		{
			PlayerEnhancedInputComponent->BindAction(StopTwins, ETriggerEvent::Started, this, &APlayerCharacter::OnStopTwins);
		}

		if (ResumeTwins)
		{
			PlayerEnhancedInputComponent->BindAction(ResumeTwins, ETriggerEvent::Started, this, &APlayerCharacter::OnResumeTwins);
		}

		if (FGroupAttack)
		{
			PlayerEnhancedInputComponent->BindAction(FGroupAttack, ETriggerEvent::Started, this, &APlayerCharacter::ForceGroupAttack);
		}
	}
}

ASlideDoors* APlayerCharacter::FindSlideDoorsByTag(const FName& DoorTag)
{
	ASlideDoors* DoorRef = nullptr;
	
	UWorld* World = GetWorld();
	for(ASlideDoors* Door : TActorRange<ASlideDoors>(World))
	{
		if (Door->ActorHasTag(DoorTag))
		{
			DoorRef = Cast<ASlideDoors>(Door);
			break;
		}
	}
	return DoorRef;
}

AGarageHandler* APlayerCharacter::FindGarageHandlerByTag(const FName& HandlerTag)
{
	AGarageHandler* HandlerRef = nullptr;

	UWorld* World = GetWorld();
	for(AGarageHandler* GarageHandler : TActorRange<AGarageHandler>(World))
	{
		if (GarageHandler->ActorHasTag(HandlerTag))
		{
			HandlerRef = Cast<AGarageHandler>(GarageHandler);
			break;
		}
	}

	return HandlerRef;
}

void APlayerCharacter::OnMove(const FInputActionValue& Value)
{
	// GEngine->AddOnScreenDebugMessage(-1, 15.0f, FColor::Yellow, TEXT("Your Message"));
	// FInputActionValue::Axis2D Axis = Value.Get<FInputActionValue::Axis2D>();
	FVector2D MovementVector = Value.Get<FVector2D>();
	MovementVector.Normalize();

	if (!MovementVector.IsNearlyZero())
	{
		const FRotator Rotation = Controller->GetControlRotation();
		const FRotator YawRotation(0.f, Rotation.Yaw, 0.f);
	
		const FVector ForwardDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
		AddMovementInput(ForwardDirection, MovementVector.Y * MovementSpeed);
		const FVector RightDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);
		AddMovementInput(RightDirection, MovementVector.X * MovementSpeed);
	}
}

void APlayerCharacter::GrabLeft()
{
	StretchToNearestGrabbable(true);
}

void APlayerCharacter::GrabRight()
{
	StretchToNearestGrabbable(false);
}

void APlayerCharacter::ReleaseLeft()
{
	// if (LeftPhysicsHandle->GrabbedComponent)
	// {
	// 	// LeftPhysicsHandle->ReleaseComponent();
	// 	GrabbedComponent = nullptr;
	// }
	isGrabbing = false;
}

void APlayerCharacter::ReleaseRight()
{
	// if (RightPhysicsHandle->GrabbedComponent)
	// {
	// 	// RightPhysicsHandle->ReleaseComponent();
	// 	GrabbedComponent = nullptr;
	// }
	isGrabbing = false;
}

void APlayerCharacter::Grabbing()
{
    // Perform a sphere sweep to find grabbable objects in the specified direction
	FVector StartLocation = GetActorLocation();
	FVector EndLocation = StartLocation;
	
	FCollisionQueryParams CollisionParams;
    CollisionParams.AddIgnoredActor(this);  // Ignore the player character

    TArray<FHitResult> HitResults;
    GetWorld()->SweepMultiByChannel(HitResults, StartLocation, EndLocation, FQuat::Identity, ECC_Visibility, FCollisionShape::MakeSphere(75.0f), CollisionParams);

	// Draw the red sphere for visualization
    DrawDebugSphere(
        GetWorld(),
        StartLocation,
        75.0f,
        4,
        FColor::Red,
        false,
        5.0f,
        0,
        1
    );

	for (FHitResult& Hit : HitResults)
	{
		if (Hit.Component.IsValid())
		{
			FString ComponentName = Hit.Component->GetName();
			GEngine->AddOnScreenDebugMessage(-1, 0.1f, FColor::Red, ComponentName);
			
			// Check if the owner implements the specific interface
			if (Hit.Component->GetOwner()->GetClass()->ImplementsInterface(UGrabbableInterface::StaticClass()))
			{
				// Check if the component's name is "SlideDoor"
				if (ComponentName == "SlideDoor")
				{
					GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Blue, TEXT("This is a SlideDoor"));
					GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Green, TEXT("This is grabbable"));

					// ConstraintComponent->SetAngularSwing1Limit(EAngularConstraintMotion::ACM_Free, 0.f);
					// ConstraintComponent->SetAngularSwing2Limit(EAngularConstraintMotion::ACM_Locked, 0.f);
					// ConstraintComponent->SetAngularTwistLimit(EAngularConstraintMotion::ACM_Locked, 0.f);
					ConstraintComponent->SetConstrainedComponents(GetMesh(), NAME_None, Hit.GetComponent(), NAME_None);

					IGrabbableInterface* GrabInterface = Cast<IGrabbableInterface>(Hit.GetActor());
					if (GrabInterface && isGrabbing == true)
					{
						AActor* Actor = Hit.GetActor();
						GrabInterface->Execute_Grab(Actor);
						isGrabbing = true;
					}
					
					// Draw the red sphere for visualization
					DrawDebugSphere(
						GetWorld(),
						StartLocation,
						75.0f,
						4,
						FColor::Green,
						false,
						5.0f,
						0,
						1
					);
				}
			}
		}
	}
}

void APlayerCharacter::Release()
{
	isGrabbing = false;
	
	if (isGrabbing == false)
		ConstraintComponent->BreakConstraint();
}

void APlayerCharacter::StretchToNearestGrabbable(bool bIsLeftHand)
{
    // Get the hand sphere component for the specified hand
    USphereComponent* HandSphere = bIsLeftHand ? LeftHandSphereCollision : RightHandSphereCollision;
    UPhysicsHandleComponent* PhysicsHandle = bIsLeftHand ? LeftPhysicsHandle : RightPhysicsHandle;

    // Perform a sphere sweep to find grabbable objects in the specified direction
    FVector StartLocation = HandSphere->GetComponentLocation();
    FVector ForwardVector = GetActorForwardVector();
    FVector EndLocation = StartLocation + ForwardVector * MaxGrabDistance;

    FCollisionQueryParams CollisionParams;
    CollisionParams.AddIgnoredActor(this);  // Ignore the player character

    TArray<FHitResult> HitResults;
    GetWorld()->SweepMultiByChannel(HitResults, StartLocation, EndLocation, FQuat::Identity, ECC_Visibility, FCollisionShape::MakeSphere(HandSphere->GetScaledSphereRadius()), CollisionParams);

	// Draw the red sphere for visualization
    DrawDebugSphere(
        GetWorld(),
        StartLocation,
        HandSphere->GetScaledSphereRadius(),
        1,
        FColor::Red,
        false,
        5.0f,
        0,
        1
    );

	for (FHitResult& Hit : HitResults)
	{
		if (Hit.Component.IsValid())
		{
			FString ComponentName = Hit.Component->GetName();
			GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, ComponentName);
			
			// Check if the owner implements the specific interface
			if (Hit.Component->GetOwner()->GetClass()->ImplementsInterface(UGrabbableInterface::StaticClass()))
			{
				// Check if the component's name is "SlideDoor"
				if (ComponentName == "SlideDoor")
				{
					GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Blue, TEXT("This is a SlideDoor"));
					GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Green, TEXT("This is grabbable"));

					// Get the hand to which you want to attach the "SlideDoor" object
					USceneComponent* Hand = bIsLeftHand ? LeftHandSphereCollision : RightHandSphereCollision;

					// Attach the "SlideDoor" object to the hand
					Hit.Component->AttachToComponent(Hand, FAttachmentTransformRules::SnapToTargetNotIncludingScale, Hand->GetAttachSocketName());
					
					// ConstraintComponent->SetConstrainedComponents(GetMesh(), "spine_05", Hit.GetComponent(), NAME_None);

					isGrabbing = true;
					// GrabObject(Hit.GetComponent(), bIsLeftHand);
					
					// Draw the red sphere for visualization
					DrawDebugSphere(
						GetWorld(),
						StartLocation,
						HandSphere->GetScaledSphereRadius(),
						1,
						FColor::Green,
						false,
						5.0f,
						0,
						1
					);
				}
			}
		}
	}
}

void APlayerCharacter::GrabObject(UPrimitiveComponent* InGrabbedComponent, bool bIsLeftHand)
{
	if (!InGrabbedComponent || !InGrabbedComponent->GetOwner())
	{
		return;
	}

	// Check if the component's name is "SlideDoor"
	if (InGrabbedComponent->GetName() != "SlideDoor")
	{
		return;
	}

	// Store the grabbed component for future reference
	GrabbedComponent = InGrabbedComponent;

	UPhysicsHandleComponent* PhysicsHandle = bIsLeftHand ? LeftPhysicsHandle : RightPhysicsHandle;

	// Attach the grabbed object to the hand
	PhysicsHandle->GrabComponentAtLocation(
		InGrabbedComponent,
		NAME_None,
		InGrabbedComponent->GetComponentLocation()
	);
    
	isGrabbing = true;
}

void APlayerCharacter::Jump()
{
	// Check if Player can jump - First one from APlayerCharacter class (default), second one from us for Roomba
	if (CanJump() && bCanJump)
	{
		Super::Jump();

		PlayCharacterSound(this);

		//Detach the Roomba
		TArray<AActor*> Actors;
		GetOverlappingActors(Actors);
		for(AActor* Actor : Actors)
		{
			IJumpableInterface* JumpableInterface = Cast<IJumpableInterface>(Actor);
			if (JumpableInterface)
				JumpableInterface->Execute_JumpedOn(Actor, this);
		}
	}

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Blue, TEXT("Jump Called"));
	}
}

void APlayerCharacter::StopJumping()
{
	Super::StopJumping();

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(1, 5.f, FColor::Red, TEXT("StopJumping Called"));
	}
}

void APlayerCharacter::CallRoomManagerDebugFunctions()
{
	/*
	if (RoomManagerVariable)
	{
		APlayerController* PlayerController = UGameplayStatics::GetPlayerController(this, 0);

		if (PlayerController)
		{
			// Get the key binding for the DebugAction input action
			bool bIsDebugActionPressed = PlayerController->IsInputKeyDown(EKeys::One);

			// Determine the room ID based on the pressed key
			int32 RoomID = 1;  // Default room ID
			if (bIsDebugActionPressed)
			{
				RoomID = 1;
			}
			// ... repeat for other keys ...

			// Call RoomManager's debug functions with the specified room ID
			RoomManagerVariable->DebugConnectedRooms(RoomID);
			RoomManagerVariable->DebugWaypoints(RoomID);
			RoomManagerVariable->DebugRandomConnectedRoomID(RoomID);
		}
	}
	*/
}

//Julian Code:
void APlayerCharacter::OnCloseLeftSlideDoor_Implementation()
{
	if (SlideDoorLeftRef)
		SlideDoorLeftRef->SetDoorTrue();
	GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Cyan, FString::Printf(TEXT("Close left slide door")));

}

void APlayerCharacter::OnCloseLeftSlideDoorEnd_Implementation()
{
	if (SlideDoorLeftRef)
		SlideDoorLeftRef->SetDoorFalse();
	GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Cyan, FString::Printf(TEXT("Open  left Slide door")));
}

void APlayerCharacter::OnCloseRightSlideDoor_Implementation()
{
	if (SlideDoorRightRef)
		SlideDoorRightRef->SetDoorTrue();
	GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Cyan, FString::Printf(TEXT("Close right slide door")));

}

void APlayerCharacter::OnCloseRightSlideDoorEnd_Implementation()
{
	if (SlideDoorRightRef)
		SlideDoorRightRef->SetDoorFalse();
	GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Cyan, FString::Printf(TEXT("Open right slide door")));

}

void APlayerCharacter::OnUseLeftHandler_Implementation()
{
	if (GarageHandlerLeftRef)
		GarageHandlerLeftRef->SetHandlerTrue();
	GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Cyan, FString::Printf(TEXT("Close left handler door")));

}
void APlayerCharacter::OnUseLeftHandlerEnd_Implementation()
{
	if (GarageHandlerLeftRef)
		GarageHandlerLeftRef->SetHandlerFalse();
	GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Cyan, FString::Printf(TEXT("Open left handler door")));

}
void APlayerCharacter::OnUseRightHandler_Implementation()
{
	if (GarageHandlerRightRef)
		GarageHandlerRightRef->SetHandlerTrue();
	GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Cyan, FString::Printf(TEXT("Close right handler door")));

}
void APlayerCharacter::OnUseRightHandlerEnd_Implementation()
{
	if (GarageHandlerRightRef)
		GarageHandlerRightRef->SetHandlerFalse();
	GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Cyan, FString::Printf(TEXT("Open  right handler door")));

}

void APlayerCharacter::OnIncreasePower_Implementation()
{
	if (PowerSystem)
		PowerSystem->AddPower(100.0f);
}

void APlayerCharacter::OnStopTwins()
{
	for(ATheTwins* Twin : Twins)
	{
		Twin->PauseAllTwinTimers();
	}
}

void APlayerCharacter::OnResumeTwins()
{
	for(ATheTwins* Twin : Twins)
	{
		Twin->ResumeAllTwinTimers();
	}
}

void APlayerCharacter::ForceGroupAttack_Implementation()
{
	GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, "CalledFunction");
	for (ATheTwins* Twin : Twins)
	{
		Twin->ForceGroupATK();
	}
}

//Function that gets triggered through Roomba Event
void APlayerCharacter::HandleRoombaAttachedEvent_Implementation(APlayerCharacter* Character)
{
	if (Character != this) return;
	MovementSpeed = 0.5f;
	bCanJump = false;
}

void APlayerCharacter::HandleRoombaDetachedEvent_Implementation()
{
	MovementSpeed = 1.0f;
	bCanJump = true;
}

void APlayerCharacter::ActivateWidgetEvent_Implementation(const FText& NewText)
{
	if (WidgetInstance)
	{
		Notify(NewText);
	}

	APlayerController* PC = Cast<APlayerController>(GetController());
	if (PC)
	{
		DisableInput(PC);
		PC->bShowMouseCursor = true;
	}
}

void APlayerCharacter::NotifyHit(UPrimitiveComponent* MyComp, AActor* Other, UPrimitiveComponent* OtherComp,
	bool bSelfMoved, FVector HitLocation, FVector HitNormal, FVector NormalImpulse, const FHitResult& Hit)
{
	Super::NotifyHit(MyComp, Other, OtherComp, bSelfMoved, HitLocation, HitNormal, NormalImpulse, Hit);
	float CharacterZVelocity = NormalImpulse.Z;
	
	IJumpableInterface* JumpableInterface = Cast<IJumpableInterface>(Other);
	if (JumpableInterface)
		JumpableInterface->Execute_JumpedOn(Other, this);
	
	// if (NormalImpulse.Y > 0.0f)
	// {
	// 	IJumpableInterface* JumpableInterface = Cast<IJumpableInterface>(Other);
	// 	if (JumpableInterface)
	// 		JumpableInterface->Execute_JumpedOn(Other);
	// }
}

void APlayerCharacter::PlayCharacterSound_Implementation(AActor* Actor)
{
	// Get a reference to the UAudioManager instance.
	UAudioManager& AudioManager = UAudioManager::GetInstance();

	if (CharacterSoundCue)
	{
		GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Green, TEXT("~Played sound"));
		// Play the sound using the UAudioManager.
		AudioManager.PlaySoundAtLocation(CharacterSoundCue, Actor);
	}
	else
	{
		GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, TEXT("APlayerCharacter::PlayCharacterSound - Invalid SoundCue"));
	}
}
