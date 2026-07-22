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

#include <algorithm>

ServerCommandPermissionManager * ServerCommandPermissionManager::ms_instance = 0;

namespace
{
	std::string normalizeCommandPath(std::string const & commandPath)
	{
		return Unicode::toLower(Unicode::getTrim(commandPath));
	}
}

ServerCommandPermissionManager::ServerCommandPermissionManager() :
		CommandPermissionManager(),
		m_permissionTable(0)
{
	m_permissionTable = DataTableManager::getTable("datatables/admin/command_permissions.iff", true);
	DEBUG_FATAL(!m_permissionTable, ("Could not open command permissions table"));

	int const commandColumn = m_permissionTable->findColumnNumber("Command");
	int const levelColumn = m_permissionTable->findColumnNumber("Level");
	DEBUG_FATAL(commandColumn < 0 || levelColumn < 0, ("command_permissions.iff must contain Command and Level columns"));

	CommandParser::setPermissionManager(this);
	setInstance(this);
}

//------------------------------------------------------------------------------------------

ServerCommandPermissionManager::~ServerCommandPermissionManager()
{
	setInstance(0);
	DataTableManager::close("datatables/admin/command_permissions.iff");
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

	std::string command = Unicode::wideToNarrow(commandPath);
	int const clientLevel = client->getGodLevel();
	int const commandLevel = resolvePermissionLevel(command, 0, true);
	bool const retval = commandLevel >= 0 && commandLevel <= clientLevel;
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

	int const commandColumn = m_permissionTable->findColumnNumber("Command");
	int const levelColumn = m_permissionTable->findColumnNumber("Level");
	if (commandColumn < 0 || levelColumn < 0)
		return -1;

	std::string const normalizedPath = normalizeCommandPath(commandPath);
	for (int row = 0; row < m_permissionTable->getNumRows(); ++row)
	{
		if (normalizeCommandPath(m_permissionTable->getStringValue(commandColumn, row)) == normalizedPath)
			return m_permissionTable->getIntValue(levelColumn, row);
	}
	return -1;
}

//------------------------------------------------------------------------------------------

int ServerCommandPermissionManager::resolvePermissionLevel(std::string const & commandPath, int builtInLevel, bool requireTableEntry) const
{
	int const tableLevel = lookupPermissionLevel(commandPath);
	if (tableLevel < 0)
		return requireTableEntry ? -1 : builtInLevel;
	return std::max(builtInLevel, tableLevel);
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
