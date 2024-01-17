// Copyright © 2017-2023 Esri R&D Center Zurich. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "InstanceReplacement.h"
#include "ReplacementDialog.h"

#include "InstanceReplacementDialog.generated.h"

UCLASS(DisplayName = "Instance Replacement")
class UInstanceReplacementWrapper : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FString SourceMeshIdentifier;

	UPROPERTY()
	TArray<UStaticMeshComponent*> MeshComponents;

	UPROPERTY(EditAnywhere, DisplayName = "Options", Category = "Replacements")
	TArray<FReplacementOption> Replacements;
};

UCLASS(meta = (DisplayName = "Options"))
class UInstanceReplacementDialogOptions : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "Replacements")
	UInstanceReplacementAsset* TargetReplacementAsset;

	UPROPERTY(EditAnywhere, Category = "Replacements")
	TMap<FString, UInstanceReplacementWrapper*> InstanceReplacements;
};

class SInstanceReplacementDialogWidget : public SReplacementDialogWidget
{
	UInstanceReplacementDialogOptions* ReplacementDialogOptions = nullptr;

	TArray<TSharedPtr<SCheckBox>> IsolateCheckboxes;
	TSharedPtr<SCheckBox> ApplyToAllVitruvioActorsCheckBox;

public:
	SLATE_BEGIN_ARGS(SInstanceReplacementDialogWidget) {}
	SLATE_ARGUMENT(TSharedPtr<SWindow>, ParentWindow)
	SLATE_ARGUMENT(UVitruvioComponent*, VitruvioComponent)
	SLATE_ARGUMENT(bool, GeneratedWithoutReplacements)
	SLATE_END_ARGS()

	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;

	void Construct(const FArguments& InArgs);

protected:
	virtual FText CreateHeaderText() override;
	virtual TSharedPtr<ISinglePropertyView> CreateTargetReplacementWidget() override;
	virtual void UpdateApplyButtonEnablement() override;
	virtual void OnCreateNewAsset() override;
	virtual void AddDialogOptions(const TSharedPtr<SVerticalBox>& Content) override;
	virtual void OnWindowClosed() override;
	virtual void UpdateReplacementTable() override;
	virtual FReply OnReplacementConfirmed() override;
	virtual FReply OnReplacementCanceled() override;
};

class FInstanceReplacementDialog
{
public:
	template <typename TOnWindowClosed>
	static void OpenDialog(UVitruvioComponent* VitruvioComponent, TOnWindowClosed OnWindowClosed, bool bGeneratedWithoutReplacements)
	{
		FReplacementDialog::OpenDialog<SInstanceReplacementDialogWidget>(VitruvioComponent, OnWindowClosed, bGeneratedWithoutReplacements,
																		 {800, 600});
	}
};