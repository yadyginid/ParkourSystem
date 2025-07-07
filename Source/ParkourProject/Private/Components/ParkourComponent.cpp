#include "ParkourProject/Public/Components/ParkourComponent.h" // Убедитесь, что путь правильный
#include "Components/CapsuleComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Net/UnrealNetwork.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/SpringArmComponent.h"
#include "Widgets/Text/ISlateEditableTextWidget.h"

UParkourComponent::UParkourComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	SetIsReplicatedByDefault(true);
}

void UParkourComponent::BeginPlay()
{
	Super::BeginPlay();
	OwnerCharacter = Cast<ACharacter>(GetOwner());
	if (OwnerCharacter)
	{
		OwnerMovementComponent = OwnerCharacter->GetCharacterMovement();
		OwnerSpringArm = OwnerCharacter->FindComponentByClass<USpringArmComponent>();
	}
	
	if (ParkourTimelineCurve)
	{
		FOnTimelineFloat UpdateFunction;
		UpdateFunction.BindUFunction(this, FName("TimelineUpdate"));
		ParkourTimeline.AddInterpFloat(ParkourTimelineCurve, UpdateFunction);

		FOnTimelineEvent FinishedFunction;
		FinishedFunction.BindUFunction(this, FName("TimelineFinished"));
		ParkourTimeline.SetTimelineFinishedFunc(FinishedFunction);
	}
}

void UParkourComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	ParkourTimeline.TickTimeline(DeltaTime);
}

void UParkourComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UParkourComponent, CurrentParkourState);
}

void UParkourComponent::OnParkourInput()
{
	if (!OwnerCharacter || !OwnerCharacter->IsLocallyControlled() || CurrentParkourState.ActionType != EParkourActionType::None)
	{
		return;
	}
	
	if (OwnerMovementComponent && OwnerMovementComponent->IsFalling())
	{
		OwnerCharacter->Jump();
		return;
	}
	
	FParkourAction DummyAction;
	FVector DummyLocation;
	if (CanParkour(DummyAction, DummyLocation))
	{
		PerformParkour(DummyAction, DummyLocation);
		Server_TryPerformParkour();
	}
	else
	{
		OwnerCharacter->Jump();
	}
}

void UParkourComponent::Server_TryPerformParkour_Implementation()
{
	FParkourAction FoundAction;
	FVector LedgeLocation;
	if (CanParkour(FoundAction, LedgeLocation))
	{
		const UCapsuleComponent* Capsule = OwnerCharacter->GetCapsuleComponent();
		
		const FVector StartPoint = LedgeLocation 
			+ (OwnerCharacter->GetActorForwardVector() * (FoundAction.StartingOffset.X - Capsule->GetScaledCapsuleRadius())) 
			+ FVector(0, 0, FoundAction.StartingOffset.Z);
		
		//const FVector EndPoint = LedgeLocation + FoundAction.LandedOffset;
		FVector EndPoint = LedgeLocation + FVector(FoundAction.LandedOffset.X, FoundAction.LandedOffset.Y, 0.f);
		EndPoint.Z += Capsule->GetScaledCapsuleHalfHeight();
		
		FHitResult SweepHitResult;
		FCollisionQueryParams Params;
		Params.AddIgnoredActor(OwnerCharacter);
		const FCollisionShape CapsuleShape = FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(), Capsule->GetScaledCapsuleHalfHeight());

		const bool bHit = GetWorld()->SweepSingleByChannel(
			SweepHitResult,
			StartPoint,
			StartPoint,
			FQuat::Identity,
			ECC_Visibility,
			CapsuleShape,
			Params
		);
		
		if (bDrawDebugTraces)
		{
			DrawDebugCapsule(GetWorld(), StartPoint, Capsule->GetScaledCapsuleHalfHeight(), Capsule->GetScaledCapsuleRadius(), FQuat::Identity, bHit ? FColor::Red : FColor::Green, false, 5.f, 0, 2.f);
		}
		
		if (bHit)
		{
			AActor* HitActor = SweepHitResult.GetActor();
			UE_LOG(LogTemp, Error, TEXT("!!! SAFETY SWEEP BLOCKED! Hit Actor: [%s], Hit Component: [%s]"), 
				*GetNameSafe(HitActor), 
				*GetNameSafe(SweepHitResult.GetComponent()));
			// ---
			return; 
		}

		
		FReplicatedParkourState NewState;
		NewState.ActionType = FoundAction.ActionType;
		NewState.StartLocation = StartPoint;
		NewState.EndLocation = EndPoint;
		NewState.Montage = FoundAction.Montage;
		NewState.PlayRate = (FoundAction.AnimationCorrectionTime > 0.f) ? (1.f / FoundAction.AnimationCorrectionTime) : 1.f;
		NewState.ArcHeight = FoundAction.ArcHeight;
		
		CurrentParkourState = NewState;
		OnRep_CurrentParkourState(); 
	}
}

