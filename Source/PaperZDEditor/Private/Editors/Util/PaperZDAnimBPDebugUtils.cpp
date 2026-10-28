// Copyright 2017 ~ 2022 Critical Failure Studio Ltd. All rights reserved.

#include "Editors/Util/PaperZDAnimBPDebugUtils.h"
#include "AnimNodes/PaperZDAnimNode_StateMachine.h"
#include "Graphs/Nodes/PaperZDAnimGraphNode_StateMachine.h"
#include "Graphs/Nodes/PaperZDStateGraphNode_Conduit.h"
#include "Graphs/Nodes/PaperZDStateGraphNode_Transition.h"
#include "Graphs/PaperZDStateMachineGraph.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "PaperZDAnimBP.h"
#include "PaperZDAnimBPGeneratedClass.h"
#include "PaperZDAnimInstance.h"

namespace
{
	struct FResolvedStateMachineDebugData
	{
		const FPaperZDAnimStateMachineDebugData* CompiledData = nullptr;
		const FPaperZDAnimNode_StateMachine* RuntimeNode = nullptr;
	};

	bool ResolveStateMachineDebugData(const UEdGraphNode* GraphNode, FResolvedStateMachineDebugData& OutData)
	{
		if (!GraphNode)
		{
			return false;
		}

		const UPaperZDAnimBP* AnimBP = Cast<UPaperZDAnimBP>(FBlueprintEditorUtils::FindBlueprintForNode(GraphNode));
		UPaperZDAnimInstance* DebugInstance = AnimBP ? Cast<UPaperZDAnimInstance>(AnimBP->GetObjectBeingDebugged()) : nullptr;
		if (!IsValid(DebugInstance) || (AnimBP->GeneratedClass && !DebugInstance->IsA(AnimBP->GeneratedClass)))
		{
			return false;
		}

		const UPaperZDStateMachineGraph* StateMachineGraph = Cast<UPaperZDStateMachineGraph>(GraphNode->GetGraph());
		const UPaperZDAnimGraphNode_StateMachine* MachineGraphNode = StateMachineGraph ? StateMachineGraph->OwnerAnimGraphNode.Get() : nullptr;
		const UPaperZDAnimBPGeneratedClass* GeneratedClass = Cast<UPaperZDAnimBPGeneratedClass>(DebugInstance->GetClass());
		if (!MachineGraphNode || !MachineGraphNode->NodeGuid.IsValid() || !GeneratedClass)
		{
			return false;
		}

		OutData.CompiledData = GeneratedClass->GetDebugData().StateMachineData.Find(MachineGraphNode->NodeGuid);
		if (!OutData.CompiledData)
		{
			return false;
		}

		for (const FPaperZDAnimNode_StateMachine* RuntimeNode : GeneratedClass->GetStateMachineNodes(DebugInstance))
		{
			if (RuntimeNode && RuntimeNode->StateMachineIndex == OutData.CompiledData->StateMachineIndex)
			{
				OutData.RuntimeNode = RuntimeNode;
				return true;
			}
		}

		return false;
	}

	float GetRecentWeight(const FPaperZDAnimNode_StateMachine& RuntimeNode)
	{
		const float Duration = FPaperZDAnimNode_StateMachine::GetDebugTransitionDisplayDuration();
		return Duration > 0.0f ? 1.0f - FMath::Clamp(RuntimeNode.GetRecentTransitionElapsedTime() / Duration, 0.0f, 1.0f) : 0.0f;
	}
}

bool FPaperZDAnimBPDebugUtils::GetStateDebugInfo(const UEdGraphNode* GraphNode, FPaperZDStateDebugInfo& OutDebugInfo)
{
	OutDebugInfo = FPaperZDStateDebugInfo();

	FResolvedStateMachineDebugData ResolvedData;
	if (!ResolveStateMachineDebugData(GraphNode, ResolvedData))
	{
		return false;
	}

	const int32* StateIndex = ResolvedData.CompiledData->StateNodeToIndex.Find(GraphNode->NodeGuid);
	if (!StateIndex)
	{
		return false;
	}

	OutDebugInfo.bIsActive = ResolvedData.RuntimeNode->GetCurrentStateIndex() == *StateIndex;
	OutDebugInfo.ElapsedTime = OutDebugInfo.bIsActive ? ResolvedData.RuntimeNode->GetCurrentStateTime() : 0.0f;

	//Conduits are never the terminal current state, but their accepted incoming and outgoing
	//transition links are present in the recent path and can be used to show traversal.
	if (GraphNode->IsA<UPaperZDStateGraphNode_Conduit>())
	{
		for (const UEdGraphNode* CandidateNode : GraphNode->GetGraph()->Nodes)
		{
			const UPaperZDStateGraphNode_Transition* TransitionNode = Cast<UPaperZDStateGraphNode_Transition>(CandidateNode);
			if (TransitionNode && (TransitionNode->GetFromNode() == GraphNode || TransitionNode->GetToNode() == GraphNode))
			{
				const int32* TransitionIndex = ResolvedData.CompiledData->TransitionNodeToIndex.Find(TransitionNode->NodeGuid);
				if (TransitionIndex && ResolvedData.RuntimeNode->GetRecentTransitionIndices().Contains(*TransitionIndex))
				{
					OutDebugInfo.bWasRecentlyTraversed = true;
					OutDebugInfo.RecentWeight = GetRecentWeight(*ResolvedData.RuntimeNode);
					break;
				}
			}
		}
	}

	return true;
}

bool FPaperZDAnimBPDebugUtils::GetTransitionDebugInfo(const UEdGraphNode* GraphNode, FPaperZDTransitionDebugInfo& OutDebugInfo)
{
	OutDebugInfo = FPaperZDTransitionDebugInfo();

	FResolvedStateMachineDebugData ResolvedData;
	if (!ResolveStateMachineDebugData(GraphNode, ResolvedData))
	{
		return false;
	}

	const int32* TransitionIndex = ResolvedData.CompiledData->TransitionNodeToIndex.Find(GraphNode->NodeGuid);
	if (!TransitionIndex)
	{
		return false;
	}

	OutDebugInfo.bWasRecentlyTaken = ResolvedData.RuntimeNode->GetRecentTransitionIndices().Contains(*TransitionIndex);
	OutDebugInfo.bIsTransitionalAnimationPlaying = ResolvedData.RuntimeNode->GetActiveTransitionIndex() == *TransitionIndex;
	OutDebugInfo.RecentWeight = OutDebugInfo.bWasRecentlyTaken ? GetRecentWeight(*ResolvedData.RuntimeNode) : 0.0f;
	return true;
}
