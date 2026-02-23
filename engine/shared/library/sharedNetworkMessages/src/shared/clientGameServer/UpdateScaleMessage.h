// ======================================================================
//
// UpdateScaleMessage.h
//
// ======================================================================

#ifndef INCLUDED_UpdateScaleMessage_H
#define INCLUDED_UpdateScaleMessage_H

// ======================================================================

#include "sharedMath/Vector.h"
#include "sharedMathArchive/VectorArchive.h"
#include "sharedNetworkMessages/GameNetworkMessage.h"

// ======================================================================

class UpdateScaleMessage : public GameNetworkMessage
{
public:
	UpdateScaleMessage(NetworkId const &networkId, Vector const &scale);
	explicit UpdateScaleMessage(Archive::ReadIterator &source);
	~UpdateScaleMessage();

	NetworkId const &getNetworkId() const;
	Vector const &getScale() const;

private:
	Archive::AutoVariable<NetworkId> m_networkId;
	Archive::AutoVariable<Vector> m_scale;

	UpdateScaleMessage(UpdateScaleMessage const &);
	UpdateScaleMessage &operator=(UpdateScaleMessage const &);
};

// ======================================================================

inline NetworkId const &UpdateScaleMessage::getNetworkId() const
{
	return m_networkId.get();
}

inline Vector const &UpdateScaleMessage::getScale() const
{
	return m_scale.get();
}

// ======================================================================

#endif