void UParkourComponent::OnRep_CurrentParkourState()
{
	if(LocalCurrentActionType == CurrentParkourState.ActionType) return;
	LocalCurrentActionType = CurrentParkourState.ActionType;
	
	// Если начинается паркур
	if (CurrentParkourState.ActionType != EParkourActionType::None)
	{
		if (!OwnerCharacter || !OwnerMovementComponent || !ParkourTimelineCurve) return;
		
		OwnerMovementComponent->SetMovementMode(MOVE_Flying);
		// Телепортация в начальную точку теперь происходит только здесь, перед запуском Timeline
		OwnerCharacter->SetActorLocation(CurrentParkourState.StartLocation, false, nullptr, ETeleportType::TeleportPhysics);
		
		PlayParkourAnimation();
		
		SetComponentTickEnabled(true);
		ParkourTimeline.SetPlayRate(CurrentParkourState.PlayRate);
		ParkourTimeline.PlayFromStart();
	}
	else // Если паркур закончился
	{
		PlayParkourAnimation();
	}
}

bool UParkourComponent::CanParkour(FParkourAction& OutAction, FVector& OutLedgeLocation) const
{
	// Мы используем LogTemp, Error, чтобы сообщения были красными и хорошо видны в логе.
	
	if (!OwnerCharacter || !OwnerMovementComponent)
	{
		UE_LOG(LogTemp, Error, TEXT("CANPARKOUR FAIL #1: OwnerCharacter or OwnerMovementComponent is NULL."));
		return false;
	}

	if (CurrentParkourState.ActionType != EParkourActionType::None)
	{
		UE_LOG(LogTemp, Error, TEXT("CANPARKOUR FAIL #2: Already in a parkour action."));
		return false;
	}
	
	if (OwnerMovementComponent->IsFalling())
	{
		UE_LOG(LogTemp, Error, TEXT("CANPARKOUR FAIL #3: Character is falling."));
		return false;
	}
	
	const FVector Start = OwnerCharacter->GetActorLocation();
	const FVector Forward = OwnerCharacter->GetActorForwardVector();
	const FVector End = Start + Forward * 120.0f;

	FHitResult WallHit;
	FCollisionQueryParams Params;
	Params.AddIgnoredActor(OwnerCharacter);

	const bool bWallHit = GetWorld()->LineTraceSingleByChannel(WallHit, Start, End, ECC_Visibility, Params);
	if (!bWallHit)
	{
		UE_LOG(LogTemp, Error, TEXT("CANPARKOUR FAIL #4: Wall trace did not hit anything."));
		return false;
	}
	
	if (bDrawDebugTraces) DrawDebugLine(GetWorld(), Start, WallHit.ImpactPoint, FColor::Red, false, 3.0f, 0, 2.0f);
	
	if (FVector::DotProduct(WallHit.ImpactNormal, FVector::UpVector) > 0.3f)
	{
		UE_LOG(LogTemp, Error, TEXT("CANPARKOUR FAIL #5: Wall angle is too steep. DotProduct: %f"), FVector::DotProduct(WallHit.ImpactNormal, FVector::UpVector));
		return false;
	}

	const FVector LedgeCheckStart = WallHit.ImpactPoint + Forward * 15.0f + FVector(0.f, 0.f, OwnerCharacter->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() * 2.f);
	const FVector LedgeCheckEnd = LedgeCheckStart - FVector(0.f, 0.f, OwnerCharacter->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() * 2.f + 50.f);

	FHitResult LedgeHit;
	const bool bLedgeHit = GetWorld()->LineTraceSingleByChannel(LedgeHit, LedgeCheckStart, LedgeCheckEnd, ECC_Visibility, Params);
	if (!bLedgeHit)
	{
		UE_LOG(LogTemp, Error, TEXT("CANPARKOUR FAIL #6: Ledge trace did not hit anything."));
		return false;
	}

	if (bDrawDebugTraces) DrawDebugLine(GetWorld(), LedgeCheckStart, LedgeHit.ImpactPoint, FColor::Cyan, false, 3.0f, 0, 2.0f);
	
	if (FVector::DotProduct(LedgeHit.ImpactNormal, FVector::UpVector) < 0.9f)
	{
		UE_LOG(LogTemp, Error, TEXT("CANPARKOUR FAIL #7: Ledge surface angle is not flat enough. DotProduct: %f"), FVector::DotProduct(LedgeHit.ImpactNormal, FVector::UpVector));
		return false;
	}

	const UCapsuleComponent* Capsule = OwnerCharacter->GetCapsuleComponent();
	const FVector CapsuleCheckLocation = LedgeHit.ImpactPoint + FVector(0, 0, Capsule->GetScaledCapsuleHalfHeight() + 2.0f);
	const FCollisionShape CapsuleShape = FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(), Capsule->GetScaledCapsuleHalfHeight());
	FHitResult SweepHitResult;
	const bool bBlocked = GetWorld()->SweepSingleByChannel(SweepHitResult, CapsuleCheckLocation, CapsuleCheckLocation, FQuat::Identity, ECC_Visibility, CapsuleShape, Params);

	if (bDrawDebugTraces) DrawDebugCapsule(GetWorld(), CapsuleCheckLocation, Capsule->GetScaledCapsuleHalfHeight(), Capsule->GetScaledCapsuleRadius(), FQuat::Identity, bBlocked ? FColor::Red : FColor::Green, false, 3.f, 0, 2.f);

	if (bBlocked)
	{
		UE_LOG(LogTemp, Error, TEXT("CANPARKOUR FAIL #8: Space above the ledge is blocked. Actor hit: %s"), *GetNameSafe(SweepHitResult.GetActor()));
		return false;
	}

	const float LedgeHeight = (LedgeHit.ImpactPoint - Start).Z;
	if (TryFindParkourAction(LedgeHeight, OutAction))
	{
		OutLedgeLocation = LedgeHit.ImpactPoint;
		UE_LOG(LogTemp, Warning, TEXT("CANPARKOUR SUCCESS! Found action. Ledge Height: %f"), LedgeHeight);
		return true;
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("CANPARKOUR FAIL #9: No suitable ParkourAction found for Ledge Height: %f"), LedgeHeight);
		return false;
	}
	
	return false;
}

