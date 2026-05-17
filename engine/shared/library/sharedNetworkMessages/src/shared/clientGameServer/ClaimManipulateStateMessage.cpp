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
	m_canManipulate(canManipulate ? static_cast<int8>(1) : static_cast<int8>(0))
{
	addVariable(m_canManipulate);
}

// ----------------------------------------------------------------------

ClaimManipulateStateMessage::ClaimManipulateStateMessage(Archive::ReadIterator & source) :
	GameNetworkMessage(cms_name),
	m_canManipulate(static_cast<int8>(0))
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
	return m_canManipulate.get() != 0;
}

// ======================================================================
