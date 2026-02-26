// ======================================================================
//
// ClusterBroadcaster.cpp
// copyright 2024 Sony Online Entertainment
//
// ======================================================================

#include "FirstSwgFileControl.h"
#include "ClusterBroadcaster.h"

#include "ClusterConsoleConnection.h"
#include "ConfigFileControl.h"

#include "sharedFoundation/Os.h"
#include "sharedLog/Log.h"
#include "sharedNetwork/NetworkHandler.h"
#include "sharedNetworkMessages/ConsoleChannelMessages.h"

#include <cstdio>

// ======================================================================

bool                      ClusterBroadcaster::ms_installed      = false;
ClusterConsoleConnection * ClusterBroadcaster::ms_connection     = 0;
int                        ClusterBroadcaster::ms_reconnectTimer = 0;

static const int RECONNECT_INTERVAL_FRAMES = 500;  // ~5 seconds at 10ms sleep

// ======================================================================

void ClusterBroadcaster::install()
{
	if (ms_installed)
		return;

	ms_installed = true;

	if (!ConfigFileControl::getEnableClusterReload())
	{
		printf("[CLUSTER] Cluster reload disabled by config\n");
		fflush(stdout);
		return;
	}

	const char * host = ConfigFileControl::getCentralConsoleHost();
	int port = ConfigFileControl::getCentralConsolePort();

	if (!host || host[0] == '\0')
	{
		printf("[CLUSTER] No centralConsoleHost configured, cluster reload disabled\n");
		fflush(stdout);
		return;
	}

	printf("[CLUSTER] Connecting to CentralServer console at %s:%d ...\n", host, port);
	fflush(stdout);

	ms_connection = new ClusterConsoleConnection(std::string(host), static_cast<unsigned short>(port));
	ms_reconnectTimer = 0;
}

// ----------------------------------------------------------------------

void ClusterBroadcaster::remove()
{
	if (ms_connection)
	{
		ms_connection->disconnect();
		ms_connection = 0;
	}
	ms_installed = false;
}

// ----------------------------------------------------------------------

void ClusterBroadcaster::update()
{
	if (!ms_installed || !ConfigFileControl::getEnableClusterReload())
		return;

	if (!ms_connection)
	{
		++ms_reconnectTimer;
		if (ms_reconnectTimer >= RECONNECT_INTERVAL_FRAMES)
		{
			ms_reconnectTimer = 0;
			const char * host = ConfigFileControl::getCentralConsoleHost();
			int port = ConfigFileControl::getCentralConsolePort();
			if (host && host[0] != '\0')
			{
				LOG("FileControl", ("ClusterBroadcaster: reconnecting to %s:%d", host, port));
				ms_connection = new ClusterConsoleConnection(std::string(host), static_cast<unsigned short>(port));
			}
		}
		return;
	}

	if (!ms_connection->isConnected())
	{
		++ms_reconnectTimer;
		if (ms_reconnectTimer >= RECONNECT_INTERVAL_FRAMES)
		{
			ms_reconnectTimer = 0;
			ms_connection->disconnect();
			ms_connection = 0;
		}
	}
}

// ----------------------------------------------------------------------

bool ClusterBroadcaster::isConnected()
{
	return ms_connection && ms_connection->isConnected();
}

// ----------------------------------------------------------------------

bool ClusterBroadcaster::ensureConnected()
{
	if (isConnected())
		return true;

	if (!ConfigFileControl::getEnableClusterReload())
		return false;

	const char * host = ConfigFileControl::getCentralConsoleHost();
	int port = ConfigFileControl::getCentralConsolePort();

	if (!host || host[0] == '\0')
		return false;

	if (ms_connection)
	{
		ms_connection->disconnect();
		ms_connection = 0;
	}

	ms_connection = new ClusterConsoleConnection(std::string(host), static_cast<unsigned short>(port));

	// Pump the network briefly to allow the connection to establish
	for (int i = 0; i < 50; ++i)
	{
		NetworkHandler::update();
		NetworkHandler::dispatch();
		if (ms_connection->isConnected())
			return true;
		Os::sleep(100);
	}

	LOG("FileControl", ("ClusterBroadcaster: failed to connect to CentralServer console at %s:%d", host, port));
	return ms_connection->isConnected();
}

// ----------------------------------------------------------------------

bool ClusterBroadcaster::sendConsoleCommand(const std::string & command)
{
	if (!ensureConnected())
	{
		LOG("FileControl", ("ClusterBroadcaster: cannot send command, not connected to CentralServer"));
		printf("[CLUSTER] Not connected to CentralServer console, cannot send: %s\n", command.c_str());
		fflush(stdout);
		return false;
	}

	LOG("FileControl", ("ClusterBroadcaster: sending command: %s", command.c_str()));
	printf("[CLUSTER] Sending: %s\n", command.c_str());
	fflush(stdout);

	ConGenericMessage msg(command);
	Archive::ByteStream bs;
	msg.pack(bs);
	ms_connection->send(bs, true);

	return true;
}

// ======================================================================
// Reload commands
//
// The command format is: "planet all game <command> <arg>"
// CentralServer forwards this to all PlanetServers, which forward to
// a GameServer. The GameServer's CentralCommandParserGame handles it:
//   - reloadScript <scriptName>     -> GameScriptObject::reloadScript + broadcast
//   - reloadServerTemplate <name>   -> ObjectTemplateList::reload + broadcast
//   - reloadTable <tableName>       -> DataTableManager::reload + broadcast
// ======================================================================

bool ClusterBroadcaster::broadcastReloadScript(const std::string & scriptName)
{
	std::string cmd = "planet all game reloadScript " + scriptName;
	return sendConsoleCommand(cmd);
}

// ----------------------------------------------------------------------

bool ClusterBroadcaster::broadcastReloadDatatable(const std::string & datatableName)
{
	std::string cmd = "planet all game reloadTable " + datatableName;
	return sendConsoleCommand(cmd);
}

// ----------------------------------------------------------------------

bool ClusterBroadcaster::broadcastReloadTemplate(const std::string & templateName)
{
	std::string cmd = "planet all game reloadServerTemplate " + templateName;
	return sendConsoleCommand(cmd);
}

// ======================================================================
