// ======================================================================
//
// ClusterConsoleConnection.h
// copyright 2024 Sony Online Entertainment
//
// Lightweight connection from SwgFileControl to CentralServer's console
// port. Used to send reload commands that CentralServer forwards to
// all planet/game servers.
//
// ======================================================================

#ifndef INCLUDED_ClusterConsoleConnection_H
#define INCLUDED_ClusterConsoleConnection_H

// ======================================================================

#include "sharedNetwork/Connection.h"

// ======================================================================

class ClusterConsoleConnection : public Connection
{
public:

	ClusterConsoleConnection(const std::string & address, unsigned short port);
	virtual ~ClusterConsoleConnection();

	virtual void onConnectionClosed();
	virtual void onConnectionOpened();
	virtual void onReceive(const Archive::ByteStream & bs);

	bool isConnected() const;

private:

	ClusterConsoleConnection(const ClusterConsoleConnection &);
	ClusterConsoleConnection & operator=(const ClusterConsoleConnection &);

	bool m_connected;
};

// ======================================================================

inline bool ClusterConsoleConnection::isConnected() const
{
	return m_connected;
}

// ======================================================================

#endif
