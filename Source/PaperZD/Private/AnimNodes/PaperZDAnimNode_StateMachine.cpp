// Copyright 2017 ~ 2022 Critical Failure Studio Ltd. All rights reserved.

#include "AnimNodes/PaperZDAnimNode_StateMachine.h"
#include "AnimNodes/PaperZDAnimStateMachine.h"
#include "AnimNodes/PaperZDAnimNode_Sink.h"
#include "AnimSequences/Players/PaperZDAnimPlayer.h"
#include "PaperZDAnimInstance.h"
#include "PaperZDAnimBPGeneratedClass.h"

#if ZD_VERSION_INLINED_CPP_SUPPORT
#include UE_INLINE_GENERATED_CPP_BY_NAME(PaperZDAnimNode_StateMachine)
#endif

FPaperZDAnimNode_StateMachine::FScopedAnimationUpdate::FScopedAnimationUpdate(FPaperZDAnimNode_StateMachine* InStateMachine, const FPaperZDAnimationUpdateContext& InUpdateContext)
	: StateMachine(InStateMachine)
	, UpdateContext(InUpdateContext)
{
	check(InStateMachine);
	SequenceCompleteHandle = UpdateContext.AnimInstance->GetPlayer()->OnPlaybackSequenceComplete_Native.AddRaw(this, &FPaperZDAnimNode_StateMachine::FScopedAnimationUpdate::OnTransitionalAnimationComplete);
}

FPaperZDAnimNode_StateMachine::FScopedAnimationUpdate::~FScopedAnimationUpdate()
{
	UpdateContext.AnimInstance->GetPlayer()->OnPlaybackSequenceComplete_Native.Remove(SequenceCompleteHandle);
}

void FPaperZDAnimNode_StateMachine::FScopedAnimationUpdate::Update()
{
	FPaperZDAnimNode_Base* CurrentAnimNode = StateMachine->GetCurrentAnimNode();
	if (CurrentAnimNode)
	{
		CurrentAnimNode->Update(UpdateContext);
	}
}

void FPaperZDAnimNode_StateMachine::FScopedAnimationUpdate::OnTransitionalAnimationComplete(const UPaperZDAnimSequence* AnimSequence)
{
	//Need to delay the removal of the transitional AnimNode, as at this point it hasn't been evaluated
	StateMachine->bPopTransitionalAnimNode = true;
}

//////////////////////////////////////////////////////////////////////////
//
//////////////////////////////////////////////////////////////////////////
FPaperZDAnimNode_StateMachine::FPaperZDAnimNode_StateMachine()
	: StateMachineIndex(INDEX_NONE)
	, CachedStateMachine(nullptr)
	, CurrentStateIndex(INDEX_NONE)
	, CurrentStateTime(0.0f)
	, CurrentStateAnimNode(nullptr)
	, CurrentTransitionalAnimNode(nullptr)
	, bPopTransitionalAnimNode(false)
#if WITH_EDITORONLY_DATA
	, RecentTransitionElapsedTime(0.0f)
	, ActiveTransitionIndex(INDEX_NONE)
#endif
{}

void FPaperZDAnimNode_StateMachine::OnInitialize(const FPaperZDAnimationInitContext& InitContext)
{
#if WITH_EDITORONLY_DATA
	ResetDebugData();
#endif

	//Cache state machine and initialize first state
	UPaperZDAnimBPGeneratedClass* GeneratedClass = InitContext.GetAnimBPClass();
	if (GeneratedClass->GetStateMachines().IsValidIndex(StateMachineIndex))
	{
		CachedStateMachine = &GeneratedClass->GetStateMachines()[StateMachineIndex];
		if (CachedStateMachine->Nodes.IsValidIndex(CachedStateMachine->InitialState))
		{
			SetState(CachedStateMachine->InitialState, InitContext);
			check(CurrentStateAnimNode);
			CurrentStateAnimNode->Initialize(InitContext);
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("FPaperZDAnimNode_StateMachine: Bad machine ptr"));
	}
}

