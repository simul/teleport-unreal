
// Duplicated from ReflectionEnvironment.cpp.

struct fReflectionCaptureCache
{
public:
	const FCaptureComponentSceneState *Find(const FGuid &MapBuildDataId) const;
	FCaptureComponentSceneState *Find(const FGuid &MapBuildDataId);

	const FCaptureComponentSceneState *Find(const UReflectionCaptureComponent *Component) const;
	FCaptureComponentSceneState *Find(const UReflectionCaptureComponent *Component);
	const FCaptureComponentSceneState &FindChecked(const UReflectionCaptureComponent *Component) const;
	FCaptureComponentSceneState &FindChecked(const UReflectionCaptureComponent *Component);

	FCaptureComponentSceneState &Add(const UReflectionCaptureComponent *Component, const FCaptureComponentSceneState &Value);
	FCaptureComponentSceneState *AddReference(const UReflectionCaptureComponent *Component);
	bool Remove(const UReflectionCaptureComponent *Component);
	int32 Prune(const TSet<FGuid> KeysToKeep, TArray<int32> &ReleasedIndices);

	int32 GetKeys(TArray<FGuid> &OutKeys) const;
	int32 GetKeys(TSet<FGuid> &OutKeys) const;

	void Empty();

protected:
	bool RemapRegisteredComponentMapBuildDataId(const UReflectionCaptureComponent *Component);
	void RegisterComponentMapBuildDataId(const UReflectionCaptureComponent *Component);
	void UnregisterComponentMapBuildDataId(const UReflectionCaptureComponent *Component);

	// Different map build data id of a capture might share the same capture component while editing (e.g., when they move).
	// need to replace it with the new one.
	TMap<const UReflectionCaptureComponent *, FGuid> RegisteredComponentMapBuildDataIds;

	TMap<FGuid, FReflectionCaptureCacheEntry> CaptureData;
};


const FCaptureComponentSceneState *fReflectionCaptureCache::Find(const FGuid &MapBuildDataId) const
{
	const FReflectionCaptureCacheEntry *Entry = CaptureData.Find(MapBuildDataId);
	if (Entry == nullptr)
		return nullptr;

	return &(Entry->SceneState);
}

FCaptureComponentSceneState *fReflectionCaptureCache::Find(const FGuid &MapBuildDataId)
{
	FReflectionCaptureCacheEntry *Entry = CaptureData.Find(MapBuildDataId);
	if (Entry == nullptr)
		return nullptr;

	return &(Entry->SceneState);
}

const FCaptureComponentSceneState *fReflectionCaptureCache::Find(const UReflectionCaptureComponent *Component) const
{
	if (!Component) // Intentionally not IsValid(Component), as this often occurs when Component is explicitly PendingKill.
		return nullptr;

	const FCaptureComponentSceneState *SceneState = Find(Component->MapBuildDataId);
	if (SceneState)
	{
		return SceneState;
	}

	const FGuid *Guid = RegisteredComponentMapBuildDataIds.Find(Component);
	if (Guid)
	{
		return Find(*Guid);
	}

	return nullptr;
}

FCaptureComponentSceneState *fReflectionCaptureCache::Find(const UReflectionCaptureComponent *Component)
{
	if (!Component) // Intentionally not IsValid(Component), as this often occurs when Component is explicitly PendingKill.
		return nullptr;

	FCaptureComponentSceneState *SceneState = Find(Component->MapBuildDataId);
	if (SceneState)
	{
		return SceneState;
	}

	const FGuid *Guid = RegisteredComponentMapBuildDataIds.Find(Component);
	if (Guid)
	{
		return Find(*Guid);
	}
	return nullptr;
}

const FCaptureComponentSceneState &fReflectionCaptureCache::FindChecked(const UReflectionCaptureComponent *Component) const
{
	const FCaptureComponentSceneState *Found = Find(Component);
	check(Found);

	return *Found;
}

FCaptureComponentSceneState &fReflectionCaptureCache::FindChecked(const UReflectionCaptureComponent *Component)
{
	FCaptureComponentSceneState *Found = Find(Component);
	check(Found);

	return *Found;
}

FCaptureComponentSceneState &fReflectionCaptureCache::Add(const UReflectionCaptureComponent *Component, const FCaptureComponentSceneState &Value)
{
	// During Reflection Capture Placement in editor, this is potentially not IsValid
	//  So just check to make sure that the pointer is non-null
	check(Component)

		FCaptureComponentSceneState *Existing = AddReference(Component);
	if (Existing != nullptr)
	{
		return *Existing;
	}
	else
	{
		FReflectionCaptureCacheEntry &Entry = CaptureData.Add(Component->MapBuildDataId, {1, Value});
		RegisterComponentMapBuildDataId(Component);
		return Entry.SceneState;
	}
}

