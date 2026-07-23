// ======================================================================
//
// DynamicBunkerRoomCatalog.cpp
// copyright 2026 Titan
//
// ======================================================================

#include "serverGame/FirstServerGame.h"
#include "serverGame/DynamicBunkerRoomCatalog.h"

#include "serverGame/ServerObjectTemplate.h"
#include "sharedFile/TreeFile.h"
#include "sharedFoundation/CrcLowerString.h"
#include "sharedGame/SharedObjectTemplate.h"
#include "sharedLog/Log.h"
#include "sharedObject/ObjectTemplateList.h"
#include "sharedObject/PortalPropertyTemplate.h"
#include "sharedObject/PortalPropertyTemplateList.h"

#include <algorithm>
#include <cstdio>
#include <map>
#include <set>
#include <sstream>
#include <vector>

// ======================================================================

namespace DynamicBunkerRoomCatalogNamespace
{
	bool s_cacheValid = false;
	DynamicBunkerOpenFloorplanMessage::RoomList s_cachedRooms;
	std::map<std::string, DynamicBunkerOpenFloorplanMessage::RoomEntry> s_roomById;

	bool pathLooksUseful(std::string const &pobPath)
	{
		// Prefer interiors that are typically modular / bunker-like, but still allow
		// broad discovery of any multi-cell POB referenced by a building template.
		// Exclude obvious non-room noise.
		if (pobPath.find("appearance/") == std::string::npos)
			return false;
		if (pobPath.find(".pob") == std::string::npos)
			return false;
		if (pobPath.find("space/") != std::string::npos)
			return false;
		if (pobPath.find("ship_") != std::string::npos)
			return false;
		return true;
	}

	bool resolveDonorPobPath(std::string const &inPath, std::string &outPath)
	{
		if (inPath.empty())
			return false;

		std::vector<std::string> candidates;
		candidates.push_back(inPath);

		if (inPath.compare(0, 11, "appearance/") == 0)
			candidates.push_back(inPath.substr(11));

		if (inPath.find("appearance/") != 0)
			candidates.push_back("appearance/" + inPath);

		for (size_t i = 0; i < candidates.size(); ++i)
		{
			std::string const &candidate = candidates[i];
			if (candidate.empty())
				continue;

			if (TreeFile::exists(candidate.c_str()))
			{
				outPath = candidate;
				return true;
			}

			PortalPropertyTemplate const *const tmpl = PortalPropertyTemplateList::fetch(CrcLowerString(candidate.c_str()));
			if (tmpl)
			{
				tmpl->release();
				outPath = candidate;
				return true;
			}
		}

		return false;
	}

	bool templateNameLooksLikeBuilding(char const *templateName)
	{
		if (!templateName || !*templateName)
			return false;

		// Building / installation templates (server or shared) may own a POB.
		if (strstr(templateName, "object/building/") == 0 &&
			strstr(templateName, "object/installation/") == 0)
		{
			return false;
		}
		return true;
	}

	bool tryCollectPobFromShared(SharedObjectTemplate const *shared, std::set<std::string> &uniquePobs)
	{
		if (!shared)
			return false;

		std::string const &pob = shared->getPortalLayoutFilename();
		if (pob.empty() || !pathLooksUseful(pob))
			return false;

		std::string resolved;
		if (!resolveDonorPobPath(pob, resolved))
			return false;

		uniquePobs.insert(resolved);
		return true;
	}

	std::string makeRoomId(std::string const &donorPob, int cellIndex, int portalIndex)
	{
		std::ostringstream oss;
		oss << "dyn|" << donorPob << '|' << cellIndex << '|' << portalIndex;
		return oss.str();
	}

	std::string makeDisplayName(std::string const &donorPob, char const *cellName, int cellIndex, int portalIndex)
	{
		std::string leaf = donorPob;
		size_t slash = leaf.find_last_of("/\\");
		if (slash != std::string::npos)
			leaf = leaf.substr(slash + 1);
		size_t dot = leaf.rfind('.');
		if (dot != std::string::npos)
			leaf = leaf.substr(0, dot);

		for (size_t i = 0; i < leaf.size(); ++i)
		{
			if (leaf[i] == '_')
				leaf[i] = ' ';
		}

		char buf[320];
		if (cellName && *cellName)
			snprintf(buf, sizeof(buf), "%s / %s (p%d)", leaf.c_str(), cellName, portalIndex);
		else
			snprintf(buf, sizeof(buf), "%s / cell %d (p%d)", leaf.c_str(), cellIndex, portalIndex);
		return buf;
	}

