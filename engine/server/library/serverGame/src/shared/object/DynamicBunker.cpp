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
#include "sharedFile/TreeFile.h"
#include "sharedLog/Log.h"
#include "sharedNetworkMessages/DynamicBunkerMessages.h"
#include "sharedObject/CellProperty.h"
#include "sharedObject/NetworkIdManager.h"
#include "sharedObject/Portal.h"
#include "sharedObject/PortalProperty.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

// ======================================================================

namespace DynamicBunkerNamespace
{
	char const *const OV_ROOT = "dynamicBunker";
	char const *const OV_COUNT = "dynamicBunker.count";
	char const *const OV_ASSIGNED_ROOM = "dynamicBunker.assignedRoom";
	char const *const OV_CUSTOM_COUNT = "dynamicBunker.customSocket.count";
	char const *const OV_BRIDGE_COUNT = "dynamicBunker.bridge.count";

	std::string customSocketKey(int index, char const *field)
	{
		char buffer[128];
		snprintf(buffer, sizeof(buffer), "%s.customSocket.%d.%s", OV_ROOT, index, field);
		return buffer;
	}

	std::string bridgeKey(int index, char const *field)
	{
		char buffer[128];
		snprintf(buffer, sizeof(buffer), "%s.bridge.%d.%s", OV_ROOT, index, field);
		return buffer;
	}

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

	void persistCustomSockets(ServerObject &building, PortalProperty const &portalProperty)
	{
		int oldCount = 0;
		IGNORE_RETURN(building.getObjVars().getItem(OV_CUSTOM_COUNT, oldCount));

		PortalProperty::CustomSocketList const &sockets = portalProperty.getCustomSockets();
		int const newCount = static_cast<int>(sockets.size());
		building.setObjVarItem(OV_CUSTOM_COUNT, newCount);

		for (size_t i = 0; i < sockets.size(); ++i)
		{
			PortalProperty::CustomSocket const &socket = sockets[i];
			int const index = static_cast<int>(i);
			building.setObjVarItem(customSocketKey(index, "cell"), socket.cellIndex);
			building.setObjVarItem(customSocketKey(index, "socket"), socket.socketIndex);
			building.setObjVarItem(customSocketKey(index, "label"), socket.label);
			building.setObjVarItem(customSocketKey(index, "open"), socket.open ? 1 : 0);
			building.setObjVarItem(customSocketKey(index, "transform"), socket.doorTransform_o2p);
			building.setObjVarItem(customSocketKey(index, "width"), socket.doorwayWidth);
			building.setObjVarItem(customSocketKey(index, "height"), socket.doorwayHeight);
		}

		for (int i = newCount; i < oldCount; ++i)
		{
			building.removeObjVarItem(customSocketKey(i, "cell"));
			building.removeObjVarItem(customSocketKey(i, "socket"));
			building.removeObjVarItem(customSocketKey(i, "label"));
			building.removeObjVarItem(customSocketKey(i, "open"));
			building.removeObjVarItem(customSocketKey(i, "transform"));
			building.removeObjVarItem(customSocketKey(i, "width"));
			building.removeObjVarItem(customSocketKey(i, "height"));
		}
	}

	void persistBridgeSegments(ServerObject &building, PortalProperty const &portalProperty)
	{
		int oldCount = 0;
		IGNORE_RETURN(building.getObjVars().getItem(OV_BRIDGE_COUNT, oldCount));

		PortalProperty::BridgeSegmentList const &bridges = portalProperty.getBridgeSegments();
		int const newCount = static_cast<int>(bridges.size());
		building.setObjVarItem(OV_BRIDGE_COUNT, newCount);

		for (size_t i = 0; i < bridges.size(); ++i)
		{
			PortalProperty::BridgeSegment const &bridge = bridges[i];
			int const index = static_cast<int>(i);
			building.setObjVarItem(bridgeKey(index, "hostCell"), bridge.hostCellIndex);
			building.setObjVarItem(bridgeKey(index, "hostPortal"), bridge.hostPortalIndex);
			building.setObjVarItem(bridgeKey(index, "graftCell"), bridge.graftedCellIndex);
			building.setObjVarItem(bridgeKey(index, "graftPortal"), bridge.graftedPortalIndex);
			building.setObjVarItem(bridgeKey(index, "transform"), bridge.transform_o2p);
			building.setObjVarItem(bridgeKey(index, "length"), bridge.length);
			building.setObjVarItem(bridgeKey(index, "width"), bridge.width);
			building.setObjVarItem(bridgeKey(index, "height"), bridge.height);
		}

		for (int i = newCount; i < oldCount; ++i)
		{
			building.removeObjVarItem(bridgeKey(i, "hostCell"));
			building.removeObjVarItem(bridgeKey(i, "hostPortal"));
			building.removeObjVarItem(bridgeKey(i, "graftCell"));
			building.removeObjVarItem(bridgeKey(i, "graftPortal"));
			building.removeObjVarItem(bridgeKey(i, "transform"));
			building.removeObjVarItem(bridgeKey(i, "length"));
			building.removeObjVarItem(bridgeKey(i, "width"));
			building.removeObjVarItem(bridgeKey(i, "height"));
		}
	}

