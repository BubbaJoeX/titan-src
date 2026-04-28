// ======================================================================
//
// DeveloperWaterLevelMessage.h
// Server -> client: additive water plane height delta (meters).
//
// ======================================================================

#ifndef INCLUDED_DeveloperWaterLevelMessage_H
#define INCLUDED_DeveloperWaterLevelMessage_H

#include "Archive/AutoDeltaByteStream.h"
#include "sharedNetworkMessages/GameNetworkMessage.h"

class DeveloperWaterLevelMessage : public GameNetworkMessage
{
public:

	explicit DeveloperWaterLevelMessage (float deltaMeters);
	explicit DeveloperWaterLevelMessage (Archive::ReadIterator & source);
	virtual ~DeveloperWaterLevelMessage ();

	float getDeltaMeters () const;

	static char const * const cms_name;

private:

	Archive::AutoVariable<float> m_deltaMeters;
};

#endif
