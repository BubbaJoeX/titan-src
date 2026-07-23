// ======================================================================
//
// DynamicBunkerRoomCatalog.h
// copyright 2026 Titan
//
// Runtime discovery of graftable rooms from object templates + POB cells.
// No per-room datatable required.
// ======================================================================

#ifndef INCLUDED_DynamicBunkerRoomCatalog_H
#define INCLUDED_DynamicBunkerRoomCatalog_H

#include "sharedNetworkMessages/DynamicBunkerMessages.h"

#include <string>

// ======================================================================

class DynamicBunkerRoomCatalog
{
public:

	// roomId format: "dyn|<donorPob>|<cellIndex>|<portalIndex>"
	static bool parseRoomId(std::string const &roomId, std::string &outDonorPob, int &outDonorCellIndex, int &outDonorPortalIndex);
	static bool resolveDonorPobPath(std::string const &inPath, std::string &outPath);

	static void buildCatalog(DynamicBunkerOpenFloorplanMessage::RoomList &outRooms);
	static bool lookupRoom(std::string const &roomId, DynamicBunkerOpenFloorplanMessage::RoomEntry &outRoom);

	// Force rebuild on next open (after content hotload, etc.)
	static void invalidateCache();

private:

	DynamicBunkerRoomCatalog();
	DynamicBunkerRoomCatalog(DynamicBunkerRoomCatalog const &);
	DynamicBunkerRoomCatalog &operator =(DynamicBunkerRoomCatalog const &);
};

// ======================================================================

#endif
