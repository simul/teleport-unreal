// Copyright 2018-2024 Simul.co
#define WIN32_LEAN_AND_MEAN

#include "Components/TeleportPawnComponent.h"

UTeleportPawnComponent::UTeleportPawnComponent()
{
}
UTeleportPawnComponent::~UTeleportPawnComponent()
{
}

void UTeleportPawnComponent::SetPoseMapping(USceneComponent *s, FString m)
{
	if(m.Len()>0)
		PoseMappings.Add(s,m);
	else
		PoseMappings.Remove(s);
}