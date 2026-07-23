// ======================================================================
//
// DynamicBunker.h
// copyright 2026 Titan
//
// ======================================================================

#ifndef INCLUDED_DynamicBunker_H
#define INCLUDED_DynamicBunker_H

class Client;
class DynamicBunkerAssignRoomMessage;
class NetworkId;
class ServerObject;

#include <string>

// ======================================================================

class DynamicBunker
{
public:

	static bool addRoomHook(
		ServerObject &building,
		int hostCellIndex,
		int hostPortalIndex,
		char const *donorPobName,
		int donorCellIndex,
		int donorPortalIndex,
		NetworkId &outCellId);

	static bool linkExistingPortals(
		ServerObject &building,
		int cellIndexA,
		int portalIndexA,
		int cellIndexB,
		int portalIndexB);

	static int getPortalCount(ServerObject const &building, int cellIndex);
	static bool getCellAppearanceName(ServerObject const &building, int cellIndex, std::string &outName);

	// Open the floorplan UI for a player (catalog + snap sockets).
	static bool openFloorplan(Client &client, ServerObject &building, ServerObject &terminal, int selectedCellIndex, int selectedPortalIndex);

	// Handle client Assign from the floorplan UI.
	static void handleAssignRoom(Client &client, DynamicBunkerAssignRoomMessage const &message);

	// Re-apply graft descriptors stored on the building after load.
	static void restoreGraftsFromObjVars(ServerObject &building);

private:

	DynamicBunker();
	DynamicBunker(DynamicBunker const &);
	DynamicBunker &operator =(DynamicBunker const &);
};

// ======================================================================

#endif
