// ======================================================================
//
// CityTerrainService.cpp
// copyright 2026 Titan
//
// Server-side handler for city terrain painting messages
// Manages terrain modifications including persistence and height queries
// ======================================================================

#include "serverGame/FirstServerGame.h"
#include "serverGame/CityTerrainService.h"

#include "serverGame/CityInfo.h"
#include "serverGame/CityInterface.h"
#include "serverGame/Client.h"
#include "serverGame/CreatureObject.h"
#include "serverGame/GameServer.h"
#include "serverGame/ServerObject.h"
#include "serverGame/ServerWorld.h"
#include "sharedFoundation/DynamicVariableList.h"
#include "sharedFoundation/ExitChain.h"
#include "sharedLog/Log.h"
#include "sharedNetworkMessages/CityTerrainMessages.h"
#include "Unicode.h"
#include "UnicodeUtils.h"

#include <ctime>
#include <cstdlib>
#include <cmath>
#include <sstream>
#include <map>

// ======================================================================

namespace CityTerrainServiceNamespace
{
	bool s_installed = false;
	int32 s_priorityCounter = 0;

	int const MAX_RADIUS_BY_RANK[] = {0, 10, 15, 20, 30, 40, 50};
	int const NUM_RANKS = sizeof(MAX_RADIUS_BY_RANK) / sizeof(MAX_RADIUS_BY_RANK[0]);

	std::string const TERRAIN_OBJVAR_ROOT = "city.terrain.regions";
	std::string const TERRAIN_OBJVAR_COUNT = "city.terrain.regions.region_count";

	// In-memory storage for terrain regions
	typedef std::map<std::string, CityTerrainRegion> RegionMap;
	typedef std::multimap<int32, std::string> CityRegionMap;

	RegionMap s_regions;
	CityRegionMap s_cityRegions;
}

using namespace CityTerrainServiceNamespace;

// ======================================================================

void CityTerrainService::install()
{
	DEBUG_FATAL(s_installed, ("CityTerrainService already installed"));
	s_installed = true;
	s_regions.clear();
	s_cityRegions.clear();
	s_priorityCounter = 0;
	ExitChain::add(CityTerrainService::remove, "CityTerrainService::remove");
	LOG("CityTerrain", ("CityTerrainService installed"));
}

// ----------------------------------------------------------------------

void CityTerrainService::remove()
{
	DEBUG_FATAL(!s_installed, ("CityTerrainService not installed"));
	s_regions.clear();
	s_cityRegions.clear();
	s_installed = false;
	LOG("CityTerrain", ("CityTerrainService removed"));
}

// ----------------------------------------------------------------------

void CityTerrainService::addRegionToMemory(CityTerrainRegion const & region)
{
	removeRegionFromMemory(region.regionId);

	CityTerrainRegion newRegion = region;
	newRegion.active = true;
	newRegion.timestamp = static_cast<int64>(time(0));
	newRegion.priority = ++s_priorityCounter;

	s_regions[region.regionId] = newRegion;
	s_cityRegions.insert(std::make_pair(region.cityId, region.regionId));

	LOG("CityTerrain", ("addRegionToMemory: id=%s cityId=%d type=%d center=(%.1f,%.1f) radius=%.1f height=%.1f priority=%d",
		region.regionId.c_str(), region.cityId, region.type, region.centerX, region.centerZ, region.radius, region.height, newRegion.priority));
}

// ----------------------------------------------------------------------

void CityTerrainService::removeRegionFromMemory(std::string const & regionId)
{
	RegionMap::iterator it = s_regions.find(regionId);
	if (it != s_regions.end())
	{
		int32 cityId = it->second.cityId;
		s_regions.erase(it);

		std::pair<CityRegionMap::iterator, CityRegionMap::iterator> range = s_cityRegions.equal_range(cityId);
		for (CityRegionMap::iterator cityIt = range.first; cityIt != range.second;)
		{
			if (cityIt->second == regionId)
			{
				CityRegionMap::iterator toErase = cityIt;
				++cityIt;
				s_cityRegions.erase(toErase);
			}
			else
			{
				++cityIt;
			}
		}

		LOG("CityTerrain", ("removeRegionFromMemory: removed region %s", regionId.c_str()));
	}
}

// ----------------------------------------------------------------------

void CityTerrainService::clearCityRegionsFromMemory(int32 cityId)
{
	std::pair<CityRegionMap::iterator, CityRegionMap::iterator> range = s_cityRegions.equal_range(cityId);

	std::vector<std::string> regionsToRemove;
	for (CityRegionMap::iterator it = range.first; it != range.second; ++it)
	{
		regionsToRemove.push_back(it->second);
	}

	for (size_t i = 0; i < regionsToRemove.size(); ++i)
	{
		s_regions.erase(regionsToRemove[i]);
	}

	s_cityRegions.erase(range.first, range.second);

	LOG("CityTerrain", ("clearCityRegionsFromMemory: cleared %d regions for city %d",
		static_cast<int>(regionsToRemove.size()), cityId));
}

// ----------------------------------------------------------------------

bool CityTerrainService::isPointInCircle(float px, float pz, float cx, float cz, float radius)
{
	float dx = px - cx;
	float dz = pz - cz;
	return (dx * dx + dz * dz) <= (radius * radius);
}

// ----------------------------------------------------------------------

bool CityTerrainService::isPointInLine(float px, float pz, float x1, float z1, float x2, float z2, float width)
{
	float dx = x2 - x1;
	float dz = z2 - z1;
	float lineLenSq = dx * dx + dz * dz;

	if (lineLenSq < 0.0001f)
		return isPointInCircle(px, pz, x1, z1, width * 0.5f);

	float t = ((px - x1) * dx + (pz - z1) * dz) / lineLenSq;
	t = (t < 0.0f) ? 0.0f : ((t > 1.0f) ? 1.0f : t);

	float closestX = x1 + t * dx;
	float closestZ = z1 + t * dz;

	float distX = px - closestX;
	float distZ = pz - closestZ;
	float distSq = distX * distX + distZ * distZ;

	float halfWidth = width * 0.5f;
	return distSq <= (halfWidth * halfWidth);
}