bool UParkourComponent::TryFindParkourAction(float LedgeHeight, FParkourAction& OutAction) const
{
	for (const FParkourAction& Action : ParkourActions)
	{
		if (LedgeHeight >= Action.MinHeight && LedgeHeight < Action.MaxHeight)
		{
			OutAction = Action;
			return true;
		}
	}
	return false;
}

void UParkourComponent::TimelineUpdate(float Alpha)
{
	if (OwnerCharacter)
	{
		// 1. Считаем базовую позицию на прямой линии, как и раньше
		const FVector NewLocation = FMath::Lerp(CurrentParkourState.StartLocation, CurrentParkourState.EndLocation, Alpha);

		// --- НОВЫЙ КОД ДЛЯ ДУГИ ---
		// 2. Вычисляем дополнительную высоту.
		// FMath::Sin(Alpha * PI) дает нам красивую кривую: 0 в начале, 1 в середине, 0 в конце.
		// Мы умножаем ее на высоту дуги, которую зададим в редакторе.
		const float Z_Offset = FMath::Sin(Alpha * PI) * CurrentParkourState.ArcHeight;

		// 3. Применяем смещение к нашей вычисленной позиции
		FVector FinalLocation = NewLocation;
		FinalLocation.Z += Z_Offset;
		// --- КОНЕЦ НОВОГО КОДА ---

		OwnerCharacter->SetActorLocation(FinalLocation);
	}
}

