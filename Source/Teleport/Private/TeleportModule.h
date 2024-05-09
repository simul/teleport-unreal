// Copyright 2018-2024 Simul.co

#pragma once

#include "CoreMinimal.h"
#include "Teleport.h"
class FGeometrySource;


#if !UE_BUILD_SHIPPING || !UE_BUILD_TEST
#define WITH_TELEPORT_STATS 1
#endif
namespace avs
{
	enum class LogSeverity;
}
class TELEPORT_API FTeleportModule : public ITeleport
{ 
public:
	/* Begin IModuleInterface */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	/* End IModuleInterface */
	
	FString GetPluginDir() const;
	GeometrySource* GetGeometrySource();
	// To receive log output from cout/cerr.
	static void OutputLogCallback(int severity,const char *txt);
private:
	bool LoadLibrary_libavstream();
	void* Handle_libavstream = nullptr;

	//TUniquePtr<avs::Context> Context;
	static void LogMessageHandler(avs::LogSeverity Severity, const char* Msg, void*);

};
#include "Logging/LogMacros.h"
DECLARE_LOG_CATEGORY_EXTERN(LogTeleport, Log, All);