	bool getPortalBuildingTransform(PortalProperty const &portalProperty, int cellIndex, int portalIndex, Transform &outTransform_o2p)
	{
		return portalProperty.getPortalSocketTransform_o2p(cellIndex, portalIndex, outTransform_o2p);
	}

	void recordBridgeIfNeeded(
		PortalProperty &portalProperty,
		int hostCellIndex,
		int hostPortalIndex,
		int graftedCellIndex,
		int graftedPortalIndex)
	{
		Transform hostTransform;
		Transform graftTransform;
		if (!getPortalBuildingTransform(portalProperty, hostCellIndex, hostPortalIndex, hostTransform))
			return;
		if (!getPortalBuildingTransform(portalProperty, graftedCellIndex, graftedPortalIndex, graftTransform))
			return;

		float width = 2.5f;
		float height = 3.0f;
		if (PortalProperty::isCustomSocketIndex(hostPortalIndex))
		{
			PortalProperty::CustomSocket customSocket;
			if (portalProperty.findCustomSocket(hostCellIndex, hostPortalIndex, customSocket))
			{
				width = std::max(0.5f, customSocket.doorwayWidth);
				height = std::max(0.5f, customSocket.doorwayHeight);
			}
		}

		Vector const hostPos = hostTransform.getPosition_p();
		Vector const graftPos = graftTransform.getPosition_p();
		Vector const delta = graftPos - hostPos;
		float const gap = delta.magnitude();
		if (gap < 0.05f)
			return;

		Vector const mid = (hostPos + graftPos) * 0.5f;
		Transform bridgeTransform;
		bridgeTransform.setPosition_p(mid);

		PortalProperty::BridgeSegment segment;
		segment.hostCellIndex = hostCellIndex;
		segment.hostPortalIndex = hostPortalIndex;
		segment.graftedCellIndex = graftedCellIndex;
		segment.graftedPortalIndex = graftedPortalIndex;
		segment.transform_o2p = bridgeTransform;
		if (gap > 0.01f)
			segment.transform_o2p.yaw_l(atan2f(delta.x, delta.z));
		segment.length = std::max(0.5f, gap);
		segment.width = width;
		segment.height = height;
		portalProperty.recordBridgeSegment(segment);
	}

