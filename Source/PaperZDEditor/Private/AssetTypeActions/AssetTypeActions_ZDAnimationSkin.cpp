// Copyright 2017 ~ 2026 Critical Failure Studio Ltd. All rights reserved.

#include "AssetTypeActions/AssetTypeActions_ZDAnimationSkin.h"
#include "AnimSequences/Skins/PaperZDAnimationSkin.h"

//Styles
#include "Styling/AppStyle.h"

#define LOCTEXT_NAMESPACE "AssetTypeActionsPaperZDAnimationSkin"

const FName FAssetTypeActions_ZDAnimationSkin::CategoryKey = FName("PaperZD_AssetActionsKey");
const FText FAssetTypeActions_ZDAnimationSkin::CategoryDisplayName = FText::FromString("PaperZD");

FAssetTypeActions_ZDAnimationSkin::FAssetTypeActions_ZDAnimationSkin(EAssetTypeCategories::Type InAssetCategory) 
	: MyAssetCategory(InAssetCategory)
{}

FText FAssetTypeActions_ZDAnimationSkin::GetName() const
{
	return NSLOCTEXT("AssetTypeActions", "AssetTypeActions_ZDAnimationSkin", "Animation Skin Asset");
}

UClass* FAssetTypeActions_ZDAnimationSkin::GetSupportedClass() const
{
	return UPaperZDAnimationSkin::StaticClass();
}

uint32 FAssetTypeActions_ZDAnimationSkin::GetCategories()
{
	return MyAssetCategory;
}

FText FAssetTypeActions_ZDAnimationSkin::GetAssetDescription(const FAssetData& AssetData) const
{
	return FText::FromString("A skin file that can be applied to an animation instance to modify how the render components displays the AnimSequences.");
}

#undef LOCTEXT_NAMESPACE
