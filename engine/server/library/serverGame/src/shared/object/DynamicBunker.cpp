// ======================================================================
//
// DynamicBunker.cpp
// copyright 2026 Titan
//
// Runtime bunker room grafting: snap donor POB cells onto host portals.
// ======================================================================

#include "serverGame/FirstServerGame.h"
#include "serverGame/DynamicBunker.h"

#include "serverGame/CellObject.h"
#include "serverGame/Client.h"
#include "serverGame/ContainerInterface.h"
#include "serverGame/DynamicBunkerRoomCatalog.h"
#include "serverGame/ServerObject.h"
#include "serverGame/ServerWorld.h"
#include "sharedFoundation/DynamicVariableList.h"
#include "sharedLog/Log.h"
#include "sharedNetworkMessages/DynamicBunkerMessages.h"
#include "sharedObject/CellProperty.h"
#include "sharedObject/NetworkIdManager.h"
#include "sharedObject/PortalProperty.h"

#include <cstdio>
#include <vector>

// ======================================================================

namespace DynamicBunkerNamespace
{
	char const *const OV_ROOT = "dynamicBunker";
	char const *const OV_COUNT = "dynamicBunker.count";

	std::string graftKey(int index, char const *field)
	{
		char buffer[128];
		snprintf(buffer, sizeof(buffer), "%s.graft.%d.%s", OV_ROOT, index, field);
		return buffer;
	}

	void persistGrafts(ServerObject &building, PortalProperty const &portalProperty)
	{
		PortalProperty::DynamicRoomGraftList const &grafts = portalProperty.getDynamicRoomGrafts();
		building.setObjVarItem(OV_COUNT, static_cast<int>(grafts.size()));

		for (size_t i = 0; i < grafts.size(); ++i)
		{
			PortalProperty::DynamicRoomGraft const &graft = grafts[i];
			int const index = static_cast<int>(i);
			building.setObjVarItem(graftKey(index, "graftedCell"), graft.graftedCellIndex);
			building.setObjVarItem(graftKey(index, "hostCell"), graft.hostCellIndex);
			building.setObjVarItem(graftKey(index, "hostPortal"), graft.hostPortalIndex);
			building.setObjVarItem(graftKey(index, "graftPortal"), graft.graftedPortalIndex);
			building.setObjVarItem(graftKey(index, "donorCell"), graft.donorCellIndex);
			building.setObjVarItem(graftKey(index, "donorPob"), graft.donorPobName);
		}
	}

	void broadcastGraft(ServerObject &building, PortalProperty::DynamicRoomGraft const &graft, NetworkId const &cellId, Transform const &cellTransform)
	{
		DynamicBunkerGraftMessage const message(
			building.getNetworkId(),
			cellId,
			graft.graftedCellIndex,
			graft.hostCellIndex,
			graft.hostPortalIndex,
			graft.graftedPortalIndex,
			graft.donorCellIndex,
			graft.donorPobName,
			cellTransform);

		building.sendToClientsInUpdateRange(message, true, false);
	}
}

using namespace DynamicBunkerNamespace;

// ======================================================================

