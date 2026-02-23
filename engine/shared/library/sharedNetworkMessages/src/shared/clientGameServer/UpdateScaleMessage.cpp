// ======================================================================
//
// UpdateScaleMessage.cpp
//
// ======================================================================

#include "sharedNetworkMessages/FirstSharedNetworkMessages.h"
#include "sharedNetworkMessages/UpdateScaleMessage.h"

#include "sharedMath/Vector.h"

// ======================================================================

UpdateScaleMessage::UpdateScaleMessage(NetworkId const &networkId, Vector const &scale) :
	GameNetworkMessage("UpdateScaleMessage"),
	m_networkId(networkId),
	m_scale(scale)
{
	addVariable(m_networkId);
	addVariable(m_scale);
}

// ----------------------------------------------------------------------

UpdateScaleMessage::UpdateScaleMessage(Archive::ReadIterator &source) :
	GameNetworkMessage("UpdateScaleMessage"),
	m_networkId(),
	m_scale()
{
	addVariable(m_networkId);
	addVariable(m_scale);
	unpack(source);
}

// ----------------------------------------------------------------------

UpdateScaleMessage::~UpdateScaleMessage()
{
}

// ======================================================================
