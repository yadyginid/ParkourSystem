#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Components/TimelineComponent.h"
#include "ParkourComponent.generated.h"

// --- ОБЪЯВЛЕНИЕ СТРУКТУР ДАННЫХ ---

class USpringArmComponent;
class UCharacterMovementComponent;

UENUM(BlueprintType)
enum class EParkourActionType : uint8
{
	None,
	LowClimb,
	NormalClimb,
	HighClimb,
	Vault
};

USTRUCT(BlueprintType)
struct FParkourAction
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, Category = "Parkour")
	EParkourActionType ActionType = EParkourActionType::None;

	UPROPERTY(EditDefaultsOnly, Category = "Parkour")
	TObjectPtr<UAnimMontage> Montage = nullptr;

	UPROPERTY(EditDefaultsOnly, Category = "Parkour")
	float MinHeight = 0.0f;
	
	UPROPERTY(EditDefaultsOnly, Category = "Parkour")
	float MaxHeight = 0.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Parkour")
	FVector StartingOffset = FVector::ZeroVector;

	UPROPERTY(EditDefaultsOnly, Category = "Parkour")
	FVector LandedOffset = FVector::ZeroVector;
	
	UPROPERTY(EditDefaultsOnly, Category = "Parkour", meta = (ClampMin = "0.1"))
	float AnimationCorrectionTime = 1.f;

	// *** ДОБАВЛЯЕМ ЭТО ПОЛЕ СЮДА ***
	UPROPERTY(EditDefaultsOnly, Category = "Parkour")
	float ArcHeight = 50.f; 
};

// Структура для надежной атомарной репликации состояния паркура
USTRUCT()
struct FReplicatedParkourState
{
	GENERATED_BODY()

	UPROPERTY()
	EParkourActionType ActionType = EParkourActionType::None;

	UPROPERTY()
	FVector_NetQuantize StartLocation;

	UPROPERTY()
	FVector_NetQuantize EndLocation;
	
	UPROPERTY()
	TObjectPtr<UAnimMontage> Montage = nullptr;
	
	UPROPERTY()
	float PlayRate = 1.f;

	UPROPERTY()
	float ArcHeight = 0.f;
};

// --- ОБЪЯВЛЕНИЕ КЛАССА КОМПОНЕНТА ---

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class PARKOURPROJECT_API UParkourComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UParkourComponent();

	/** Главная функция, вызываемая персонажем по нажатию кнопки. */
	void OnParkourInput();

protected:
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	
	UPROPERTY(EditDefaultsOnly, Category = "Parkour Actions", meta = (TitleProperty = "ActionType"))
	TArray<FParkourAction> ParkourActions;

	UPROPERTY(EditDefaultsOnly, Category = "Parkour|Debug")
	bool bDrawDebugTraces = false;

	UPROPERTY(EditDefaultsOnly, Category = "Parkour|Timeline")
	TObjectPtr<UCurveFloat> ParkourTimelineCurve;

	EParkourActionType LocalCurrentActionType;

private:
	// --- Объявления функций ---
	bool CanParkour(FParkourAction& OutAction, FVector& OutLedgeLocation) const;
	bool TryFindParkourAction(float LedgeHeight, FParkourAction& OutAction) const;
	void PlayParkourAnimation();

	UFUNCTION(Server, Reliable)
	void Server_TryPerformParkour();
    
	UFUNCTION()
	void OnRep_CurrentParkourState();

	// --- Логика Timeline ---
	FTimeline ParkourTimeline;
	UFUNCTION()
	void TimelineUpdate(float Alpha);
	UFUNCTION()
	void TimelineFinished();

	// --- Переменные ---
	UPROPERTY(ReplicatedUsing = OnRep_CurrentParkourState)
	FReplicatedParkourState CurrentParkourState;
	
	UPROPERTY()
	TObjectPtr<ACharacter> OwnerCharacter;

	UPROPERTY()
	TObjectPtr<UCharacterMovementComponent> OwnerMovementComponent;

	UPROPERTY()
	TObjectPtr<USpringArmComponent> OwnerSpringArm;

	UFUNCTION(Server, Reliable)
	void ServerPlayRootMotionMatchTarget(FVector ClimbTargetLocation);

	UPROPERTY()
	TObjectPtr<UAnimMontage> Montage = nullptr;

	void PerformParkour(const FParkourAction& Action, const FVector& LedgeLocation);
};