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
#include "serverGame/CreatureObject.h"
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
	char const *const OV_ASSIGNED_ROOM = "dynamicBunker.assignedRoom";

	std::string graftKey(int index, char const *field)
	{
		char buffer[128];
		snprintf(buffer, sizeof(buffer), "%s.graft.%d.%s", OV_ROOT, index, field);
		return buffer;
	}

	std::string assignedRoomKey(int hostCellIndex, int hostPortalIndex)
	{
		char buffer[128];
		snprintf(buffer, sizeof(buffer), "%s.%d.%d", OV_ASSIGNED_ROOM, hostCellIndex, hostPortalIndex);
		return buffer;
	}

	void persistGrafts(ServerObject &building, PortalProperty const &portalProperty)
	{
		int oldCount = 0;
		IGNORE_RETURN(building.getObjVars().getItem(OV_COUNT, oldCount));

		PortalProperty::DynamicRoomGraftList const &grafts = portalProperty.getDynamicRoomGrafts();
		int const newCount = static_cast<int>(grafts.size());
		building.setObjVarItem(OV_COUNT, newCount);

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

		for (int i = newCount; i < oldCount; ++i)
		{
			building.removeObjVarItem(graftKey(i, "graftedCell"));
			building.removeObjVarItem(graftKey(i, "hostCell"));
			building.removeObjVarItem(graftKey(i, "hostPortal"));
			building.removeObjVarItem(graftKey(i, "graftPortal"));
			building.removeObjVarItem(graftKey(i, "donorCell"));
			building.removeObjVarItem(graftKey(i, "donorPob"));
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

	void broadcastUngraft(ServerObject &building, PortalProperty::DynamicRoomGraft const &graft, NetworkId const &cellId)
	{
		DynamicBunkerUngraftMessage const message(
			building.getNetworkId(),
			cellId,
			graft.graftedCellIndex,
			graft.hostCellIndex,
			graft.hostPortalIndex);

		building.sendToClientsInUpdateRange(message, true, false);
	}

	void ejectCellContentsToHost(PortalProperty &portalProperty, int fromCellIndex, int toCellIndex)
	{
		CellProperty *const fromCell = portalProperty.getCell(fromCellIndex);
		CellProperty *const toCell = portalProperty.getCell(toCellIndex);
		if (!fromCell || !toCell)
			return;

		ServerObject *const toCellObject = safe_cast<ServerObject *>(&toCell->getOwner());
		if (!toCellObject)
			return;

		std::vector<ServerObject *> contents;
		for (ContainerIterator it = fromCell->begin(); it != fromCell->end(); ++it)
		{
			Object *const obj = (*it).getObject();
			ServerObject *const so = obj ? obj->asServerObject() : 0;
			if (so)
				contents.push_back(so);
		}

		Transform const destTransform = toCellObject->getTransform_o2p();
		for (size_t i = 0; i < contents.size(); ++i)
		{
			ServerObject *const so = contents[i];
			if (!so)
				continue;

			Container::ContainerErrorCode error = Container::CEC_Success;
			if (so->asCreatureObject())
			{
				if (!ContainerInterface::transferItemToCell(*toCellObject, *so, destTransform, 0, error))
				{
					WARNING(true, ("DynamicBunker - failed to eject creature %s from grafted cell %d (%d)",
						so->getNetworkId().getValueString().c_str(), fromCellIndex, static_cast<int>(error)));
				}
			}
			else
			{
				IGNORE_RETURN(so->permanentlyDestroy(DeleteReasons::Replaced));
			}
		}
	}

	bool removeGraftRecursive(ServerObject &building, PortalProperty &portalProperty, int graftedCellIndex)
	{
		// Cascade: remove grafts whose host is this grafted cell first.
		bool removedChild = true;
		while (removedChild)
		{
			removedChild = false;
			PortalProperty::DynamicRoomGraftList const &grafts = portalProperty.getDynamicRoomGrafts();
			for (size_t i = 0; i < grafts.size(); ++i)
			{
				if (grafts[i].hostCellIndex == graftedCellIndex)
				{
					if (!removeGraftRecursive(building, portalProperty, grafts[i].graftedCellIndex))
						return false;
					removedChild = true;
					break;
				}
			}
		}

		PortalProperty::DynamicRoomGraft graft;
		bool found = false;
		{
			PortalProperty::DynamicRoomGraftList const &grafts = portalProperty.getDynamicRoomGrafts();
			for (size_t i = 0; i < grafts.size(); ++i)
			{
				if (grafts[i].graftedCellIndex == graftedCellIndex)
				{
					graft = grafts[i];
					found = true;
					break;
				}
			}
		}
		if (!found)
		{
			WARNING(true, ("DynamicBunker::removeGraftRecursive - no graft record for cell %d", graftedCellIndex));
			return false;
		}

		IGNORE_RETURN(portalProperty.unlinkCellPortal(graft.hostCellIndex, graft.hostPortalIndex));
		portalProperty.unlinkAllCellPortals(graftedCellIndex);

		ejectCellContentsToHost(portalProperty, graftedCellIndex, graft.hostCellIndex);

		CellProperty *const graftedCell = portalProperty.getCell(graftedCellIndex);
		ServerObject *const cellObject = graftedCell ? safe_cast<ServerObject *>(&graftedCell->getOwner()) : 0;
		NetworkId const cellId = cellObject ? cellObject->getNetworkId() : NetworkId::cms_invalid;

		IGNORE_RETURN(portalProperty.clearLoadedCellSlot(graftedCellIndex));
		IGNORE_RETURN(portalProperty.removeDynamicRoomGraft(graftedCellIndex));
		IGNORE_RETURN(portalProperty.releaseGraftedCellSlot(graftedCellIndex));

		if (cellObject)
			IGNORE_RETURN(cellObject->permanentlyDestroy(DeleteReasons::Replaced));

		building.removeObjVarItem(assignedRoomKey(graft.hostCellIndex, graft.hostPortalIndex));
		persistGrafts(building, portalProperty);
		broadcastUngraft(building, graft, cellId);

		LOG("dynamic_bunker", ("removeGraft building=%s graftedCell=%d host=%d/%d cellId=%s",
			building.getNetworkId().getValueString().c_str(),
			graftedCellIndex,
			graft.hostCellIndex,
			graft.hostPortalIndex,
			cellId.getValueString().c_str()));

		return true;
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

	// Replace: if this socket already owns a dynamic graft, tear it down first.
	PortalProperty::DynamicRoomGraft existing;
	if (portalProperty->findDynamicRoomGraftForSocket(hostCellIndex, hostPortalIndex, existing))
	{
		if (!removeGraftRecursive(building, *portalProperty, existing.graftedCellIndex))
		{
			WARNING(true, ("DynamicBunker::addRoomHook - failed to unassign existing graft before replace"));
			return false;
		}
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

bool DynamicBunker::removeRoomHook(ServerObject &building, int hostCellIndex, int hostPortalIndex)
{
	PortalProperty *const portalProperty = building.getPortalProperty();
	if (!portalProperty)
		return false;

	PortalProperty::DynamicRoomGraft graft;
	if (!portalProperty->findDynamicRoomGraftForSocket(hostCellIndex, hostPortalIndex, graft))
	{
		WARNING(true, ("DynamicBunker::removeRoomHook - no dynamic graft on socket %d/%d", hostCellIndex, hostPortalIndex));
		return false;
	}

	return removeGraftRecursive(building, *portalProperty, graft.graftedCellIndex);
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

	// Prefer client-side catalog (local POBs). Still send a server catalog for
	// older clients / fallback when the client rebuild finds nothing.
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
		snprintf(label, sizeof(label), "cell %d / portal %d", socket.cellIndex, socket.portalIndex);
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

	LOG("dynamic_bunker", ("openFloorplan player=%s building=%s rooms=%d sockets=%d selected=%d/%d",
		client.getCharacterObjectId().getValueString().c_str(),
		building.getNetworkId().getValueString().c_str(),
		static_cast<int>(rooms.size()),
		static_cast<int>(socketEntries.size()),
		selectedCellIndex,
		selectedPortalIndex));
	return true;
}

// ----------------------------------------------------------------------

bool DynamicBunker::openFloorplanForClient(Client &client)
{
	ServerObject *const character = client.getCharacterObject();
	if (!character)
		return false;

	Object *const topmost = ContainerInterface::getTopmostContainer(*character);
	ServerObject *const building = topmost ? safe_cast<ServerObject *>(topmost) : 0;
	if (!building || !building->getPortalProperty())
	{
		WARNING(true, ("DynamicBunker::openFloorplanForClient - player %s is not inside a POB building",
			client.getCharacterObjectId().getValueString().c_str()));
		return false;
	}

	PortalProperty const *const portalProperty = building->getPortalProperty();
	NOT_NULL(portalProperty);

	int preferredCell = 1;
	CellObject const *const containingCell = ContainerInterface::getContainingCellObject(*character);
	if (containingCell)
		preferredCell = containingCell->getCell();

	PortalProperty::PortalSocketInfoList sockets;
	portalProperty->collectPortalSockets(sockets);

	int selectedCell = preferredCell;
	int selectedPortal = 0;
	bool found = false;

	// Prefer an open passable portal on the player's current cell.
	for (size_t i = 0; i < sockets.size(); ++i)
	{
		PortalProperty::PortalSocketInfo const &socket = sockets[i];
		if (socket.cellIndex == preferredCell && socket.open && socket.passable)
		{
			selectedCell = socket.cellIndex;
			selectedPortal = socket.portalIndex;
			found = true;
			break;
		}
	}

	// Else any open passable socket in the building.
	if (!found)
	{
		for (size_t i = 0; i < sockets.size(); ++i)
		{
			PortalProperty::PortalSocketInfo const &socket = sockets[i];
			if (socket.open && socket.passable)
			{
				selectedCell = socket.cellIndex;
				selectedPortal = socket.portalIndex;
				found = true;
				break;
			}
		}
	}

	// Else first socket, or the player's cell with portal 0.
	if (!found && !sockets.empty())
	{
		selectedCell = sockets[0].cellIndex;
		selectedPortal = sockets[0].portalIndex;
		found = true;
	}

	if (!found)
	{
		selectedCell = preferredCell > 0 ? preferredCell : 1;
		selectedPortal = 0;
	}

	// No dedicated terminal — use the building as the message context object.
	return openFloorplan(client, *building, *building, selectedCell, selectedPortal);
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

	building->setObjVarItem(assignedRoomKey(message.getHostCellIndex(), message.getHostPortalIndex()), message.getRoomId());

	ServerObject *const terminal = safe_cast<ServerObject *>(NetworkIdManager::getObjectById(message.getTerminalId()));
	IGNORE_RETURN(openFloorplan(client, *building, terminal ? *terminal : *building, message.getHostCellIndex(), message.getHostPortalIndex()));

	LOG("dynamic_bunker", ("handleAssignRoom ok room=%s cell=%s", message.getRoomId().c_str(), cellId.getValueString().c_str()));
}

// ----------------------------------------------------------------------

void DynamicBunker::handleUnassignRoom(Client &client, DynamicBunkerUnassignRoomMessage const &message)
{
	ServerObject *const building = safe_cast<ServerObject *>(NetworkIdManager::getObjectById(message.getBuildingId()));
	if (!building || !building->getPortalProperty())
	{
		WARNING(true, ("DynamicBunker::handleUnassignRoom - invalid building %s", message.getBuildingId().getValueString().c_str()));
		return;
	}

	if (!removeRoomHook(*building, message.getHostCellIndex(), message.getHostPortalIndex()))
	{
		WARNING(true, ("DynamicBunker::handleUnassignRoom - failed for socket %d/%d",
			message.getHostCellIndex(), message.getHostPortalIndex()));
		return;
	}

	ServerObject *const terminal = safe_cast<ServerObject *>(NetworkIdManager::getObjectById(message.getTerminalId()));
	IGNORE_RETURN(openFloorplan(client, *building, terminal ? *terminal : *building, message.getHostCellIndex(), message.getHostPortalIndex()));

	LOG("dynamic_bunker", ("handleUnassignRoom ok building=%s socket=%d/%d",
		building->getNetworkId().getValueString().c_str(),
		message.getHostCellIndex(),
		message.getHostPortalIndex()));
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