void UParkourComponent::TimelineFinished()
{
	//SetComponentTickEnabled(false);
	if (OwnerCharacter && OwnerCharacter->HasAuthority())
	{
		// Используем более надежный способ сброса состояния
		const FReplicatedParkourState EmptyState;
		CurrentParkourState = EmptyState;
		
		OnRep_CurrentParkourState(); 
	}
}

void UParkourComponent::PerformParkour(const FParkourAction& Action, const FVector& LedgeLocation)
{
	const UCapsuleComponent* Capsule = OwnerCharacter->GetCapsuleComponent();
       
	const FVector StartPoint = LedgeLocation 
	   + (OwnerCharacter->GetActorForwardVector() * (Action.StartingOffset.X - Capsule->GetScaledCapsuleRadius())) 
	   + FVector(0, 0, Action.StartingOffset.Z);
    
	// Используем более точный расчёт конечной точки из предыдущего ответа
	FVector EndPoint = LedgeLocation + FVector(Action.LandedOffset.X, Action.LandedOffset.Y, 0.f);
	EndPoint.Z += Capsule->GetScaledCapsuleHalfHeight();

	// Проверку на столкновение (Sweep) здесь можно пропустить для предсказания,
	// так как сервер всё равно сделает финальную проверку.
    
	FReplicatedParkourState NewState;
	NewState.ActionType = Action.ActionType;
	NewState.StartLocation = StartPoint;
	NewState.EndLocation = EndPoint;
	NewState.Montage = Action.Montage;
	NewState.PlayRate = (Action.AnimationCorrectionTime > 0.f) ? (1.f / Action.AnimationCorrectionTime) : 1.f;
	NewState.ArcHeight = Action.ArcHeight;
    
	CurrentParkourState = NewState;
	// Напрямую вызываем OnRep, чтобы запустить таймлайн немедленно
	OnRep_CurrentParkourState(); 
}

void UParkourComponent::ServerPlayRootMotionMatchTarget_Implementation(FVector ClimbTargetLocation)
{
	
}

void UParkourComponent::PlayParkourAnimation()
{
	if (!OwnerCharacter || !OwnerMovementComponent) return;

	// Если паркур ЗАКОНЧИЛСЯ
	if (CurrentParkourState.ActionType == EParkourActionType::None)
	{
		OwnerMovementComponent->SetMovementMode(MOVE_Falling);
		// Возвращаем камере стандартное поведение
		if (OwnerSpringArm)
		{
			//OwnerSpringArm->bDoCollisionTest = true;
		}
		return;
	}

	// Если паркур НАЧИНАЕТСЯ
	if (CurrentParkourState.Montage)
	{
		// Отключаем столкновения для камеры, чтобы она плавно проходила сквозь объекты
		if (OwnerSpringArm)
		{
			//OwnerSpringArm->bDoCollisionTest = false;
		}

		OwnerMovementComponent->SetMovementMode(MOVE_Flying);
		if (UAnimInstance* AnimInstance = OwnerCharacter->GetMesh()->GetAnimInstance())
		{
			AnimInstance->Montage_Play(CurrentParkourState.Montage);
		}
	}
}