	void buildSocketEntries(
		PortalProperty const &portalProperty,
		DynamicBunkerOpenFloorplanMessage::SocketList &socketEntries)
	{
		PortalProperty::PortalSocketInfoList sockets;
		portalProperty.collectPortalSockets(sockets);
		socketEntries.reserve(sockets.size());

		for (size_t i = 0; i < sockets.size(); ++i)
		{
			PortalProperty::PortalSocketInfo const &socket = sockets[i];
			DynamicBunkerOpenFloorplanMessage::SocketEntry entry;
			entry.cellIndex = socket.cellIndex;
			entry.portalIndex = socket.portalIndex;
			entry.open = socket.open && socket.passable;
			entry.linkedCellIndex = -1;
			entry.linkedPortalIndex = -1;
			entry.custom = PortalProperty::isCustomSocketIndex(socket.portalIndex);

			PortalProperty::DynamicRoomGraft graft;
			if (portalProperty.findDynamicRoomGraftForSocket(socket.cellIndex, socket.portalIndex, graft))
			{
				entry.open = false;
				if (graft.hostCellIndex == socket.cellIndex && graft.hostPortalIndex == socket.portalIndex)
				{
					entry.linkedCellIndex = graft.graftedCellIndex;
					entry.linkedPortalIndex = graft.graftedPortalIndex;
				}
				else
				{
					entry.linkedCellIndex = graft.hostCellIndex;
					entry.linkedPortalIndex = graft.hostPortalIndex;
				}
			}
			else if (!entry.custom)
			{
				int linkedCell = -1;
				int linkedPortal = -1;
				if (portalProperty.getPortalNeighbor(socket.cellIndex, socket.portalIndex, linkedCell, linkedPortal))
				{
					entry.linkedCellIndex = linkedCell;
					entry.linkedPortalIndex = linkedPortal;
					entry.open = false;
				}
			}

			Transform portalTransform;
			if (getPortalBuildingTransform(portalProperty, socket.cellIndex, socket.portalIndex, portalTransform))
			{
				entry.mapX = portalTransform.getPosition_p().x;
				entry.mapZ = portalTransform.getPosition_p().z;
			}

			char label[160];
			if (entry.custom)
			{
				PortalProperty::CustomSocket customSocket;
				if (portalProperty.findCustomSocket(socket.cellIndex, socket.portalIndex, customSocket) && !customSocket.label.empty())
					snprintf(label, sizeof(label), "custom: %s", customSocket.label.c_str());
				else
					snprintf(label, sizeof(label), "custom snap %d", socket.portalIndex);
			}
			else
			{
				snprintf(label, sizeof(label), "cell %d / portal %d", socket.cellIndex, socket.portalIndex);
			}

			if (entry.linkedCellIndex >= 0)
			{
				char linkBuf[96];
				snprintf(linkBuf, sizeof(linkBuf), " -> cell %d / portal %d", entry.linkedCellIndex, entry.linkedPortalIndex);
				strncat(label, linkBuf, sizeof(label) - strlen(label) - 1);
			}
			entry.label = label;
			socketEntries.push_back(entry);
		}
	}

	void buildBridgeEntries(
		PortalProperty const &portalProperty,
		DynamicBunkerOpenFloorplanMessage::BridgeList &bridgeEntries)
	{
		PortalProperty::BridgeSegmentList const &bridges = portalProperty.getBridgeSegments();
		bridgeEntries.reserve(bridges.size());
		for (size_t i = 0; i < bridges.size(); ++i)
		{
			PortalProperty::BridgeSegment const &bridge = bridges[i];
			DynamicBunkerOpenFloorplanMessage::BridgeEntry entry;
			entry.hostCellIndex = bridge.hostCellIndex;
			entry.hostPortalIndex = bridge.hostPortalIndex;
			entry.graftedCellIndex = bridge.graftedCellIndex;
			entry.graftedPortalIndex = bridge.graftedPortalIndex;
			entry.transform_o2p = bridge.transform_o2p;
			entry.length = bridge.length;
			entry.width = bridge.width;
			entry.height = bridge.height;
			bridgeEntries.push_back(entry);
		}
	}

	void buildCustomSocketEntries(
		PortalProperty const &portalProperty,
		DynamicBunkerOpenFloorplanMessage::CustomSocketList &customSocketEntries)
	{
		PortalProperty::CustomSocketList const &sockets = portalProperty.getCustomSockets();
		customSocketEntries.reserve(sockets.size());
		for (size_t i = 0; i < sockets.size(); ++i)
		{
			PortalProperty::CustomSocket const &socket = sockets[i];
			DynamicBunkerOpenFloorplanMessage::CustomSocketEntry entry;
			entry.cellIndex = socket.cellIndex;
			entry.socketIndex = socket.socketIndex;
			entry.label = socket.label;
			entry.doorTransform_o2p = socket.doorTransform_o2p;
			entry.open = socket.open;
			entry.doorwayWidth = socket.doorwayWidth;
			entry.doorwayHeight = socket.doorwayHeight;
			customSocketEntries.push_back(entry);
		}
	}

	void updatePortalLayoutCrc(ServerObject &building, PortalProperty const &portalProperty)
	{
		uint32 const crc = portalProperty.computeEffectiveLayoutCrc();
		building.setObjVarItem("portalProperty.crc", static_cast<int>(crc));
	}

	void broadcastCustomSocket(ServerObject &building, PortalProperty::CustomSocket const &socket)
	{
		DynamicBunkerCustomSocketSyncMessage const message(
			building.getNetworkId(),
			socket.cellIndex,
			socket.socketIndex,
			socket.label,
			socket.doorTransform_o2p,
			socket.open,
			socket.doorwayWidth,
			socket.doorwayHeight);
		building.sendToClientsInUpdateRange(message, true, false);
	}

