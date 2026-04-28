// ======================================================================
//
// DeveloperWaterLevelMessage.h
// Server -> client: full list of local water table patches.
//
// ======================================================================

#ifndef INCLUDED_DeveloperWaterLevelMessage_H
#define INCLUDED_DeveloperWaterLevelMessage_H

#include "Archive/AutoDeltaByteStream.h"
#include "sharedNetworkMessages/GameNetworkMessage.h"
#include "sharedTerrain/TerrainWaterLevelDeveloperDelta.h"

#include <vector>

class DeveloperWaterLevelMessage : public GameNetworkMessage
{
public:

	explicit DeveloperWaterLevelMessage (std::vector<LocalWaterTablePatch> const & patches);
	explicit DeveloperWaterLevelMessage (Archive::ReadIterator & source);
	virtual ~DeveloperWaterLevelMessage ();

	std::vector<LocalWaterTablePatch> getPatches () const;

	static char const * const cms_name;

private:

	Archive::AutoArray<LocalWaterTablePatch> m_patches;
};

#endif
