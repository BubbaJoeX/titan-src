// ======================================================================
//
// ServerCommandPermissionManager.cpp
// copyright (c) 2003 Sony Online Entertainment
//
// =====================================================================

#include "serverGame/FirstServerGame.h"
#include "serverGame/ServerCommandPermissionManager.h"

#include "serverGame/Client.h"
#include "serverGame/PlayerObject.h"
#include "serverGame/ServerObject.h"
#include "serverGame/ServerWorld.h"
#include "sharedCommandParser/CommandParser.h"
#include "sharedLog/Log.h"
#include "sharedUtility/DataTable.h"
#include "sharedUtility/DataTableManager.h"

#include "UnicodeUtils.h"

ServerCommandPermissionManager * ServerCommandPermissionManager::ms_instance = 0;

ServerCommandPermissionManager::ServerCommandPermissionManager() :
		CommandPermissionManager(),
		m_permissionTable(0)
{
	m_permissionTable = DataTableManager::getTable("datatables/admin/command_permissions.iff", true);
	DEBUG_FATAL(!m_permissionTable, ("Could not open command permissions table"));
	CommandParser::setPermissionManager(this);
	setInstance(this);
}

//------------------------------------------------------------------------------------------

ServerCommandPermissionManager::~ServerCommandPermissionManager()
{
	setInstance(0);
	DataTableManager::close("command_permissions.iff");
	CommandParser::setPermissionManager(0);
}


//------------------------------------------------------------------------------------------

bool ServerCommandPermissionManager::isCommandAllowed (const NetworkId & userId, const Unicode::String & commandPath) const
{
	// commands sent from the ServerConsole program don't have a client associated from them
	// although it is possible to  resolve to a ServerObject
	std::string cmd = Unicode::wideToNarrow(commandPath);
	if( cmd == "game" )
	{
		ServerObject * tmpu = ServerWorld::findObjectByNetworkId(userId);
		// a ServerConsole command sometimes won't have a ServerObject (first time a cluster receives a command)
		// a ServerConsole command will never have a Client associated with it
		if( !tmpu || !tmpu->getClient())
		{
			LOG("ServerCommandPermissionManager", ("Allowing permission to execute ServerConsole command.") );
			return true;
		}
		else
		{
			LOG("ServerCommandPermissionManager", ("Disallowing permission to execute ServerConsole command because it has a Client associated with it."));
			return false;
		}
	}

	ServerObject * user = ServerWorld::findObjectByNetworkId(userId);
	if (!user)
		return false;

	Client* client = user->getClient();
	if (!client)
		return false;

	int clientLevel = client->getGodLevel();
	std::string command = Unicode::wideToNarrow(commandPath);
	int row = m_permissionTable->searchColumnString( 0, command);
	// Commands not listed in command_permissions must not be executable via the in-game console path.
	int const commandLevelNotListed = 1000;
	int commandLevel = commandLevelNotListed;
	
	if (row != -1)
		commandLevel = m_permissionTable->getIntValue(1, row);

	bool retval =  (commandLevel <= clientLevel);
	if (!retval)
	{
		LOG("CustomerService",("Avatar:%s denied command %s because the command level is %d and they are %d", PlayerObject::getAccountDescription(userId).c_str(), command.c_str(), commandLevel, clientLevel));
	}
	return retval;
}

//------------------------------------------------------------------------------------------

int ServerCommandPermissionManager::lookupPermissionLevel(std::string const & commandPath) const
{
	if (!m_permissionTable)
		return -1;
	int row = m_permissionTable->searchColumnString(0, commandPath);
	if (row == -1)
		return -1;
	return m_permissionTable->getIntValue(1, row);
}

//------------------------------------------------------------------------------------------

ServerCommandPermissionManager * ServerCommandPermissionManager::getInstance()
{
	return ms_instance;
}

//------------------------------------------------------------------------------------------

void ServerCommandPermissionManager::setInstance(ServerCommandPermissionManager * mgr)
{
	ms_instance = mgr;
}


//------------------------------------------------------------------------------------------