// ----------------------------------------------------------------------

float CityTerrainService::calculateBlendWeight(float distance, float blendDistance)
{
	if (blendDistance <= 0.0f)
		return 0.0f;

	float weight = 1.0f - (distance / blendDistance);
	return (weight < 0.0f) ? 0.0f : ((weight > 1.0f) ? 1.0f : weight);
}

// ----------------------------------------------------------------------

bool CityTerrainService::getModifiedTerrainHeight(float x, float z, float originalHeight, float & outHeight)
{
	outHeight = originalHeight;
	bool modified = false;

	struct RegionInfluence
	{
		CityTerrainRegion const * region;
		float weight;
		float distance;
		int32 priority;
	};

	std::vector<RegionInfluence> influences;

	for (RegionMap::const_iterator it = s_regions.begin(); it != s_regions.end(); ++it)
	{
		CityTerrainRegion const & region = it->second;
		if (!region.active || region.type != CityTerrainModificationType::MT_FLATTEN)
			continue;

		float totalRadius = region.radius + region.blendDistance;
		if (!isPointInCircle(x, z, region.centerX, region.centerZ, totalRadius))
			continue;

		float dist = static_cast<float>(sqrt(
			(x - region.centerX) * (x - region.centerX) +
			(z - region.centerZ) * (z - region.centerZ)));

		RegionInfluence inf;
		inf.region = &region;
		inf.distance = dist;
		inf.priority = region.priority;

		if (dist <= region.radius)
		{
			inf.weight = 1.0f;
		}
		else
		{
			inf.weight = 1.0f - calculateBlendWeight(dist - region.radius, region.blendDistance);
		}

		influences.push_back(inf);
	}

	if (influences.empty())
		return false;

	// Sort by priority (higher first)
	for (size_t i = 0; i < influences.size(); ++i)
	{
		for (size_t j = i + 1; j < influences.size(); ++j)
		{
			if (influences[j].priority > influences[i].priority)
			{
				RegionInfluence temp = influences[i];
				influences[i] = influences[j];
				influences[j] = temp;
			}
		}
	}

	float finalHeight = originalHeight;
	float appliedWeight = 0.0f;

	for (size_t i = 0; i < influences.size(); ++i)
	{
		RegionInfluence const & inf = influences[i];

		if (inf.weight >= 0.999f)
		{
			finalHeight = inf.region->height;
			appliedWeight = 1.0f;
			modified = true;
			break;
		}
		else if (appliedWeight < 1.0f)
		{
			float remainingWeight = 1.0f - appliedWeight;
			float contributionWeight = inf.weight * remainingWeight;

			if (contributionWeight > 0.001f)
			{
				finalHeight = finalHeight * (1.0f - contributionWeight) +
							  inf.region->height * contributionWeight;
				appliedWeight += contributionWeight;
				modified = true;
			}
		}
	}

	if (modified && appliedWeight < 1.0f)
	{
		float edgeBlend = appliedWeight;
		finalHeight = originalHeight * (1.0f - edgeBlend) + finalHeight * edgeBlend;
	}

	outHeight = finalHeight;
	return modified;
}

// ----------------------------------------------------------------------

bool CityTerrainService::isInCityTerrainArea(float x, float z, int32 & outCityId)
{
	for (RegionMap::const_iterator it = s_regions.begin(); it != s_regions.end(); ++it)
	{
		CityTerrainRegion const & region = it->second;
		if (!region.active)
			continue;

		bool inRegion = false;
		switch (region.type)
		{
		case CityTerrainModificationType::MT_SHADER_CIRCLE:
		case CityTerrainModificationType::MT_FLATTEN:
			inRegion = isPointInCircle(x, z, region.centerX, region.centerZ, region.radius + region.blendDistance);
			break;
		case CityTerrainModificationType::MT_SHADER_LINE:
			inRegion = isPointInLine(x, z, region.centerX, region.centerZ, region.endX, region.endZ,
									 region.width + region.blendDistance);
			break;
		}

		if (inRegion)
		{
			outCityId = region.cityId;
			return true;
		}
	}
	return false;
}

// ----------------------------------------------------------------------

void CityTerrainService::getRegionsForCity(int32 cityId, std::vector<CityTerrainRegion const *> & outRegions)
{
	outRegions.clear();

	std::pair<CityRegionMap::const_iterator, CityRegionMap::const_iterator> range = s_cityRegions.equal_range(cityId);
	for (CityRegionMap::const_iterator it = range.first; it != range.second; ++it)
	{
		RegionMap::const_iterator regionIt = s_regions.find(it->second);
		if (regionIt != s_regions.end())
		{
			outRegions.push_back(&regionIt->second);
		}
	}
}

// ----------------------------------------------------------------------

void CityTerrainService::adjustObjectToTerrainHeight(ServerObject * object)
{
	if (!object)
		return;

	// Skip players and creatures - they handle their own movement
	if (object->isPlayerControlled())
		return;

	CreatureObject * const creature = object->asCreatureObject();
	if (creature)
		return;

	// Skip objects in cells
	if (object->getAttachedTo())
		return;

	Vector const pos = object->getPosition_w();
	float newHeight;

	if (getModifiedTerrainHeight(pos.x, pos.z, pos.y, newHeight))
	{
		if (std::fabs(pos.y - newHeight) > 0.1f)
		{
			Vector newPos(pos.x, newHeight, pos.z);
			object->setPosition_w(newPos);

			LOG("CityTerrain", ("adjustObjectToTerrainHeight: adjusted %s from %.1f to %.1f",
				object->getNetworkId().getValueString().c_str(), pos.y, newHeight));
		}
	}
}

