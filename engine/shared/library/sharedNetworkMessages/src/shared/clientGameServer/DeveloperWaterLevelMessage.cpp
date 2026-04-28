// ======================================================================
//
// DeveloperWaterLevelMessage.cpp
//
// ======================================================================

#include "sharedNetworkMessages/FirstSharedNetworkMessages.h"
#include "sharedNetworkMessages/DeveloperWaterLevelMessage.h"

#include "Archive/Archive.h"

// ----------------------------------------------------------------------

namespace Archive
{
	void get (ReadIterator & source, LocalWaterTablePatch & target)
	{
		get (source, target.centerX);
		get (source, target.centerZ);
		get (source, target.radius);
		get (source, target.deltaMeters);
	}

	void put (ByteStream & target, LocalWaterTablePatch const & source)
	{
		put (target, source.centerX);
		put (target, source.centerZ);
		put (target, source.radius);
		put (target, source.deltaMeters);
	}
}

// ----------------------------------------------------------------------

char const * const DeveloperWaterLevelMessage::cms_name = "DeveloperWaterLevelMessage";

DeveloperWaterLevelMessage::DeveloperWaterLevelMessage (std::vector<LocalWaterTablePatch> const & patches) :
	GameNetworkMessage (DeveloperWaterLevelMessage::cms_name),
	m_patches ()
{
	addVariable (m_patches);
	m_patches.set (patches);
}

DeveloperWaterLevelMessage::DeveloperWaterLevelMessage (Archive::ReadIterator & source) :
	GameNetworkMessage (DeveloperWaterLevelMessage::cms_name),
	m_patches ()
{
	addVariable (m_patches);
	unpack (source);
}

DeveloperWaterLevelMessage::~DeveloperWaterLevelMessage ()
{
}

std::vector<LocalWaterTablePatch> DeveloperWaterLevelMessage::getPatches () const
{
	return m_patches.get ();
}
