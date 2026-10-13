// Copyright 2017 ~ 2026 Critical Failure Studio Ltd. All rights reserved.

#pragma once
#include "CoreMinimal.h"
#include "AssetTypeActions_Base.h"

class UPaperZDAnimSequence;
class FAssetTypeActions_ZDAnimationSkin : public FAssetTypeActions_Base
{
public:
	//Category Key for registering an advanced category
	static const FName CategoryKey;

	//Display name of the category, will be called ZetaD to avoid confusing the user with Paper2D
	static const FText CategoryDisplayName;

	//Constructor will have the custom asset category passed
	FAssetTypeActions_ZDAnimationSkin(EAssetTypeCategories::Type InAssetCategory);

private:
	EAssetTypeCategories::Type MyAssetCategory;

public:
	// IAssetTypeActions Implementation
	virtual FText GetName() const override;
	virtual FColor GetTypeColor() const override { return FColor(0, 175, 255); }
	virtual UClass* GetSupportedClass() const override;
	virtual uint32 GetCategories() override;
	virtual FText GetAssetDescription(const FAssetData& AssetData) const override;
	// End IAssetTypeActions Implementation
};