// ----------------------------------------------------------------------

void CityTerrainService::adjustBuildoutObjectsInCity(int32 cityId)
{
	if (!CityInterface::cityExists(cityId))
		return;

	CityInfo const & cityInfo = CityInterface::getCityInfo(cityId);
	float cityCenterX = static_cast<float>(cityInfo.getX());
	float cityCenterZ = static_cast<float>(cityInfo.getZ());
	int cityRadius = cityInfo.getRadius();

	Vector cityCenter(cityCenterX, 0.0f, cityCenterZ);

	// Search with extra margin to ensure we don't miss edge objects
	std::vector<ServerObject *> objectsInRange;
	ServerWorld::findObjectsInRange(cityCenter, static_cast<float>(cityRadius + 100), objectsInRange);

	int adjustedCount = 0;
	int structuresAdjusted = 0;
	int buildoutAdjusted = 0;

	for (std::vector<ServerObject *>::iterator it = objectsInRange.begin(); it != objectsInRange.end(); ++it)
	{
		ServerObject * const obj = *it;
		if (!obj)
			continue;

		// Skip player-controlled objects
		if (obj->isPlayerControlled())
			continue;

		// Skip creatures
		CreatureObject * const creature = obj->asCreatureObject();
		if (creature)
			continue;

		// Skip interior objects (attached to cells)
		if (obj->getAttachedTo())
			continue;

		Vector const pos = obj->getPosition_w();

		// Check if within city limits
		float dx = pos.x - cityCenterX;
		float dz = pos.z - cityCenterZ;
		float dist = std::sqrt(dx * dx + dz * dz);

		if (dist > static_cast<float>(cityRadius))
			continue;

		// Get the modified terrain height at this position
		float newHeight;
		if (!getModifiedTerrainHeight(pos.x, pos.z, pos.y, newHeight))
			continue;

		// Only adjust if there's a meaningful height difference
		if (std::fabs(pos.y - newHeight) < 0.1f)
			continue;

		Vector newPos(pos.x, newHeight, pos.z);
		obj->setPosition_w(newPos);
		adjustedCount++;

		// Track object types for logging
		if (!obj->isPersisted())
		{
			buildoutAdjusted++;
		}
		else if (obj->getGameObjectType() >= 512 && obj->getGameObjectType() < 768)
		{
			structuresAdjusted++;
		}
	}

	LOG("CityTerrain", ("adjustBuildoutObjectsInCity: adjusted %d objects (%d structures, %d buildout) in city %d",
		adjustedCount, structuresAdjusted, buildoutAdjusted, cityId));
}

// ----------------------------------------------------------------------

void CityTerrainService::adjustAllObjectsInModifiedArea(int32 cityId, float centerX, float centerZ, float radius)
{
	LOG("CityTerrain", ("adjustAllObjectsInModifiedArea: cityId=%d center=(%.1f,%.1f) radius=%.1f",
		cityId, centerX, centerZ, radius));

	Vector const searchCenter(centerX, 0.0f, centerZ);

	// Search in a larger area to catch objects that might be affected by blending
	float const searchRadius = radius + 100.0f;
	std::vector<ServerObject *> objectsInRange;
	ServerWorld::findObjectsInRange(searchCenter, searchRadius, objectsInRange);

	int adjustedCount = 0;
	int structuresAdjusted = 0;
	int playerStructures = 0;

	for (std::vector<ServerObject *>::iterator it = objectsInRange.begin(); it != objectsInRange.end(); ++it)
	{
		ServerObject * const obj = *it;
		if (!obj)
			continue;

		// Skip player-controlled objects - players handle their own position
		if (obj->isPlayerControlled())
			continue;

		// Skip creatures - NPCs and pets handle their own movement
		CreatureObject * const creature = obj->asCreatureObject();
		if (creature)
			continue;

		// Skip objects inside cells (interior objects like furniture)
		if (obj->getAttachedTo())
			continue;

		Vector const pos = obj->getPosition_w();
		float dx = pos.x - centerX;
		float dz = pos.z - centerZ;
		float dist = std::sqrt(dx * dx + dz * dz);

		// Skip objects far outside the affected area
		if (dist > radius + 50.0f)
			continue;

		// Get the modified terrain height at this object's position
		float newHeight;
		if (!getModifiedTerrainHeight(pos.x, pos.z, pos.y, newHeight))
			continue;

		// Only adjust if there's a meaningful height difference
		float heightDiff = std::fabs(pos.y - newHeight);
		if (heightDiff < 0.1f)
			continue;

		Vector newPos(pos.x, newHeight, pos.z);
		obj->setPosition_w(newPos);
		adjustedCount++;

		// Track object types for detailed logging
		int got = obj->getGameObjectType();
		if (got >= 512 && got < 768) // Structure game object types
		{
			structuresAdjusted++;
			if (obj->isPersisted())
			{
				playerStructures++;
				LOG("CityTerrain", ("adjustAllObjectsInModifiedArea: adjusted player structure %s from %.1f to %.1f (diff=%.1f)",
					obj->getNetworkId().getValueString().c_str(), pos.y, newHeight, heightDiff));
			}
		}
	}

	LOG("CityTerrain", ("adjustAllObjectsInModifiedArea: adjusted %d total objects (%d structures, %d player-placed)",
		adjustedCount, structuresAdjusted, playerStructures));
}

// ----------------------------------------------------------------------

void CityTerrainService::onCityHallLoaded(ServerObject * cityHall, int32 cityId)
{
	if (!cityHall || cityId <= 0)
		return;

	LOG("CityTerrain", ("onCityHallLoaded: loading terrain regions for city %d", cityId));

	loadRegionsFromCityHall(cityHall, cityId);

	// After loading regions, adjust any buildout objects in the area
	adjustBuildoutObjectsInCity(cityId);
}

// ----------------------------------------------------------------------

