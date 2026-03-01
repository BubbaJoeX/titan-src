// ======================================================================
//
// UpdateCellLightsMessage.cpp
//
// Copyright 2026 Titan
//
// ======================================================================

#include "sharedNetworkMessages/FirstSharedNetworkMessages.h"
#include "sharedNetworkMessages/UpdateCellLightsMessage.h"

// ======================================================================

UpdateCellLightsMessage::UpdateCellLightsMessage(NetworkId const &cellId, float r, float g, float b, float brightness) :
	GameNetworkMessage("UpdateCellLightsMessage"),
	m_cellId(cellId),
	m_r(r),
	m_g(g),
	m_b(b),
	m_brightness(brightness)
{
	AutoByteStream::addVariable(m_cellId);
	AutoByteStream::addVariable(m_r);
	AutoByteStream::addVariable(m_g);
	AutoByteStream::addVariable(m_b);
	AutoByteStream::addVariable(m_brightness);
}

// ----------------------------------------------------------------------

UpdateCellLightsMessage::UpdateCellLightsMessage(Archive::ReadIterator & source) :
	GameNetworkMessage("UpdateCellLightsMessage"),
	m_cellId(NetworkId::cms_invalid),
	m_r(1.0f),
	m_g(1.0f),
	m_b(1.0f),
	m_brightness(1.0f)
{
	AutoByteStream::addVariable(m_cellId);
	AutoByteStream::addVariable(m_r);
	AutoByteStream::addVariable(m_g);
	AutoByteStream::addVariable(m_b);
	AutoByteStream::addVariable(m_brightness);
	unpack(source);
}

// ----------------------------------------------------------------------

UpdateCellLightsMessage::~UpdateCellLightsMessage()
{
}

// ======================================================================
