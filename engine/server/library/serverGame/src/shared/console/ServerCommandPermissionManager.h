// ======================================================================
//
// ServerCommandPermissionManager.h
// copyright (c) 2003 Sony Online Entertainment
//
// ======================================================================

#ifndef INCLUDED_ServerCommandPermissionManager_H
#define INCLUDED_ServerCommandPermissionManager_H


#include "sharedCommandParser/CommandPermissionManager.h"

#include <string>

class DataTable;

class ServerCommandPermissionManager : public CommandPermissionManager
{

public:
	ServerCommandPermissionManager ();
	virtual ~ServerCommandPermissionManager ();

	/**
	* the commandPath is a period-seperated path down through the command tree
	*/

	virtual bool                 isCommandAllowed         (const NetworkId & userId, const Unicode::String & commandPath) const;

	/** @return required staff level from command_permissions.iff, or -1 if the command is not listed. */
	int                          lookupPermissionLevel    (std::string const & commandPath) const;

	static ServerCommandPermissionManager * getInstance ();
	static void                  setInstance             (ServerCommandPermissionManager * mgr);

protected:
	ServerCommandPermissionManager (const ServerCommandPermissionManager & rhs);
	ServerCommandPermissionManager &   operator=(const ServerCommandPermissionManager & rhs);

private:

	DataTable*                  m_permissionTable;
	static ServerCommandPermissionManager * ms_instance;
};


#endif