bool DynamicBunker::addRoomHook(ServerObject &building, int hostCellIndex, int hostPortalIndex, char const *donorPobName, int donorCellIndex, int donorPortalIndex, NetworkId &outCellId)
{
	outCellId = NetworkId::cms_invalid;
	NOT_NULL(donorPobName);

	PortalProperty *const portalProperty = building.getPortalProperty();
	if (!portalProperty)
	{
		WARNING(true, ("DynamicBunker::addRoomHook - %s has no PortalProperty", building.getNetworkId().getValueString().c_str()));
		return false;
	}

	if (hostCellIndex < 1 || hostCellIndex >= portalProperty->getNumberOfCells())
	{
		WARNING(true, ("DynamicBunker::addRoomHook - invalid host cell %d", hostCellIndex));
		return false;
	}

	if (!portalProperty->getCell(hostCellIndex))
	{
		WARNING(true, ("DynamicBunker::addRoomHook - host cell %d not loaded", hostCellIndex));
		return false;
	}

	Transform cellTransform;
	if (!portalProperty->computeGraftCellTransform(hostCellIndex, hostPortalIndex, donorPobName, donorCellIndex, donorPortalIndex, cellTransform))
	{
		WARNING(true, ("DynamicBunker::addRoomHook - failed to compute graft transform for %s", donorPobName));
		return false;
	}

	int const graftedCellIndex = portalProperty->reserveGraftedCellSlot(donorPobName, donorCellIndex);
	if (graftedCellIndex < 1)
		return false;

	ServerObject *const cellObject = ServerWorld::createNewObject("object/cell/cell.iff", Transform::identity, 0, false);
	if (!cellObject)
	{
		WARNING(true, ("DynamicBunker::addRoomHook - failed to create cell object"));
		return false;
	}

	CellObject *const cell = cellObject->asCellObject();
	NOT_NULL(cell);
	cell->setCell(graftedCellIndex);

	Container::ContainerErrorCode tmp = Container::CEC_Success;
	if (!portalProperty->addCellObject(*cellObject, tmp))
	{
		WARNING(true, ("DynamicBunker::addRoomHook - addCellObject failed (%d)", static_cast<int>(tmp)));
		cellObject->permanentlyDestroy(DeleteReasons::BadContainerTransfer);
		return false;
	}

	portalProperty->cellLoaded(graftedCellIndex, *cellObject, false);
	cellObject->setTransform_o2p(cellTransform);
	cellObject->setLoadWith(building.getNetworkId());
	if (building.isPersisted())
		cellObject->persist();

	if (!portalProperty->linkCellPortals(hostCellIndex, hostPortalIndex, graftedCellIndex, donorPortalIndex))
	{
		WARNING(true, ("DynamicBunker::addRoomHook - portal link failed"));
		return false;
	}

	PortalProperty::DynamicRoomGraft graft;
	graft.graftedCellIndex = graftedCellIndex;
	graft.hostCellIndex = hostCellIndex;
	graft.hostPortalIndex = hostPortalIndex;
	graft.graftedPortalIndex = donorPortalIndex;
	graft.donorCellIndex = donorCellIndex;
	graft.donorPobName = donorPobName;
	portalProperty->recordDynamicRoomGraft(graft);
	persistGrafts(building, *portalProperty);

	outCellId = cellObject->getNetworkId();
	broadcastGraft(building, graft, outCellId, cellTransform);

	LOG("dynamic_bunker", ("addRoomHook building=%s hostCell=%d hostPortal=%d donor=%s donorCell=%d donorPortal=%d graftedCell=%d cellId=%s",
		building.getNetworkId().getValueString().c_str(),
		hostCellIndex, hostPortalIndex, donorPobName, donorCellIndex, donorPortalIndex, graftedCellIndex,
		outCellId.getValueString().c_str()));

	return true;
}

// ----------------------------------------------------------------------

bool DynamicBunker::linkExistingPortals(ServerObject &building, int cellIndexA, int portalIndexA, int cellIndexB, int portalIndexB)
{
	PortalProperty *const portalProperty = building.getPortalProperty();
	if (!portalProperty)
		return false;

	bool const ok = portalProperty->linkCellPortals(cellIndexA, portalIndexA, cellIndexB, portalIndexB);
	if (ok)
	{
		LOG("dynamic_bunker", ("linkExistingPortals building=%s %d:%d <-> %d:%d",
			building.getNetworkId().getValueString().c_str(),
			cellIndexA, portalIndexA, cellIndexB, portalIndexB));
	}
	return ok;
}

// ----------------------------------------------------------------------

int DynamicBunker::getPortalCount(ServerObject const &building, int cellIndex)
{
	PortalProperty const *const portalProperty = building.getPortalProperty();
	if (!portalProperty || cellIndex < 1 || cellIndex >= portalProperty->getNumberOfCells())
		return 0;

	CellProperty const *const cell = portalProperty->getCell(cellIndex);
	if (!cell)
		return 0;

	return cell->getPortalCount();
}

// ----------------------------------------------------------------------

bool DynamicBunker::getCellAppearanceName(ServerObject const &building, int cellIndex, std::string &outName)
{
	PortalProperty const *const portalProperty = building.getPortalProperty();
	if (!portalProperty || cellIndex < 1 || cellIndex >= portalProperty->getNumberOfCells())
		return false;

	char const *const appearance = portalProperty->getCellAppearanceName(cellIndex);
	if (!appearance || !*appearance)
		return false;

	outName = appearance;
	return true;
}

// ----------------------------------------------------------------------

