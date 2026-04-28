// ======================================================================
//
// DeveloperWaterLevelMessage.cpp
//
// ======================================================================

#include "sharedNetworkMessages/FirstSharedNetworkMessages.h"
#include "sharedNetworkMessages/DeveloperWaterLevelMessage.h"

char const * const DeveloperWaterLevelMessage::cms_name = "DeveloperWaterLevelMessage";

DeveloperWaterLevelMessage::DeveloperWaterLevelMessage (float const deltaMeters) :
	GameNetworkMessage (DeveloperWaterLevelMessage::cms_name),
	m_deltaMeters (deltaMeters)
{
	addVariable (m_deltaMeters);
}

DeveloperWaterLevelMessage::DeveloperWaterLevelMessage (Archive::ReadIterator & source) :
	GameNetworkMessage (DeveloperWaterLevelMessage::cms_name),
	m_deltaMeters ()
{
	addVariable (m_deltaMeters);
	unpack (source);
}

DeveloperWaterLevelMessage::~DeveloperWaterLevelMessage ()
{
}

float DeveloperWaterLevelMessage::getDeltaMeters () const
{
	return m_deltaMeters.get ();
}
