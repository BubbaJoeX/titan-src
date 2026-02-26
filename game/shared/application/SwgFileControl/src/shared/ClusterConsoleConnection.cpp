// ======================================================================
//
// ClusterConsoleConnection.cpp
// copyright 2024 Sony Online Entertainment
//
// ======================================================================

#include "FirstSwgFileControl.h"
#include "ClusterConsoleConnection.h"

#include "sharedFoundation/CrcConstexpr.hpp"
#include "sharedLog/Log.h"
#include "sharedNetwork/NetworkSetupData.h"
#include "sharedNetworkMessages/ConsoleChannelMessages.h"

#include <cstdio>

// ======================================================================

ClusterConsoleConnection::ClusterConsoleConnection(const std::string & address, unsigned short port) :
	Connection(address, port, NetworkSetupData()),
	m_connected(false)
{
}

// ----------------------------------------------------------------------

ClusterConsoleConnection::~ClusterConsoleConnection()
{
	m_connected = false;
}

// ----------------------------------------------------------------------

void ClusterConsoleConnection::onConnectionOpened()
{
	m_connected = true;
	LOG("FileControl", ("ClusterConsole: connected to CentralServer"));
	printf("[CLUSTER] Connected to CentralServer console\n");
	fflush(stdout);
}

// ----------------------------------------------------------------------

void ClusterConsoleConnection::onConnectionClosed()
{
	m_connected = false;
	LOG("FileControl", ("ClusterConsole: disconnected from CentralServer"));
	printf("[CLUSTER] Disconnected from CentralServer console\n");
	fflush(stdout);
}

// ----------------------------------------------------------------------

void ClusterConsoleConnection::onReceive(const Archive::ByteStream & bs)
{
	Archive::ReadIterator ri = bs.begin();
	GameNetworkMessage m(ri);
	ri = bs.begin();

	const uint32 messageType = m.getType();

	switch (messageType)
	{
	case constcrc("ConGenericMessage"):
		{
			ConGenericMessage msg(ri);
			LOG("FileControl", ("ClusterConsole response: %s", msg.getMsg().c_str()));
			printf("[CLUSTER] %s", msg.getMsg().c_str());
			fflush(stdout);
		}
		break;

	case constcrc("RequestDisconnect"):
		LOG("FileControl", ("ClusterConsole: server requested disconnect"));
		break;

	default:
		LOG("FileControl", ("ClusterConsole: unknown message type 0x%08x", messageType));
		break;
	}
}

// ======================================================================