void CityTerrainService::loadRegionsFromCityHall(ServerObject * cityHall, int32 cityId)
{
	if (!cityHall)
		return;

	DynamicVariableList const & objVars = cityHall->getObjVars();

	int regionCount = 0;
	objVars.getItem(TERRAIN_OBJVAR_COUNT, regionCount);

	if (regionCount <= 0)
	{
		LOG("CityTerrain", ("loadRegionsFromCityHall: no regions found for city %d", cityId));
		return;
	}

	int loadedCount = 0;
	std::string const regionIdsVar = TERRAIN_OBJVAR_ROOT + ".region_ids";

	// Load regions from the region_ids array
	for (int i = 0; i < regionCount + 10 && loadedCount < regionCount; ++i)
	{
		std::ostringstream indexVar;
		indexVar << regionIdsVar << "." << i;

		std::string regionId;
		if (!objVars.getItem(indexVar.str(), regionId))
			continue;

		std::string const regionBase = TERRAIN_OBJVAR_ROOT + "." + regionId;

		// Load individual fields - use int for objvar compatibility (int32 is long on Linux)
		int typeIdInt = 0;
		std::string shader;
		float centerX = 0, centerZ = 0, radius = 0, endX = 0, endZ = 0, width = 0, height = 0, blendDist = 0;
		std::string creatorIdStr, creatorName;
		int timestamp = 0;

		objVars.getItem(regionBase + ".type_id", typeIdInt);
		objVars.getItem(regionBase + ".shader_name", shader);
		objVars.getItem(regionBase + ".center_x", centerX);
		objVars.getItem(regionBase + ".center_z", centerZ);
		objVars.getItem(regionBase + ".radius", radius);
		objVars.getItem(regionBase + ".end_x", endX);
		objVars.getItem(regionBase + ".end_z", endZ);
		objVars.getItem(regionBase + ".width", width);
		objVars.getItem(regionBase + ".height", height);
		objVars.getItem(regionBase + ".blend_dist", blendDist);
		objVars.getItem(regionBase + ".creator_id", creatorIdStr);
		objVars.getItem(regionBase + ".creator_name", creatorName);
		objVars.getItem(regionBase + ".timestamp", timestamp);

		CityTerrainRegion region;
		region.regionId = regionId;
		region.cityId = cityId;
		region.type = static_cast<int32>(typeIdInt);
		region.shaderTemplate = shader;
		region.centerX = centerX;
		region.centerZ = centerZ;
		region.radius = radius;
		region.endX = endX;
		region.endZ = endZ;
		region.width = width;
		region.height = height;
		region.blendDistance = blendDist;
		region.active = true;
		region.timestamp = timestamp;
		region.creatorId = NetworkId(creatorIdStr);
		region.creatorName = creatorName;

		addRegionToMemory(region);
		loadedCount++;

		LOG("CityTerrain", ("loadRegionsFromCityHall: loaded region %s type=%d by %s",
			regionId.c_str(), typeIdInt, creatorName.c_str()));
	}

	LOG("CityTerrain", ("loadRegionsFromCityHall: loaded %d regions for city %d", loadedCount, cityId));
}

// ----------------------------------------------------------------------

void CityTerrainService::handlePaintRequest(Client const & client, CityTerrainPaintRequestMessage const & msg)
{
	int32 const cityId = msg.getCityId();
	int32 const modType = msg.getModificationType();
	std::string const & shader = msg.getShaderTemplate();
	float const centerX = msg.getCenterX();
	float const centerZ = msg.getCenterZ();
	float const radius = msg.getRadius();
	float const endX = msg.getEndX();
	float const endZ = msg.getEndZ();
	float const width = msg.getWidth();
	float const height = msg.getHeight();
	float const blendDist = msg.getBlendDistance();

	LOG("CityTerrain", ("handlePaintRequest: cityId=%d modType=%d shader=%s center=(%.1f,%.1f) radius=%.1f height=%.1f",
					   cityId, modType, shader.c_str(), centerX, centerZ, radius, height));

	if (!validateMayorPermission(client, cityId))
	{
		sendResponse(client, false, "", "You do not have permission to modify this city's terrain.");
		return;
	}

	// Handle clear all request - resets terrain to defaults
	if (modType == CityTerrainModificationType::MT_CLEAR_ALL)
	{
		LOG("CityTerrain", ("handlePaintRequest: MT_CLEAR_ALL - removing all regions for city %d", cityId));

		// Remove all regions from memory for this city
		std::vector<std::string> regionsToRemove;
		for (RegionMap::const_iterator it = s_regions.begin(); it != s_regions.end(); ++it)
		{
			if (it->second.cityId == cityId)
			{
				regionsToRemove.push_back(it->first);
			}
		}

		for (std::vector<std::string>::const_iterator it = regionsToRemove.begin(); it != regionsToRemove.end(); ++it)
		{
			removeRegionFromMemory(*it);
		}

		// Clear objvars from city hall
		removeAllRegionsFromCityHall(cityId);

		// Broadcast clear to all clients in city
		broadcastToCity(cityId, CityTerrainModificationType::MT_CLEAR_ALL, "", "", 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);

		sendResponse(client, true, "", "");
		return;
	}

	if (!validateCityBounds(cityId, centerX, centerZ, radius))
	{
		sendResponse(client, false, "", "Terrain modification extends outside city boundaries.");
		return;
	}

	if (!CityInterface::cityExists(cityId))
	{
		sendResponse(client, false, "", "City does not exist.");
		return;
	}

	std::string regionId = generateRegionId();

	// Get creator info from client
	NetworkId creatorId;
	std::string creatorName = "Unknown";
	ServerObject * const characterObject = client.getCharacterObject();
	if (characterObject)
	{
		creatorId = characterObject->getNetworkId();
		creatorName = Unicode::wideToNarrow(characterObject->getObjectName());
		if (creatorName.empty())
		{
			creatorName = characterObject->getNetworkId().getValueString();
		}
	}

	// Create region and add to memory
	CityTerrainRegion region;
	region.regionId = regionId;
	region.cityId = cityId;
	region.type = modType;
	region.shaderTemplate = shader;
	region.centerX = centerX;
	region.centerZ = centerZ;
	region.radius = radius;
	region.endX = endX;
	region.endZ = endZ;
	region.width = width;
	region.height = height;
	region.blendDistance = blendDist;
	region.active = true;
	region.creatorId = creatorId;
	region.creatorName = creatorName;

	addRegionToMemory(region);

	// Persist to city hall objvars
	saveRegionToCityHall(cityId, regionId, modType, shader, centerX, centerZ, radius, endX, endZ, width, height, blendDist, creatorId, creatorName);

	// Broadcast to all clients in city
	broadcastToCity(cityId, modType, regionId, shader, centerX, centerZ, radius, endX, endZ, width, height, blendDist);

	// If flatten, adjust ALL objects (structures and buildout) in the affected region
	if (modType == CityTerrainModificationType::MT_FLATTEN)
	{
		// Use the comprehensive function that handles all object types
		adjustAllObjectsInModifiedArea(cityId, centerX, centerZ, radius);
	}

	sendResponse(client, true, regionId, "");
}

