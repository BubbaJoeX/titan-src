// ======================================================================
//
// ClusterBroadcaster.h
// copyright 2024 Sony Online Entertainment
//
// Manages the connection from SwgFileControl to CentralServer's console
// and provides methods to broadcast reload commands to all game servers.
//
// The command path is:
//   SwgFileControl -> CentralServer console -> PlanetServer -> GameServer
//   GameServer executes reload locally + broadcasts to all other GameServers
//
// ======================================================================

#ifndef INCLUDED_ClusterBroadcaster_H
#define INCLUDED_ClusterBroadcaster_H

// ======================================================================

#include <string>

class ClusterConsoleConnection;

// ======================================================================

class ClusterBroadcaster
{
public:

	static void install();
	static void remove();
	static void update();

	static bool isConnected();
	static bool ensureConnected();

	static bool broadcastReloadScript(const std::string & scriptName);
	static bool broadcastReloadDatatable(const std::string & datatableName);
	static bool broadcastReloadTemplate(const std::string & templateName);

	static bool sendConsoleCommand(const std::string & command);

private:

	ClusterBroadcaster();
	ClusterBroadcaster(const ClusterBroadcaster &);
	ClusterBroadcaster & operator=(const ClusterBroadcaster &);

	static bool                      ms_installed;
	static ClusterConsoleConnection * ms_connection;
	static int                        ms_reconnectTimer;
};

// ======================================================================

#endif