	void expandPob(std::string const &pobPath, DynamicBunkerOpenFloorplanMessage::RoomList &rooms, std::map<std::string, DynamicBunkerOpenFloorplanMessage::RoomEntry> &byId)
	{
		std::string resolvedPob;
		if (!resolveDonorPobPath(pobPath, resolvedPob))
			return;

		PortalPropertyTemplate const *const tmpl = PortalPropertyTemplateList::fetch(CrcLowerString(resolvedPob.c_str()));
		if (!tmpl)
			return;

		int const cellCount = tmpl->getNumberOfCells();
		for (int cellIndex = 1; cellIndex < cellCount; ++cellIndex)
		{
			PortalPropertyTemplateCell const &cell = tmpl->getCell(cellIndex);
			PortalPropertyTemplateCell::PortalPropertyTemplateCellPortalList const *const portals = cell.getPortalList();
			if (!portals || portals->empty())
				continue;

			char const *const appearance = cell.getAppearanceName();
			char const *const cellName = cell.getName();

			// One catalog entry per cell; first portal is the graft socket.
			int const portalIndex = 0;
			DynamicBunkerOpenFloorplanMessage::RoomEntry room;
			room.roomId = makeRoomId(resolvedPob, cellIndex, portalIndex);
			room.displayName = makeDisplayName(resolvedPob, cellName, cellIndex, portalIndex);
			room.donorPob = resolvedPob;
			room.appearanceHint = appearance ? appearance : "";
			room.socketType = "auto";
			room.donorCellIndex = cellIndex;
			room.donorPortalIndex = portalIndex;

			if (byId.find(room.roomId) == byId.end())
			{
				byId[room.roomId] = room;
				rooms.push_back(room);
			}
		}

		tmpl->release();
	}

	void rebuildCache()
	{
		s_cachedRooms.clear();
		s_roomById.clear();

		std::vector<char const *> templateNames;
		ObjectTemplateList::getAllTemplateNamesFromCrcStringTable(templateNames);

		std::set<std::string> uniquePobs;
		int templatesScanned = 0;
		int templatesWithPob = 0;

		for (size_t i = 0; i < templateNames.size(); ++i)
		{
			char const *const name = templateNames[i];
			if (!templateNameLooksLikeBuilding(name))
				continue;

			++templatesScanned;
			ObjectTemplate const *const ot = ObjectTemplateList::fetch(name);
			if (!ot)
				continue;

			bool collected = false;

			// Prefer direct shared templates (portalLayoutFilename lives there).
			SharedObjectTemplate const *const shared = ot->asSharedObjectTemplate();
			if (shared)
				collected = tryCollectPobFromShared(shared, uniquePobs);

			// Server templates point at a shared counterpart.
			if (!collected)
			{
				ServerObjectTemplate const *const serverOt = ot->asServerObjectTemplate();
				if (serverOt)
				{
					std::string const &sharedName = serverOt->getSharedTemplate();
					if (!sharedName.empty())
					{
						ObjectTemplate const *const sharedOt = ObjectTemplateList::fetch(sharedName);
						if (sharedOt)
						{
							collected = tryCollectPobFromShared(sharedOt->asSharedObjectTemplate(), uniquePobs);
							sharedOt->releaseReference();
						}
					}
				}
			}

			if (collected)
				++templatesWithPob;

			ot->releaseReference();
		}

		s_cachedRooms.reserve(uniquePobs.size() * 4);
		for (std::set<std::string>::const_iterator it = uniquePobs.begin(); it != uniquePobs.end(); ++it)
			expandPob(*it, s_cachedRooms, s_roomById);

		struct RoomDisplayLess
		{
			bool operator()(DynamicBunkerOpenFloorplanMessage::RoomEntry const &a, DynamicBunkerOpenFloorplanMessage::RoomEntry const &b) const
			{
				return a.displayName < b.displayName;
			}
		};
		std::sort(s_cachedRooms.begin(), s_cachedRooms.end(), RoomDisplayLess());

		s_cacheValid = true;
		LOG("dynamic_bunker", ("RoomCatalog rebuilt: templatesScanned=%d withPob=%d uniquePobs=%d rooms=%d",
			templatesScanned, templatesWithPob, static_cast<int>(uniquePobs.size()), static_cast<int>(s_cachedRooms.size())));
	}
}

