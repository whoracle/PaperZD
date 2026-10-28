// Copyright 2017 ~ 2022 Critical Failure Studio Ltd. All rights reserved.

#include "Compilers/Access/PaperZDAnimBPGeneratedClassAccess.h"

TArray<FPaperZDExposedValueHandler>& FPaperZDAnimBPGeneratedClassAccess::GetExposedValueHandlers() const
{
	return GeneratedClass->EvaluateGraphExposedInputs;
}

TArray<FPaperZDAnimStateMachine>& FPaperZDAnimBPGeneratedClassAccess::GetStateMachines() const
{
	return GeneratedClass->StateMachines;
}

FPaperZDAnimBPDebugData& FPaperZDAnimBPGeneratedClassAccess::GetDebugData() const
{
	return GeneratedClass->DebugData;
}

TMap<FName, FPaperZDOverrideSlotDescriptor>& FPaperZDAnimBPGeneratedClassAccess::GetRegisteredOverrideSlots() const
{
	return GeneratedClass->RegisteredOverrideSlots;
}