// ----------------------------------------------------------------------

void CityTerrainService::handleRemoveRequest(Client const & client, CityTerrainRemoveRequestMessage const & msg)
{
	int32 const cityId = msg.getCityId();
	std::string const & regionId = msg.getRegionId();

	LOG("CityTerrain", ("handleRemoveRequest: cityId=%d regionId=%s", cityId, regionId.c_str()));

	if (!validateMayorPermission(client, cityId))
	{
		return;
	}

	// Save region data before removing, so we can adjust objects if needed
	RegionMap::iterator it = s_regions.find(regionId);
	bool wasFlatten = false;
	float savedCenterX = 0.0f;
	float savedCenterZ = 0.0f;
	float savedRadius = 0.0f;

	if (it != s_regions.end())
	{
		CityTerrainRegion const & region = it->second;
		wasFlatten = (region.type == CityTerrainModificationType::MT_FLATTEN);
		savedCenterX = region.centerX;
		savedCenterZ = region.centerZ;
		savedRadius = region.radius;
	}

	// Remove the region
	removeRegionFromMemory(regionId);
	removeRegionFromCityHall(cityId, regionId);
	broadcastToCity(cityId, CityTerrainModificationType::MT_REMOVE, regionId, "", 0, 0, 0, 0, 0, 0, 0, 0);

	// If the removed region was a flatten, re-adjust objects in that area
	// They may now need to snap to the base terrain or remaining flatten regions
	if (wasFlatten)
	{
		LOG("CityTerrain", ("handleRemoveRequest: flatten region removed, adjusting objects in area"));
		adjustAllObjectsInModifiedArea(cityId, savedCenterX, savedCenterZ, savedRadius);
	}
}

// ----------------------------------------------------------------------

void CityTerrainService::handleSyncRequest(Client const & client, CityTerrainSyncRequestMessage const & msg)
{
	int32 const cityId = msg.getCityId();
	sendTerrainSyncToClient(client, cityId);
}

// ----------------------------------------------------------------------

void CityTerrainService::sendTerrainSyncToClient(Client const & client, int32 cityId)
{
	std::vector<CityTerrainRegion const *> regions;
	getRegionsForCity(cityId, regions);

	LOG("CityTerrain", ("sendTerrainSyncToClient: sending %d regions for city %d",
		static_cast<int>(regions.size()), cityId));

	for (size_t i = 0; i < regions.size(); ++i)
	{
		CityTerrainRegion const * region = regions[i];

		CityTerrainModifyMessage const msg(cityId, region->type, region->regionId, region->shaderTemplate,
			region->centerX, region->centerZ, region->radius, region->endX, region->endZ,
			region->width, region->height, region->blendDistance);
		client.send(msg, true);
	}
}

// ----------------------------------------------------------------------

void CityTerrainService::sendNearbyCitiesTerrainSync(Client const & client, float worldX, float worldZ, float range)
{
	std::map<int, CityInfo> const & allCities = CityInterface::getAllCityInfo();
	std::string const & currentPlanet = ServerWorld::getSceneId();

	int citiesSynced = 0;
	int totalRegionsSent = 0;

	for (std::map<int, CityInfo>::const_iterator it = allCities.begin(); it != allCities.end(); ++it)
	{
		CityInfo const & cityInfo = it->second;
		int const cityId = it->first;

		if (cityInfo.getPlanet() != currentPlanet)
			continue;

		float cityX = static_cast<float>(cityInfo.getX());
		float cityZ = static_cast<float>(cityInfo.getZ());
		float cityRadius = static_cast<float>(cityInfo.getRadius());

		float dx = worldX - cityX;
		float dz = worldZ - cityZ;
		float distToCity = std::sqrt(dx * dx + dz * dz);

		float effectiveRange = range + cityRadius;
		if (distToCity <= effectiveRange)
		{
			std::vector<CityTerrainRegion const *> regions;
			getRegionsForCity(cityId, regions);

			if (!regions.empty())
			{
				for (size_t i = 0; i < regions.size(); ++i)
				{
					CityTerrainRegion const * region = regions[i];

					CityTerrainModifyMessage const msg(cityId, region->type, region->regionId, region->shaderTemplate,
						region->centerX, region->centerZ, region->radius, region->endX, region->endZ,
						region->width, region->height, region->blendDistance);
					client.send(msg, true);
					++totalRegionsSent;
				}

				++citiesSynced;
			}
		}
	}

	if (citiesSynced > 0)
	{
		LOG("CityTerrain", ("sendNearbyCitiesTerrainSync: synced %d cities with %d total regions at position (%.1f, %.1f) with range %.1f",
			citiesSynced, totalRegionsSent, worldX, worldZ, range));
	}
}

