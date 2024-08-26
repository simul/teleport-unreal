// Copyright 2018-2024 Teleport XR Ltd

#pragma once
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include <string>
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

namespace teleport
{
	namespace unreal
	{
		extern std::string ToStdString(const FString &fstr);
		extern FString ToFString(const std::string &str);
	}
} 