// ======================================================================
//
// UpdateCellLightsMessage.h
//
// Copyright 2026 Titan
//
// ======================================================================

#ifndef	_UpdateCellLightsMessage_H
#define	_UpdateCellLightsMessage_H

//-----------------------------------------------------------------------

#include "Archive/AutoDeltaByteStream.h"
#include "sharedNetworkMessages/GameNetworkMessage.h"
#include "sharedFoundation/NetworkId.h"

//-----------------------------------------------------------------------

class UpdateCellLightsMessage : public GameNetworkMessage
{
public:
	UpdateCellLightsMessage(NetworkId const &cellId, float r, float g, float b, float brightness);
	explicit UpdateCellLightsMessage(Archive::ReadIterator &source);
	~UpdateCellLightsMessage();

	NetworkId const & getCellId() const;
	float getR() const;
	float getG() const;
	float getB() const;
	float getBrightness() const;

private: 
	Archive::AutoVariable<NetworkId> m_cellId;
	Archive::AutoVariable<float>     m_r;
	Archive::AutoVariable<float>     m_g;
	Archive::AutoVariable<float>     m_b;
	Archive::AutoVariable<float>     m_brightness;
};

// ----------------------------------------------------------------------

inline NetworkId const & UpdateCellLightsMessage::getCellId() const
{
	return m_cellId.get();
}

inline float UpdateCellLightsMessage::getR() const
{
	return m_r.get();
}

inline float UpdateCellLightsMessage::getG() const
{
	return m_g.get();
}

inline float UpdateCellLightsMessage::getB() const
{
	return m_b.get();
}

inline float UpdateCellLightsMessage::getBrightness() const
{
	return m_brightness.get();
}

// ----------------------------------------------------------------------

#endif // _UpdateCellLightsMessage_H