// ----------------------------------------------------------------------

std::string CityTerrainService::generateRegionId()
{
	static int s_counter = 0;
	++s_counter;

	char buf[64];
	snprintf(buf, sizeof(buf), "region_%d_%d", static_cast<int>(time(0)), s_counter);
	return std::string(buf);
}

// ----------------------------------------------------------------------

void CityTerrainService::adjustObjectsInRegion(int32 cityId, float centerX, float centerZ, float radius, float newHeight)
{
	LOG("CityTerrain", ("adjustObjectsInRegion: cityId=%d center=(%.1f,%.1f) radius=%.1f newHeight=%.1f",
		cityId, centerX, centerZ, radius, newHeight));

	Vector const searchCenter(centerX, 0.0f, centerZ);

	// Search in a larger area to catch objects on the edges
	float const searchRadius = radius + 50.0f;
	std::vector<ServerObject *> objectsInRange;
	ServerWorld::findObjectsInRange(searchCenter, searchRadius, objectsInRange);

	int adjustedCount = 0;
	int structuresAdjusted = 0;

	for (std::vector<ServerObject *>::iterator it = objectsInRange.begin(); it != objectsInRange.end(); ++it)
	{
		ServerObject * const obj = *it;
		if (!obj)
			continue;

		// Skip player-controlled objects - they handle their own position
		if (obj->isPlayerControlled())
			continue;

		// Skip creatures - they move on their own
		CreatureObject * const creature = obj->asCreatureObject();
		if (creature)
			continue;

		// Skip objects inside cells (interior objects)
		if (obj->getAttachedTo())
			continue;

		Vector const pos = obj->getPosition_w();
		float dx = pos.x - centerX;
		float dz = pos.z - centerZ;
		float dist = std::sqrt(dx * dx + dz * dz);

		// Include objects within the region plus blend distance
		if (dist > radius + 20.0f)
			continue;

		// Calculate the proper terrain height at this object's position
		float calculatedHeight = newHeight;

		// For objects inside the core radius, use the flat height
		// For objects in the blend zone, calculate blended height
		if (dist > radius)
		{
			float blendDist = 10.0f; // Default blend distance
			float blendFactor = (dist - radius) / blendDist;
			if (blendFactor > 1.0f) blendFactor = 1.0f;

			// Blend between modified height and original terrain
			calculatedHeight = newHeight * (1.0f - blendFactor) + pos.y * blendFactor;
		}

		float heightDiff = std::fabs(pos.y - calculatedHeight);
		if (heightDiff < 0.1f)
			continue;

		Vector newPos(pos.x, calculatedHeight, pos.z);
		obj->setPosition_w(newPos);
		adjustedCount++;

		// Track if this is a building/structure for logging
		if (obj->getGameObjectType() >= 512 && obj->getGameObjectType() < 768)
		{
			structuresAdjusted++;
		}

		LOG("CityTerrain", ("adjustObjectsInRegion: adjusted object %s type=%d from height %.1f to %.1f",
			obj->getNetworkId().getValueString().c_str(), obj->getGameObjectType(), pos.y, calculatedHeight));
	}

	LOG("CityTerrain", ("adjustObjectsInRegion: adjusted %d objects (%d structures)", adjustedCount, structuresAdjusted));
}

// ----------------------------------------------------------------------

void CityTerrainService::broadcastToCity(int32 cityId, int32 modType, std::string const & regionId,
										  std::string const & shader, float centerX, float centerZ,
										  float radius, float endX, float endZ, float width,
										  float height, float blendDist)
{
	CityTerrainModifyMessage const msg(cityId, modType, regionId, shader, centerX, centerZ, radius, endX, endZ, width, height, blendDist);

	LOG("CityTerrain", ("broadcastToCity: cityId=%d modType=%d regionId=%s shader=%s",
		cityId, modType, regionId.c_str(), shader.c_str()));

	if (!CityInterface::cityExists(cityId))
	{
		LOG("CityTerrain", ("broadcastToCity: city %d does not exist", cityId));
		return;
	}

	CityInfo const & cityInfo = CityInterface::getCityInfo(cityId);
	float cityCenterX = static_cast<float>(cityInfo.getX());
	float cityCenterZ = static_cast<float>(cityInfo.getZ());
	int cityRadius = cityInfo.getRadius();

	Vector cityCenter(cityCenterX, 0.0f, cityCenterZ);

	std::vector<ServerObject *> objectsInRange;
	ServerWorld::findObjectsInRange(cityCenter, static_cast<float>(cityRadius + 100), objectsInRange);

	int sentCount = 0;
	for (std::vector<ServerObject *>::const_iterator it = objectsInRange.begin(); it != objectsInRange.end(); ++it)
	{
		if (*it == 0)
			continue;

		CreatureObject * const creature = (*it)->asCreatureObject();
		if (creature && creature->isPlayerControlled() && creature->getClient())
		{
			creature->getClient()->send(msg, true);
			sentCount++;
		}
	}

	LOG("CityTerrain", ("broadcastToCity: sent to %d clients", sentCount));
}

// ----------------------------------------------------------------------

void CityTerrainService::sendResponse(Client const & client, bool success, std::string const & regionId,
									   std::string const & errorMessage)
{
	CityTerrainPaintResponseMessage const msg(success, regionId, errorMessage);
	client.send(msg, true);
}

// ----------------------------------------------------------------------

