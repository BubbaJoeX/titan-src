// ======================================================================
//
// ClaimManipulateStateMessage.cpp
//
// ======================================================================

#include "sharedNetworkMessages/FirstSharedNetworkMessages.h"
#include "sharedNetworkMessages/ClaimManipulateStateMessage.h"

// ======================================================================

char const * const ClaimManipulateStateMessage::cms_name = "ClaimManipulateStateMessage";

// ======================================================================

ClaimManipulateStateMessage::ClaimManipulateStateMessage(bool const canManipulate) :
	GameNetworkMessage(cms_name),
	m_canManipulate(canManipulate)
{
	addVariable(m_canManipulate);
}

// ----------------------------------------------------------------------

ClaimManipulateStateMessage::ClaimManipulateStateMessage(Archive::ReadIterator & source) :
	GameNetworkMessage(cms_name),
	m_canManipulate(false)
{
	addVariable(m_canManipulate);
	unpack(source);
}

// ----------------------------------------------------------------------

ClaimManipulateStateMessage::~ClaimManipulateStateMessage()
{
}

// ----------------------------------------------------------------------

bool ClaimManipulateStateMessage::getCanManipulate() const
{
	return m_canManipulate.get();
}

// ======================================================================