using namespace DynamicBunkerRoomCatalogNamespace;

// ======================================================================

bool DynamicBunkerRoomCatalog::parseRoomId(std::string const &roomId, std::string &outDonorPob, int &outDonorCellIndex, int &outDonorPortalIndex)
{
	if (roomId.compare(0, 4, "dyn|") != 0)
		return false;

	size_t const p1 = roomId.find('|', 4);
	if (p1 == std::string::npos)
		return false;
	size_t const p2 = roomId.find('|', p1 + 1);
	if (p2 == std::string::npos)
		return false;

	outDonorPob = roomId.substr(4, p1 - 4);
	outDonorCellIndex = atoi(roomId.c_str() + p1 + 1);
	outDonorPortalIndex = atoi(roomId.c_str() + p2 + 1);
	return !outDonorPob.empty() && outDonorCellIndex >= 1 && outDonorPortalIndex >= 0;
}

// ----------------------------------------------------------------------

void DynamicBunkerRoomCatalog::buildCatalog(DynamicBunkerOpenFloorplanMessage::RoomList &outRooms)
{
	if (!s_cacheValid)
		rebuildCache();
	outRooms = s_cachedRooms;
}

// ----------------------------------------------------------------------

bool DynamicBunkerRoomCatalog::lookupRoom(std::string const &roomId, DynamicBunkerOpenFloorplanMessage::RoomEntry &outRoom)
{
	if (!s_cacheValid)
		rebuildCache();

	std::map<std::string, DynamicBunkerOpenFloorplanMessage::RoomEntry>::const_iterator const it = s_roomById.find(roomId);
	if (it != s_roomById.end())
	{
		outRoom = it->second;
		return true;
	}

	// Allow direct dyn| IDs even if cache was filtered differently.
	std::string donorPob;
	int cellIndex = 0;
	int portalIndex = 0;
	if (!parseRoomId(roomId, donorPob, cellIndex, portalIndex))
		return false;

	outRoom.roomId = roomId;
	outRoom.displayName = roomId;
	outRoom.donorPob = donorPob;
	outRoom.appearanceHint.clear();
	outRoom.socketType = "auto";
	outRoom.donorCellIndex = cellIndex;
	outRoom.donorPortalIndex = portalIndex;

	std::string resolvedPob;
	if (resolveDonorPobPath(donorPob, resolvedPob))
	{
		outRoom.donorPob = resolvedPob;
		PortalPropertyTemplate const *const tmpl = PortalPropertyTemplateList::fetch(CrcLowerString(resolvedPob.c_str()));
		if (tmpl)
		{
			if (cellIndex < tmpl->getNumberOfCells())
			{
				PortalPropertyTemplateCell const &cell = tmpl->getCell(cellIndex);
				if (cell.getAppearanceName())
					outRoom.appearanceHint = cell.getAppearanceName();
				outRoom.displayName = makeDisplayName(resolvedPob, cell.getName(), cellIndex, portalIndex);
			}
			tmpl->release();
		}
	}
	return true;
}

// ----------------------------------------------------------------------

void DynamicBunkerRoomCatalog::invalidateCache()
{
	s_cacheValid = false;
	s_cachedRooms.clear();
	s_roomById.clear();
}

// ----------------------------------------------------------------------

bool DynamicBunkerRoomCatalog::resolveDonorPobPath(std::string const &inPath, std::string &outPath)
{
	return DynamicBunkerRoomCatalogNamespace::resolveDonorPobPath(inPath, outPath);
}

// ======================================================================