void CityTerrainService::saveRegionToCityHall(int32 cityId, std::string const & regionId,
											   int32 modType, std::string const & shader,
											   float centerX, float centerZ, float radius,
											   float endX, float endZ, float width,
											   float height, float blendDist,
											   NetworkId const & creatorId, std::string const & creatorName)
{
	if (!CityInterface::cityExists(cityId))
	{
		LOG("CityTerrain", ("saveRegionToCityHall: city %d does not exist", cityId));
		return;
	}

	CityInfo const & cityInfo = CityInterface::getCityInfo(cityId);
	NetworkId const & cityHallId = cityInfo.getCityHallId();
	ServerObject * const cityHall = ServerWorld::findObjectByNetworkId(cityHallId);
	if (!cityHall)
	{
		LOG("CityTerrain", ("saveRegionToCityHall: city hall not found for city %d", cityId));
		return;
	}

	// Store each field as individual objvars for easier script access
	std::string const regionBase = TERRAIN_OBJVAR_ROOT + "." + regionId;

	// Convert type to readable string
	std::string typeStr;
	switch (modType)
	{
	case CityTerrainModificationType::MT_SHADER_CIRCLE:
		typeStr = "Shader (Circle)";
		break;
	case CityTerrainModificationType::MT_SHADER_LINE:
		typeStr = "Road/Path";
		break;
	case CityTerrainModificationType::MT_FLATTEN:
		typeStr = "Flatten";
		break;
	default:
		typeStr = "Unknown";
		break;
	}

	cityHall->setObjVarItem(regionBase + ".type", typeStr);
	cityHall->setObjVarItem(regionBase + ".type_id", static_cast<int>(modType));
	cityHall->setObjVarItem(regionBase + ".shader_name", shader);
	cityHall->setObjVarItem(regionBase + ".center_x", centerX);
	cityHall->setObjVarItem(regionBase + ".center_z", centerZ);
	cityHall->setObjVarItem(regionBase + ".radius", radius);
	cityHall->setObjVarItem(regionBase + ".end_x", endX);
	cityHall->setObjVarItem(regionBase + ".end_z", endZ);
	cityHall->setObjVarItem(regionBase + ".width", width);
	cityHall->setObjVarItem(regionBase + ".height", height);
	cityHall->setObjVarItem(regionBase + ".blend_dist", blendDist);
	cityHall->setObjVarItem(regionBase + ".creator_id", creatorId.getValueString());
	cityHall->setObjVarItem(regionBase + ".creator_name", creatorName);
	cityHall->setObjVarItem(regionBase + ".timestamp", static_cast<int>(time(0)));

	// Update the region_ids array
	std::vector<std::string> regionIds;
	DynamicVariableList const & objVars = cityHall->getObjVars();

	// Get existing region IDs
	std::string regionIdsVar = TERRAIN_OBJVAR_ROOT + ".region_ids";
	int arraySize = 0;

	// Check if array exists and get its elements
	for (int i = 0; i < 100; ++i)
	{
		std::ostringstream indexVar;
		indexVar << regionIdsVar << "." << i;
		std::string existingId;
		if (objVars.getItem(indexVar.str(), existingId))
		{
			if (existingId != regionId)  // Don't add duplicates
			{
				regionIds.push_back(existingId);
			}
			arraySize = i + 1;
		}
	}

	// Add the new region ID
	regionIds.push_back(regionId);

	// Write the updated array
	for (size_t i = 0; i < regionIds.size(); ++i)
	{
		std::ostringstream indexVar;
		indexVar << regionIdsVar << "." << i;
		cityHall->setObjVarItem(indexVar.str(), regionIds[i]);
	}

	int regionCount = static_cast<int>(regionIds.size());
	cityHall->setObjVarItem(TERRAIN_OBJVAR_COUNT, regionCount);

	LOG("CityTerrain", ("saveRegionToCityHall: saved region %s by %s (count now %d)",
		regionId.c_str(), creatorName.c_str(), regionCount));
}

// ----------------------------------------------------------------------

void CityTerrainService::removeRegionFromCityHall(int32 cityId, std::string const & regionId)
{
	if (!CityInterface::cityExists(cityId))
	{
		LOG("CityTerrain", ("removeRegionFromCityHall: city %d does not exist", cityId));
		return;
	}

	CityInfo const & cityInfo = CityInterface::getCityInfo(cityId);
	NetworkId const & cityHallId = cityInfo.getCityHallId();
	ServerObject * const cityHall = ServerWorld::findObjectByNetworkId(cityHallId);
	if (!cityHall)
	{
		LOG("CityTerrain", ("removeRegionFromCityHall: city hall not found for city %d", cityId));
		return;
	}

	// Remove all objvars for this region
	std::string const regionBase = TERRAIN_OBJVAR_ROOT + "." + regionId;
	cityHall->removeObjVarItem(regionBase + ".type");
	cityHall->removeObjVarItem(regionBase + ".type_id");
	cityHall->removeObjVarItem(regionBase + ".shader_name");
	cityHall->removeObjVarItem(regionBase + ".center_x");
	cityHall->removeObjVarItem(regionBase + ".center_z");
	cityHall->removeObjVarItem(regionBase + ".radius");
	cityHall->removeObjVarItem(regionBase + ".end_x");
	cityHall->removeObjVarItem(regionBase + ".end_z");
	cityHall->removeObjVarItem(regionBase + ".width");
	cityHall->removeObjVarItem(regionBase + ".height");
	cityHall->removeObjVarItem(regionBase + ".blend_dist");
	cityHall->removeObjVarItem(regionBase + ".creator_id");
	cityHall->removeObjVarItem(regionBase + ".creator_name");
	cityHall->removeObjVarItem(regionBase + ".timestamp");

	// Update the region_ids array - rebuild without the removed region
	DynamicVariableList const & objVars = cityHall->getObjVars();
	std::string regionIdsVar = TERRAIN_OBJVAR_ROOT + ".region_ids";

	std::vector<std::string> remainingIds;

	// Collect all region IDs except the one being removed
	for (int i = 0; i < 100; ++i)
	{
		std::ostringstream indexVar;
		indexVar << regionIdsVar << "." << i;
		std::string existingId;
		if (objVars.getItem(indexVar.str(), existingId))
		{
			if (existingId != regionId)
			{
				remainingIds.push_back(existingId);
			}
			// Remove old array entry
			cityHall->removeObjVarItem(indexVar.str());
		}
		else
		{
			break;
		}
	}

	// Rewrite the compacted array
	for (size_t i = 0; i < remainingIds.size(); ++i)
	{
		std::ostringstream indexVar;
		indexVar << regionIdsVar << "." << i;
		cityHall->setObjVarItem(indexVar.str(), remainingIds[i]);
	}

	cityHall->setObjVarItem(TERRAIN_OBJVAR_COUNT, static_cast<int>(remainingIds.size()));

	LOG("CityTerrain", ("removeRegionFromCityHall: removed region %s (count now %d)",
		regionId.c_str(), static_cast<int>(remainingIds.size())));
}