FCaptureComponentSceneState *fReflectionCaptureCache::AddReference(const UReflectionCaptureComponent *Component)
{
	// During Reflection Capture Placement in editor, this is potentially not IsValid
	//  So just check to make sure that the pointer is non-null
	check(Component)

		bool Remap = RemapRegisteredComponentMapBuildDataId(Component);
	FReflectionCaptureCacheEntry *Found = CaptureData.Find(Component->MapBuildDataId);
	if (Found == nullptr)
		return nullptr;

	// Should not add reference count if this is caused by capture rebuilt
	if (!Remap)
	{
		Found->RefCount += 1;
	}

	return &(Found->SceneState);
}

int32 fReflectionCaptureCache::GetKeys(TArray<FGuid> &OutKeys) const
{
	return CaptureData.GetKeys(OutKeys);
}

int32 fReflectionCaptureCache::GetKeys(TSet<FGuid> &OutKeys) const
{
	return CaptureData.GetKeys(OutKeys);
}

void fReflectionCaptureCache::Empty()
{
	CaptureData.Empty();
	RegisteredComponentMapBuildDataIds.Empty();
}

int32 fReflectionCaptureCache::Prune(const TSet<FGuid> KeysToKeep, TArray<int32> &ReleasedIndices)
{
	TSet<FGuid> ExistingKeys;
	CaptureData.GetKeys(ExistingKeys);

	TSet<FGuid> KeysToRemove = ExistingKeys.Difference(KeysToKeep);
	ReleasedIndices.Empty();
	ReleasedIndices.Reserve(KeysToRemove.Num());

	for (const FGuid &Key : KeysToRemove)
	{
		FReflectionCaptureCacheEntry *Found = CaptureData.Find(Key);
		if (Found == nullptr)
			continue;

		int32 CubemapIndex = Found->SceneState.CubemapIndex;
		if (CubemapIndex != -1)
			ReleasedIndices.Add(CubemapIndex);

		CaptureData.Remove(Key);
	}

	return ReleasedIndices.Num();
}

bool fReflectionCaptureCache::Remove(const UReflectionCaptureComponent *Component)
{
	if (!Component) // Intentionally not IsValid(Component), as this often occurs when Component is explicitly PendingKill.
		return false;

	RemapRegisteredComponentMapBuildDataId(Component);
	FReflectionCaptureCacheEntry *Found = CaptureData.Find(Component->MapBuildDataId);
	if (Found == nullptr)
		return false;

	if (Found->RefCount > 1)
	{
		Found->RefCount -= 1;
		return false;
	}
	else
	{
		CaptureData.Remove(Component->MapBuildDataId);
		UnregisterComponentMapBuildDataId(Component);
		return true;
	}
}

void fReflectionCaptureCache::RegisterComponentMapBuildDataId(const UReflectionCaptureComponent *Component)
{
	RegisteredComponentMapBuildDataIds.Add(Component, Component->MapBuildDataId);
}

bool fReflectionCaptureCache::RemapRegisteredComponentMapBuildDataId(const UReflectionCaptureComponent *Component)
{
	check(Component);

	// Remap old guid to new guid when the component is the same pointer.
	FGuid *OldBuildId = RegisteredComponentMapBuildDataIds.Find(Component);
	if (OldBuildId &&
		*OldBuildId != Component->MapBuildDataId)
	{
		FGuid OldBuildIdCopy = *OldBuildId;
		const FReflectionCaptureCacheEntry *Current = CaptureData.Find(OldBuildIdCopy);
		if (Current == nullptr)
			return false; // No current entry to remap to, so no remap to perform

		// Remap all pointers that point to the old guid to the new one.
		int32 ReferenceCount = 0;
		for (TPair<const UReflectionCaptureComponent *, FGuid> &item : RegisteredComponentMapBuildDataIds)
		{
			if (item.Value == OldBuildIdCopy)
			{
				item.Value = Component->MapBuildDataId;
				ReferenceCount++;
			}
		}

		FReflectionCaptureCacheEntry Entry = *Current;
		Entry.RefCount = ReferenceCount;

		CaptureData.Remove(OldBuildIdCopy);
		CaptureData.Shrink();
		CaptureData.Add(Component->MapBuildDataId, Entry);

		return true;
	}

	return false;
}

void fReflectionCaptureCache::UnregisterComponentMapBuildDataId(const UReflectionCaptureComponent *Component)
{
	RegisteredComponentMapBuildDataIds.Remove(Component);
}