	void broadcastAllCustomSockets(ServerObject &building, PortalProperty const &portalProperty)
	{
		PortalProperty::CustomSocketList const &sockets = portalProperty.getCustomSockets();
		for (size_t i = 0; i < sockets.size(); ++i)
			broadcastCustomSocket(building, sockets[i]);
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

	void broadcastBuildingReverted(ServerObject &building)
	{
		DynamicBunkerBuildingRevertedMessage const message(building.getNetworkId());
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

		IGNORE_RETURN(portalProperty.unlinkHostPortal(graft.hostCellIndex, graft.hostPortalIndex));
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
		if (PortalProperty::isCustomSocketIndex(graft.hostPortalIndex))
			IGNORE_RETURN(portalProperty.markCustomSocketOpen(graft.hostCellIndex, graft.hostPortalIndex, true));
		persistGrafts(building, portalProperty);
		broadcastUngraft(building, graft, cellId);
		updatePortalLayoutCrc(building, portalProperty);

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

	if (PortalProperty::isCustomSocketIndex(hostPortalIndex))
		IGNORE_RETURN(portalProperty->materializeCustomSocketPortal(hostCellIndex, hostPortalIndex));

	Transform cellTransform;
	int resolvedDonorPortalIndex = donorPortalIndex;
	if (!portalProperty->computeGraftCellTransform(hostCellIndex, hostPortalIndex, donorPobName, donorCellIndex, donorPortalIndex, cellTransform, &resolvedDonorPortalIndex))
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

	portalProperty->cellLoaded(graftedCellIndex, *cellObject, true);
	cellObject->setTransform_o2p(cellTransform);
	cellObject->setLoadWith(building.getNetworkId());
	if (building.isPersisted())
		cellObject->persist();

	CellProperty *const graftCell = portalProperty->getCell(graftedCellIndex);
	int const resolvedGraftPortal = graftCell
		? PortalProperty::resolveCellPortalIndex(graftCell, resolvedDonorPortalIndex)
		: -1;
	if (resolvedGraftPortal < 0)
	{
		WARNING(true, ("DynamicBunker::addRoomHook - graft cell %d has no passable portal (resolved donor %d, requested %d)",
			graftedCellIndex, resolvedDonorPortalIndex, donorPortalIndex));
		cellObject->permanentlyDestroy(DeleteReasons::Replaced);
		IGNORE_RETURN(portalProperty->clearLoadedCellSlot(graftedCellIndex));
		IGNORE_RETURN(portalProperty->releaseGraftedCellSlot(graftedCellIndex));
		return false;
	}

	bool linked = false;
	if (PortalProperty::isCustomSocketIndex(hostPortalIndex))
	{
		linked = portalProperty->linkCustomSocketGraft(hostCellIndex, hostPortalIndex, graftedCellIndex, resolvedGraftPortal);
		if (linked)
			IGNORE_RETURN(portalProperty->markCustomSocketOpen(hostCellIndex, hostPortalIndex, false));
	}
	else
	{
		linked = portalProperty->linkCellPortals(hostCellIndex, hostPortalIndex, graftedCellIndex, resolvedGraftPortal);
	}

	if (!linked)
	{
		WARNING(true, ("DynamicBunker::addRoomHook - portal link failed host=%d/%d graft=%d/%d custom=%d",
			hostCellIndex, hostPortalIndex, graftedCellIndex, resolvedGraftPortal,
			PortalProperty::isCustomSocketIndex(hostPortalIndex) ? 1 : 0));
		cellObject->permanentlyDestroy(DeleteReasons::Replaced);
		IGNORE_RETURN(portalProperty->clearLoadedCellSlot(graftedCellIndex));
		IGNORE_RETURN(portalProperty->releaseGraftedCellSlot(graftedCellIndex));
		return false;
	}

	if (linked)
	{
		Transform correctedTransform;
		if (portalProperty->computeGraftCellTransform(hostCellIndex, hostPortalIndex, donorPobName, donorCellIndex, donorPortalIndex, correctedTransform, &resolvedDonorPortalIndex))
		{
			cellTransform = correctedTransform;
			cellObject->setTransform_o2p(cellTransform);
		}
	}

	recordBridgeIfNeeded(*portalProperty, hostCellIndex, hostPortalIndex, graftedCellIndex, resolvedGraftPortal);
	persistBridgeSegments(building, *portalProperty);

	PortalProperty::DynamicRoomGraft graft;
	graft.graftedCellIndex = graftedCellIndex;
	graft.hostCellIndex = hostCellIndex;
	graft.hostPortalIndex = hostPortalIndex;
	graft.graftedPortalIndex = resolvedGraftPortal;
	graft.donorCellIndex = donorCellIndex;
	graft.donorPobName = donorPobName;
	portalProperty->recordDynamicRoomGraft(graft);
	persistGrafts(building, *portalProperty);

	outCellId = cellObject->getNetworkId();
	broadcastGraft(building, graft, outCellId, cellTransform);
	updatePortalLayoutCrc(building, *portalProperty);

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

bool DynamicBunker::revertBuildingToRawPob(ServerObject &building)
{
	PortalProperty *const portalProperty = building.getPortalProperty();
	if (!portalProperty)
		return false;

	int safety = 256;
	while (!portalProperty->getDynamicRoomGrafts().empty() && safety-- > 0)
	{
		PortalProperty::DynamicRoomGraft const graft = portalProperty->getDynamicRoomGrafts().front();
		if (!removeGraftRecursive(building, *portalProperty, graft.graftedCellIndex))
		{
			IGNORE_RETURN(portalProperty->removeDynamicRoomGraft(graft.graftedCellIndex));
			IGNORE_RETURN(portalProperty->releaseGraftedCellSlot(graft.graftedCellIndex));
			IGNORE_RETURN(portalProperty->clearLoadedCellSlot(graft.graftedCellIndex));
		}
	}

	portalProperty->dematerializeAllCustomSocketPortals();
	portalProperty->clearCustomSockets();
	portalProperty->clearBridgeSegments();
	portalProperty->clearDynamicRoomGrafts();

	persistGrafts(building, *portalProperty);
	persistCustomSockets(building, *portalProperty);
	persistBridgeSegments(building, *portalProperty);

	updatePortalLayoutCrc(building, *portalProperty);
	broadcastBuildingReverted(building);

	LOG("dynamic_bunker", ("revertBuildingToRawPob building=%s", building.getNetworkId().getValueString().c_str()));
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

	// Prefer client-side catalog (local POBs). Still send a server catalog for
	// older clients / fallback when the client rebuild finds nothing.
	DynamicBunkerOpenFloorplanMessage::RoomList rooms;
	DynamicBunkerRoomCatalog::buildCatalog(rooms);

	DynamicBunkerOpenFloorplanMessage::SocketList socketEntries;
	buildSocketEntries(*portalProperty, socketEntries);

	DynamicBunkerOpenFloorplanMessage::BridgeList bridgeEntries;
	buildBridgeEntries(*portalProperty, bridgeEntries);

	DynamicBunkerOpenFloorplanMessage::CustomSocketList customSocketEntries;
	buildCustomSocketEntries(*portalProperty, customSocketEntries);

	DynamicBunkerOpenFloorplanMessage const message(
		building.getNetworkId(),
		terminal.getNetworkId(),
		selectedCellIndex,
		selectedPortalIndex,
		rooms,
		socketEntries,
		bridgeEntries,
		customSocketEntries);
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

	std::string resolvedDonorPob;
	if (!DynamicBunkerRoomCatalog::resolveDonorPobPath(room.donorPob, resolvedDonorPob))
	{
		WARNING(true, ("DynamicBunker::handleAssignRoom - donor POB '%s' not found for room '%s' (server missing appearance TRE?)",
			room.donorPob.c_str(), message.getRoomId().c_str()));
		return;
	}

	NetworkId cellId;
	if (!addRoomHook(*building, message.getHostCellIndex(), message.getHostPortalIndex(), resolvedDonorPob.c_str(), room.donorCellIndex, room.donorPortalIndex, cellId))
	{
		WARNING(true, ("DynamicBunker::handleAssignRoom - graft failed for '%s'", message.getRoomId().c_str()));
		return;
	}

	building->setObjVarItem(assignedRoomKey(message.getHostCellIndex(), message.getHostPortalIndex()), message.getRoomId());

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

	LOG("dynamic_bunker", ("handleUnassignRoom ok building=%s socket=%d/%d",
		building->getNetworkId().getValueString().c_str(),
		message.getHostCellIndex(),
		message.getHostPortalIndex()));
}

// ----------------------------------------------------------------------

void DynamicBunker::handleCreateCustomSocket(Client &client, DynamicBunkerCreateCustomSocketMessage const &message)
{
	ServerObject *building = safe_cast<ServerObject *>(NetworkIdManager::getObjectById(message.getBuildingId()));
	if (!building || !building->getPortalProperty())
	{
		ServerObject *const character = client.getCharacterObject();
		if (character)
		{
			Object *const topmost = ContainerInterface::getTopmostContainer(*character);
			ServerObject *const fallback = topmost ? safe_cast<ServerObject *>(topmost) : 0;
			if (fallback && fallback->getPortalProperty())
				building = fallback;
		}
	}

	if (!building || !building->getPortalProperty())
	{
		WARNING(true, ("DynamicBunker::handleCreateCustomSocket - invalid building %s (player not in POB?)",
			message.getBuildingId().getValueString().c_str()));
		return;
	}

	PortalProperty *const portalProperty = building->getPortalProperty();
	if (!portalProperty->getCell(message.getCellIndex()))
	{
		WARNING(true, ("DynamicBunker::handleCreateCustomSocket - cell %d not loaded", message.getCellIndex()));
		return;
	}

	PortalProperty::CustomSocket socket;
	socket.cellIndex = message.getCellIndex();
	socket.socketIndex = portalProperty->allocateCustomSocketIndex();
	socket.label = message.getLabel().empty() ? "custom" : message.getLabel();
	socket.doorTransform_o2p = message.getDoorTransform_o2p();
	socket.open = true;
	socket.materializedPortalIndex = -1;
	socket.doorwayWidth = message.getDoorwayWidth();
	socket.doorwayHeight = message.getDoorwayHeight();
	if (socket.doorwayWidth < 0.5f)
		socket.doorwayWidth = 1.0f;
	if (socket.doorwayHeight < 0.5f)
		socket.doorwayHeight = 2.0f;

	if (!portalProperty->addCustomSocket(socket))
	{
		WARNING(true, ("DynamicBunker::handleCreateCustomSocket - failed to add socket"));
		return;
	}

	IGNORE_RETURN(portalProperty->materializeCustomSocketPortal(socket.cellIndex, socket.socketIndex));

	persistCustomSockets(*building, *portalProperty);
	updatePortalLayoutCrc(*building, *portalProperty);
	broadcastCustomSocket(*building, socket);

	LOG("dynamic_bunker", ("handleCreateCustomSocket ok building=%s cell=%d socket=%d",
		building->getNetworkId().getValueString().c_str(),
		socket.cellIndex,
		socket.socketIndex));
}

// ----------------------------------------------------------------------

void DynamicBunker::handleRevertBuilding(Client &client, DynamicBunkerRevertBuildingMessage const &message)
{
	ServerObject *building = safe_cast<ServerObject *>(NetworkIdManager::getObjectById(message.getBuildingId()));
	if (!building || !building->getPortalProperty())
	{
		ServerObject *const character = client.getCharacterObject();
		if (character)
		{
			Object *const topmost = ContainerInterface::getTopmostContainer(*character);
			ServerObject *const fallback = topmost ? safe_cast<ServerObject *>(topmost) : 0;
			if (fallback && fallback->getPortalProperty())
				building = fallback;
		}
	}

	if (!building || !building->getPortalProperty())
	{
		WARNING(true, ("DynamicBunker::handleRevertBuilding - invalid building %s", message.getBuildingId().getValueString().c_str()));
		return;
	}

	if (!revertBuildingToRawPob(*building))
	{
		WARNING(true, ("DynamicBunker::handleRevertBuilding - failed for building %s", building->getNetworkId().getValueString().c_str()));
		return;
	}

	LOG("dynamic_bunker", ("handleRevertBuilding ok building=%s", building->getNetworkId().getValueString().c_str()));
}

// ----------------------------------------------------------------------

void DynamicBunker::restoreGraftsFromObjVars(ServerObject &building)
{
	PortalProperty *const portalProperty = building.getPortalProperty();
	if (!portalProperty)
		return;

	int customCount = 0;
	if (building.getObjVars().getItem(OV_CUSTOM_COUNT, customCount) && customCount > 0)
	{
		for (int i = 0; i < customCount; ++i)
		{
			PortalProperty::CustomSocket socket;
			int openFlag = 0;
			if (!building.getObjVars().getItem(customSocketKey(i, "cell"), socket.cellIndex) ||
				!building.getObjVars().getItem(customSocketKey(i, "socket"), socket.socketIndex) ||
				!building.getObjVars().getItem(customSocketKey(i, "label"), socket.label) ||
				!building.getObjVars().getItem(customSocketKey(i, "open"), openFlag) ||
				!building.getObjVars().getItem(customSocketKey(i, "transform"), socket.doorTransform_o2p))
			{
				continue;
			}
			socket.open = (openFlag != 0);
			socket.materializedPortalIndex = -1;
			if (!building.getObjVars().getItem(customSocketKey(i, "width"), socket.doorwayWidth))
				socket.doorwayWidth = 1.0f;
			if (!building.getObjVars().getItem(customSocketKey(i, "height"), socket.doorwayHeight))
				socket.doorwayHeight = 2.0f;
			IGNORE_RETURN(portalProperty->addCustomSocket(socket));
		}

		PortalProperty::CustomSocketList const &restoredSockets = portalProperty->getCustomSockets();
		for (size_t si = 0; si < restoredSockets.size(); ++si)
		{
			PortalProperty::CustomSocket const &restored = restoredSockets[si];
			if (restored.open && portalProperty->getCell(restored.cellIndex))
				IGNORE_RETURN(portalProperty->materializeCustomSocketPortal(restored.cellIndex, restored.socketIndex));
		}
	}

	int bridgeCount = 0;
	if (building.getObjVars().getItem(OV_BRIDGE_COUNT, bridgeCount) && bridgeCount > 0)
	{
		for (int i = 0; i < bridgeCount; ++i)
		{
			PortalProperty::BridgeSegment bridge;
			if (!building.getObjVars().getItem(bridgeKey(i, "hostCell"), bridge.hostCellIndex) ||
				!building.getObjVars().getItem(bridgeKey(i, "hostPortal"), bridge.hostPortalIndex) ||
				!building.getObjVars().getItem(bridgeKey(i, "graftCell"), bridge.graftedCellIndex) ||
				!building.getObjVars().getItem(bridgeKey(i, "graftPortal"), bridge.graftedPortalIndex) ||
				!building.getObjVars().getItem(bridgeKey(i, "transform"), bridge.transform_o2p) ||
				!building.getObjVars().getItem(bridgeKey(i, "length"), bridge.length) ||
				!building.getObjVars().getItem(bridgeKey(i, "width"), bridge.width) ||
				!building.getObjVars().getItem(bridgeKey(i, "height"), bridge.height))
			{
				continue;
			}
			portalProperty->recordBridgeSegment(bridge);
		}
	}

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

		if (!TreeFile::exists(donorPob.c_str()))
		{
			WARNING(true, ("DynamicBunker::restoreGraftsFromObjVars - skipping graft %d, donor POB '%s' not found",
				i, donorPob.c_str()));
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
		{
			if (PortalProperty::isCustomSocketIndex(hostPortal))
			{
				if (!portalProperty->linkCustomSocketGraft(hostCell, hostPortal, graftedCell, graftPortal))
				{
					WARNING(true, ("DynamicBunker::restoreGraftsFromObjVars - custom socket link failed for graft %d host=%d/%d graft=%d/%d",
						i, hostCell, hostPortal, graftedCell, graftPortal));
				}
			}
			else
			{
				IGNORE_RETURN(portalProperty->linkCellPortals(hostCell, hostPortal, graftedCell, graftPortal));
			}
		}
	}

	updatePortalLayoutCrc(building, *portalProperty);
	broadcastAllCustomSockets(building, *portalProperty);

	PortalProperty::DynamicRoomGraftList const &restoredGrafts = portalProperty->getDynamicRoomGrafts();
	for (size_t gi = 0; gi < restoredGrafts.size(); ++gi)
	{
		PortalProperty::DynamicRoomGraft const &graft = restoredGrafts[gi];
		CellProperty *const graftedCell = portalProperty->getCell(graft.graftedCellIndex);
		if (!graftedCell)
			continue;
		ServerObject *const cellObject = safe_cast<ServerObject *>(&graftedCell->getOwner());
		if (!cellObject)
			continue;
		broadcastGraft(building, graft, cellObject->getNetworkId(), cellObject->getTransform_o2p());
	}

	LOG("dynamic_bunker", ("restoreGraftsFromObjVars building=%s count=%d", building.getNetworkId().getValueString().c_str(), count));
}

// ======================================================================
