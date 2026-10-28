// Copyright 2017 ~ 2022 Critical Failure Studio Ltd. All rights reserved.

#pragma once

#include "CoreMinimal.h"

class UEdGraphNode;

struct FPaperZDStateDebugInfo
{
	bool bIsActive = false;
	bool bWasRecentlyTraversed = false;
	float ElapsedTime = 0.0f;
	float RecentWeight = 0.0f;
};

struct FPaperZDTransitionDebugInfo
{
	bool bWasRecentlyTaken = false;
	bool bIsTransitionalAnimationPlaying = false;
	float RecentWeight = 0.0f;

	bool IsActive() const { return bWasRecentlyTaken || bIsTransitionalAnimationPlaying; }
};

namespace FPaperZDAnimBPDebugUtils
{
	/* Resolve live debug information for a state or conduit graph node. */
	bool GetStateDebugInfo(const UEdGraphNode* GraphNode, FPaperZDStateDebugInfo& OutDebugInfo);

	/* Resolve live debug information for a transition graph node. */
	bool GetTransitionDebugInfo(const UEdGraphNode* GraphNode, FPaperZDTransitionDebugInfo& OutDebugInfo);
}
