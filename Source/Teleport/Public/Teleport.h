// Copyright 2018-2024 Simul.co

#pragma once
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
//#pragma optimize("", off)
class GeometrySource;

class ITeleport : public IModuleInterface
{
public:
	static inline ITeleport& Get()
	{
		return FModuleManager::LoadModuleChecked<ITeleport>("Teleport");
	}

	static inline bool IsAvailable()
	{
		return FModuleManager::Get().IsModuleLoaded("Teleport");
	}

	virtual GeometrySource* GetGeometrySource() = 0;
};