// ----------------------------------------------------------------------

void CityTerrainService::removeAllRegionsFromCityHall(int32 cityId)
{
	if (!CityInterface::cityExists(cityId))
	{
		LOG("CityTerrain", ("removeAllRegionsFromCityHall: city %d does not exist", cityId));
		return;
	}

	CityInfo const & cityInfo = CityInterface::getCityInfo(cityId);
	NetworkId const & cityHallId = cityInfo.getCityHallId();
	ServerObject * const cityHall = ServerWorld::findObjectByNetworkId(cityHallId);
	if (!cityHall)
	{
		LOG("CityTerrain", ("removeAllRegionsFromCityHall: city hall not found for city %d", cityId));
		return;
	}

	DynamicVariableList const & objVars = cityHall->getObjVars();
	std::string const regionIdsVar = TERRAIN_OBJVAR_ROOT + ".region_ids";

	// First collect all region IDs
	std::vector<std::string> regionIds;
	for (int i = 0; i < 100; ++i)
	{
		std::ostringstream indexVar;
		indexVar << regionIdsVar << "." << i;
		std::string existingId;
		if (objVars.getItem(indexVar.str(), existingId))
		{
			regionIds.push_back(existingId);
			cityHall->removeObjVarItem(indexVar.str());
		}
		else
		{
			break;
		}
	}

	// Remove objvars for each region
	for (std::vector<std::string>::const_iterator it = regionIds.begin(); it != regionIds.end(); ++it)
	{
		std::string const & regionId = *it;
		std::string const regionBase = TERRAIN_OBJVAR_ROOT + "." + regionId;
		cityHall->removeObjVarItem(regionBase + ".type");
		cityHall->removeObjVarItem(regionBase + ".type_id");
		cityHall->removeObjVarItem(regionBase + ".shader_name");
		cityHall->removeObjVarItem(regionBase + ".center_x");
		cityHall->removeObjVarItem(regionBase + ".center_z");
		cityHall->removeObjVarItem(regionBase + ".radius");
		cityHall->removeObjVarItem(regionBase + ".end_x");
		cityHall->removeObjVarItem(regionBase + ".end_z");
		cityHall->removeObjVarItem(regionBase + ".width");
		cityHall->removeObjVarItem(regionBase + ".height");
		cityHall->removeObjVarItem(regionBase + ".blend_dist");
		cityHall->removeObjVarItem(regionBase + ".creator_id");
		cityHall->removeObjVarItem(regionBase + ".creator_name");
		cityHall->removeObjVarItem(regionBase + ".timestamp");
	}

	// Reset the region count
	cityHall->setObjVarItem(TERRAIN_OBJVAR_COUNT, 0);

	LOG("CityTerrain", ("removeAllRegionsFromCityHall: removed %d regions from city %d",
		static_cast<int>(regionIds.size()), cityId));
}

// ----------------------------------------------------------------------

bool CityTerrainService::validateMayorPermission(Client const & client, int32 cityId)
{
	ServerObject * const characterObject = client.getCharacterObject();
	if (!characterObject)
	{
		LOG("CityTerrain", ("validateMayorPermission: no character object"));
		return false;
	}

	CreatureObject * const playerCreature = characterObject->asCreatureObject();
	if (!playerCreature)
	{
		LOG("CityTerrain", ("validateMayorPermission: character is not a creature"));
		return false;
	}

	// God mode always allowed
	if (playerCreature->getClient() && playerCreature->getClient()->isGod())
	{
		return true;
	}

	if (!CityInterface::cityExists(cityId))
	{
		LOG("CityTerrain", ("validateMayorPermission: city %d does not exist", cityId));
		return false;
	}

	CityInfo const & cityInfo = CityInterface::getCityInfo(cityId);
	NetworkId const playerId = playerCreature->getNetworkId();

	// Mayor is always allowed
	NetworkId const leaderId = cityInfo.getLeaderId();
	if (leaderId == playerId)
	{
		return true;
	}

	// Check if player is militia
	if (CityInterface::isCityMilitia(cityId, playerId))
	{
		LOG("CityTerrain", ("validateMayorPermission: militia member approved for city %d", cityId));
		return true;
	}

	LOG("CityTerrain", ("validateMayorPermission: player is not mayor or militia"));
	return false;
}

// ----------------------------------------------------------------------

bool CityTerrainService::validateCityBounds(int32 cityId, float x, float z, float radius)
{
	if (!CityInterface::cityExists(cityId))
	{
		LOG("CityTerrain", ("validateCityBounds: city %d does not exist", cityId));
		return false;
	}

	CityInfo const & cityInfo = CityInterface::getCityInfo(cityId);
	float cityCenterX = static_cast<float>(cityInfo.getX());
	float cityCenterZ = static_cast<float>(cityInfo.getZ());
	int cityRadius = cityInfo.getRadius();

	float dx = x - cityCenterX;
	float dz = z - cityCenterZ;
	float dist = std::sqrt(dx * dx + dz * dz);

	return (dist + radius) <= static_cast<float>(cityRadius);
}

// ======================================================================