void FPaperZDAnimNode_StateMachine::OnUpdate(const FPaperZDAnimationUpdateContext& UpdateContext)
{
	if (CachedStateMachine && CurrentStateIndex != INDEX_NONE)
	{
#if WITH_EDITORONLY_DATA
		if (RecentTransitionIndices.Num() > 0)
		{
			RecentTransitionElapsedTime += FMath::Max(0.0f, UpdateContext.DeltaTime);
			if (RecentTransitionElapsedTime > GetDebugTransitionDisplayDuration())
			{
				RecentTransitionIndices.Reset();
			}
		}

		TArray<int32> TransitionsTakenThisUpdate;
#endif

		//Transitional nodes need to be removed in a deferred way, because they complete their "AnimationComplete" callback while updating
		//but by this point they haven't been evaluated and rendered.
		//We proceed to pop the pending transitional anim node now, when its "Evaluate" method has already been called.
		if (bPopTransitionalAnimNode)
		{
			CurrentTransitionalAnimNode = nullptr;
			bPopTransitionalAnimNode = false;
#if WITH_EDITORONLY_DATA
			ActiveTransitionIndex = INDEX_NONE;
#endif
		}

		//Check for any pending state change
		FNodeEvaluationContext Context(UpdateContext.AnimInstance);
		Context.VisitedNodes.Add(CurrentStateIndex);
		while (true)
		{
#if WITH_EDITORONLY_DATA
			TArray<int32> TransitionPath;
			const FPaperZDAnimStateMachineLink* NextTransition = CheckValidTransition(CurrentStateIndex, Context, &TransitionPath);
#else
			const FPaperZDAnimStateMachineLink* NextTransition = CheckValidTransition(CurrentStateIndex, Context);
#endif
			if (!NextTransition)
			{
				break;
			}

#if WITH_EDITORONLY_DATA
			TransitionsTakenThisUpdate.Append(TransitionPath);
#endif

			SetState(NextTransition->TargetNodeIndex, UpdateContext);
			Context.VisitedNodes.Add(CurrentStateIndex);
			
			//Check for transitional graphs
			if (NextTransition->HasTransitionalAnimations())
			{
				//Anim node could have been ignored when compiled (empty result node), in which case it exists but has no animations that could trigger the EndLoop callback
				//we need to make sure that the result LinkID is at least linked to something, otherwise we could potentially end up with an AnimGraph without players
				//in which case they won't be able to ever "end" by reaching the end of their animations.
 				FPaperZDAnimNode_Sink* TransitionalAnimNode = UpdateContext.GetAnimBPClass()->GetAnimNodeByPropertyIndex<FPaperZDAnimNode_Sink>(Context.AnimInstance, NextTransition->TransitionalAnimNodeIndex);
 				if (TransitionalAnimNode && TransitionalAnimNode->HasAnimationData())
 				{
					CurrentTransitionalAnimNode = TransitionalAnimNode;
#if WITH_EDITORONLY_DATA
					ActiveTransitionIndex = NextTransition->DebugTransitionIndex;
#endif
 				}
			}

			//New AnimNode needs to be initialized on entry
			FPaperZDAnimationInitContext InitContext(UpdateContext.AnimInstance);
			CurrentStateAnimNode->Initialize(InitContext);

			//Initialize optional TransitionalNode
			if (CurrentTransitionalAnimNode)
			{
				CurrentTransitionalAnimNode->Initialize(InitContext);
			}

			//We continue transitioning unless the AnimInstance doesn't allow for it
			if (!UpdateContext.AnimInstance->AllowsTransitionalStates())
			{
				break;
			}
		}

#if WITH_EDITORONLY_DATA
		if (TransitionsTakenThisUpdate.Num() > 0)
		{
			RecentTransitionIndices = MoveTemp(TransitionsTakenThisUpdate);
			RecentTransitionElapsedTime = 0.0f;
		}
#endif

		//Increment time spent on this state
		CurrentStateTime += UpdateContext.DeltaTime;

		//Pass through the OnUpdate call to the AnimNode
		{
			FScopedAnimationUpdate AnimationScope(this, UpdateContext);
			AnimationScope.Update();
		}
	}
}

void FPaperZDAnimNode_StateMachine::OnEvaluate(FPaperZDAnimationPlaybackData& OutData)
{
	FPaperZDAnimNode_Base* CurrentAnimNode = GetCurrentAnimNode();
	if (CurrentAnimNode)
	{
		CurrentAnimNode->Evaluate(OutData);
	}
}

FName FPaperZDAnimNode_StateMachine::GetMachineName() const
{
	return CachedStateMachine ? CachedStateMachine->MachineName : NAME_None;
}

void FPaperZDAnimNode_StateMachine::JumpToNode(FName Name, const FPaperZDAnimationBaseContext& Context)
{
	if (CachedStateMachine)
	{
		const int32* pTargetNodeIdx = CachedStateMachine->JumpLinks.Find(Name);
		if (pTargetNodeIdx)
		{
#if WITH_EDITORONLY_DATA
			ResetDebugData();
#endif
			SetState(*pTargetNodeIdx, Context);

			//Initialize the state
			FPaperZDAnimationInitContext InitContext(Context.AnimInstance);
			CurrentStateAnimNode->Initialize(InitContext);
		}
	}
}

void FPaperZDAnimNode_StateMachine::SetState(int32 NewState, const FPaperZDAnimationBaseContext& Context)
{
	//Call the Exit State delegate if it exists
	if (CachedStateMachine->Nodes.IsValidIndex(CurrentStateIndex) && !CachedStateMachine->Nodes[CurrentStateIndex].OnStateExitEventName.IsNone())
	{
		CallEvent(CachedStateMachine->Nodes[CurrentStateIndex].OnStateExitEventName, Context);
	}

	CurrentStateIndex = NewState;
	CurrentStateAnimNode = Context.GetAnimBPClass()->GetAnimNodeByPropertyIndex(Context.AnimInstance, CachedStateMachine->Nodes[CurrentStateIndex].AnimNodeIndex);
	CurrentStateTime = 0.0f;
	CurrentTransitionalAnimNode = nullptr;
#if WITH_EDITORONLY_DATA
	ActiveTransitionIndex = INDEX_NONE;
#endif

	//Call the Enter State delegate if it exists
	if (CachedStateMachine->Nodes.IsValidIndex(CurrentStateIndex) && !CachedStateMachine->Nodes[CurrentStateIndex].OnStateEnterEventName.IsNone())
	{
		CallEvent(CachedStateMachine->Nodes[CurrentStateIndex].OnStateEnterEventName, Context);
	}
}