bool DynamicBunker::openFloorplan(Client &client, ServerObject &building, ServerObject &terminal, int selectedCellIndex, int selectedPortalIndex)
{
	PortalProperty const *const portalProperty = building.getPortalProperty();
	if (!portalProperty)
		return false;

	DynamicBunkerOpenFloorplanMessage::RoomList rooms;
	DynamicBunkerRoomCatalog::buildCatalog(rooms);

	PortalProperty::PortalSocketInfoList sockets;
	portalProperty->collectPortalSockets(sockets);

	DynamicBunkerOpenFloorplanMessage::SocketList socketEntries;
	socketEntries.reserve(sockets.size());
	for (size_t i = 0; i < sockets.size(); ++i)
	{
		PortalProperty::PortalSocketInfo const &socket = sockets[i];
		DynamicBunkerOpenFloorplanMessage::SocketEntry entry;
		entry.cellIndex = socket.cellIndex;
		entry.portalIndex = socket.portalIndex;
		entry.open = socket.open && socket.passable;
		char label[96];
		snprintf(label, sizeof(label), "portal %d", socket.portalIndex);
		entry.label = label;
		socketEntries.push_back(entry);
	}

	DynamicBunkerOpenFloorplanMessage const message(
		building.getNetworkId(),
		terminal.getNetworkId(),
		selectedCellIndex,
		selectedPortalIndex,
		rooms,
		socketEntries);
	client.send(message, true);

	LOG("dynamic_bunker", ("openFloorplan player=%s building=%s rooms=%d sockets=%d",
		client.getCharacterObjectId().getValueString().c_str(),
		building.getNetworkId().getValueString().c_str(),
		static_cast<int>(rooms.size()),
		static_cast<int>(socketEntries.size())));
	return true;
}

// ----------------------------------------------------------------------

void DynamicBunker::handleAssignRoom(Client &client, DynamicBunkerAssignRoomMessage const &message)
{
	ServerObject *const building = safe_cast<ServerObject *>(NetworkIdManager::getObjectById(message.getBuildingId()));
	if (!building || !building->getPortalProperty())
	{
		WARNING(true, ("DynamicBunker::handleAssignRoom - invalid building %s", message.getBuildingId().getValueString().c_str()));
		return;
	}

	DynamicBunkerOpenFloorplanMessage::RoomEntry room;
	if (!DynamicBunkerRoomCatalog::lookupRoom(message.getRoomId(), room))
	{
		WARNING(true, ("DynamicBunker::handleAssignRoom - unknown room '%s'", message.getRoomId().c_str()));
		return;
	}

	NetworkId cellId;
	if (!addRoomHook(*building, message.getHostCellIndex(), message.getHostPortalIndex(), room.donorPob.c_str(), room.donorCellIndex, room.donorPortalIndex, cellId))
	{
		WARNING(true, ("DynamicBunker::handleAssignRoom - graft failed for '%s'", message.getRoomId().c_str()));
		return;
	}

	UNREF(client);
	LOG("dynamic_bunker", ("handleAssignRoom ok room=%s cell=%s", message.getRoomId().c_str(), cellId.getValueString().c_str()));
}

// ----------------------------------------------------------------------

void DynamicBunker::restoreGraftsFromObjVars(ServerObject &building)
{
	PortalProperty *const portalProperty = building.getPortalProperty();
	if (!portalProperty)
		return;

	int count = 0;
	if (!building.getObjVars().getItem(OV_COUNT, count) || count <= 0)
		return;

	for (int i = 0; i < count; ++i)
	{
		int graftedCell = 0;
		int hostCell = 0;
		int hostPortal = 0;
		int graftPortal = 0;
		int donorCell = 0;
		std::string donorPob;

		if (!building.getObjVars().getItem(graftKey(i, "graftedCell"), graftedCell) ||
			!building.getObjVars().getItem(graftKey(i, "hostCell"), hostCell) ||
			!building.getObjVars().getItem(graftKey(i, "hostPortal"), hostPortal) ||
			!building.getObjVars().getItem(graftKey(i, "graftPortal"), graftPortal) ||
			!building.getObjVars().getItem(graftKey(i, "donorCell"), donorCell) ||
			!building.getObjVars().getItem(graftKey(i, "donorPob"), donorPob))
		{
			continue;
		}

		if (!portalProperty->ensureGraftedCellSlot(graftedCell, donorPob.c_str(), donorCell))
			continue;

		PortalProperty::DynamicRoomGraft graft;
		graft.graftedCellIndex = graftedCell;
		graft.hostCellIndex = hostCell;
		graft.hostPortalIndex = hostPortal;
		graft.graftedPortalIndex = graftPortal;
		graft.donorCellIndex = donorCell;
		graft.donorPobName = donorPob;
		portalProperty->recordDynamicRoomGraft(graft);

		// Cells should already exist from DB; link portals once both sides are loaded.
		if (portalProperty->getCell(hostCell) && portalProperty->getCell(graftedCell))
			IGNORE_RETURN(portalProperty->linkCellPortals(hostCell, hostPortal, graftedCell, graftPortal));
	}

	LOG("dynamic_bunker", ("restoreGraftsFromObjVars building=%s count=%d", building.getNetworkId().getValueString().c_str(), count));
}

// ======================================================================