bool FPaperZDAnimNode_StateMachine::CanEnterNode(int32 NodeIndex, const FNodeEvaluationContext& EvaluationContext) const
{
	if (!EvaluationContext.VisitedNodes.Contains(NodeIndex))
	{
		//By default any node can be entered, unless its a conduit, in which case we need to evaluate its rule
		bool bCanEnter = true;
		const FPaperZDAnimStateMachineNode& Node = CachedStateMachine->Nodes[NodeIndex];
		if (Node.bConduit)
		{
			check(CachedStateMachine->TransitionRules.IsValidIndex(Node.ConduitRuleIndex));
			const FPaperZDAnimStateMachineTransitionRule& ConduitRule = CachedStateMachine->TransitionRules[Node.ConduitRuleIndex];
			bCanEnter = ConduitRule.EvaluateRule(EvaluationContext.AnimInstance);
		}

		return bCanEnter;
	}

	return false;
}

const FPaperZDAnimStateMachineLink* FPaperZDAnimNode_StateMachine::CheckValidTransition(int32 NodeIndex, FNodeEvaluationContext& EvaluationContext, TArray<int32>* OutTransitionPath) const
{
#if !WITH_EDITORONLY_DATA
	(void)OutTransitionPath;
#endif

	const FPaperZDAnimStateMachineNode& Node = CachedStateMachine->Nodes[NodeIndex];
	for (const FPaperZDAnimStateMachineLink& LinkTransition : Node.OutwardLinks)
	{
		if (CachedStateMachine->TransitionRules.IsValidIndex(LinkTransition.TransitionRuleIndex))
		{
			const FPaperZDAnimStateMachineTransitionRule& Rule = CachedStateMachine->TransitionRules[LinkTransition.TransitionRuleIndex];
			if (Rule.EvaluateRule(EvaluationContext.AnimInstance) && CanEnterNode(LinkTransition.TargetNodeIndex, EvaluationContext))
			{
				//We cannot allow taking any transition that leads to a conduit not connected to a state
				//If the target is a conduit, we should recursively check if it ends up in a valid state
				const FPaperZDAnimStateMachineNode& TargetNode = CachedStateMachine->Nodes[LinkTransition.TargetNodeIndex];
				if (TargetNode.bConduit)
				{
					FNodeEvaluationContext ConduitEvalContext = EvaluationContext;
					ConduitEvalContext.VisitedNodes.Add(LinkTransition.TargetNodeIndex);
#if WITH_EDITORONLY_DATA
					TArray<int32> ConduitTransitionPath;
					const FPaperZDAnimStateMachineLink* ConduitLink = CheckValidTransition(LinkTransition.TargetNodeIndex, ConduitEvalContext, OutTransitionPath ? &ConduitTransitionPath : nullptr);
#else
					const FPaperZDAnimStateMachineLink* ConduitLink = CheckValidTransition(LinkTransition.TargetNodeIndex, ConduitEvalContext);
#endif
					if (ConduitLink)
					{
						EvaluationContext = MoveTemp(ConduitEvalContext);
#if WITH_EDITORONLY_DATA
						if (OutTransitionPath)
						{
							if (LinkTransition.DebugTransitionIndex != INDEX_NONE)
							{
								OutTransitionPath->Add(LinkTransition.DebugTransitionIndex);
							}
							OutTransitionPath->Append(ConduitTransitionPath);
						}
#endif
						return ConduitLink;
					}
				}
				else
				{
					//Valid target node
#if WITH_EDITORONLY_DATA
					if (OutTransitionPath && LinkTransition.DebugTransitionIndex != INDEX_NONE)
					{
						OutTransitionPath->Add(LinkTransition.DebugTransitionIndex);
					}
#endif
					return &LinkTransition;
				}
			}
		}
	}

	return nullptr;
}

#if WITH_EDITORONLY_DATA
void FPaperZDAnimNode_StateMachine::ResetDebugData()
{
	RecentTransitionIndices.Reset();
	RecentTransitionElapsedTime = 0.0f;
	ActiveTransitionIndex = INDEX_NONE;
}
#endif

void FPaperZDAnimNode_StateMachine::CallEvent(FName Name, const FPaperZDAnimationBaseContext& Context)
{
	UFunction* FoundFunction = Context.AnimInstance->FindFunction(Name);
	if (FoundFunction)
	{
		//Create a buffer just in case (if we send a null buffer, the system will crash if the event has parameters)
 		uint8* Buffer = (uint8*)FMemory_Alloca(FoundFunction->ParmsSize);
 		FMemory::Memzero(Buffer, FoundFunction->ParmsSize);
		Context.AnimInstance->ProcessEvent(FoundFunction, Buffer);
	}
}
