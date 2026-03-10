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
	std::string const TERRAIN_OBJVAR_COUNT = "city.terrain.region_count";

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

	std::vector<ServerObject *> objectsInRange;
	ServerWorld::findObjectsInRange(cityCenter, static_cast<float>(cityRadius + 50), objectsInRange);

	int adjustedCount = 0;
	for (std::vector<ServerObject *>::iterator it = objectsInRange.begin(); it != objectsInRange.end(); ++it)
	{
		ServerObject * const obj = *it;
		if (!obj)
			continue;

		// Only adjust static/buildout objects (non-persisted objects)
		if (obj->isPersisted())
			continue;

		adjustObjectToTerrainHeight(obj);
		adjustedCount++;
	}

	LOG("CityTerrain", ("adjustBuildoutObjectsInCity: checked %d objects in city %d",
		adjustedCount, cityId));
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

	// Look for all region objvars under the root
	// Format: "city.terrain.regions.<regionId>" = "type|shader|centerX|centerZ|radius|endX|endZ|width|height|blendDist"
	int loadedCount = 0;

	// We need to iterate through objvars with the terrain prefix
	// Get the nested list and iterate
	std::string const prefix = TERRAIN_OBJVAR_ROOT + ".";

	// Use a simple approach - check numbered regions up to count
	for (int i = 0; i < regionCount + 10; ++i)
	{
		std::ostringstream regionIdStream;
		regionIdStream << "region_" << i;
		std::string const regionId = regionIdStream.str();
		std::string const varName = prefix + regionId;

		std::string regionData;
		if (objVars.getItem(varName, regionData))
		{
			// Parse: "type|shader|centerX|centerZ|radius|endX|endZ|width|height|blendDist"
			std::istringstream iss(regionData);
			int32 type = 0;
			std::string shader;
			float centerX = 0, centerZ = 0, radius = 0, endX = 0, endZ = 0, width = 0, height = 0, blendDist = 0;

			iss >> type;
			iss.ignore(1);
			std::getline(iss, shader, '|');
			iss >> centerX;
			iss.ignore(1);
			iss >> centerZ;
			iss.ignore(1);
			iss >> radius;
			iss.ignore(1);
			iss >> endX;
			iss.ignore(1);
			iss >> endZ;
			iss.ignore(1);
			iss >> width;
			iss.ignore(1);
			iss >> height;
			iss.ignore(1);
			iss >> blendDist;

			CityTerrainRegion region;
			region.regionId = regionId;
			region.cityId = cityId;
			region.type = type;
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

			addRegionToMemory(region);
			loadedCount++;
		}
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

	addRegionToMemory(region);

	// Persist to city hall objvars
	saveRegionToCityHall(cityId, regionId, modType, shader, centerX, centerZ, radius, endX, endZ, width, height, blendDist);

	// Broadcast to all clients in city
	broadcastToCity(cityId, modType, regionId, shader, centerX, centerZ, radius, endX, endZ, width, height, blendDist);

	// If flatten, adjust objects in region
	if (modType == CityTerrainModificationType::MT_FLATTEN)
	{
		adjustObjectsInRegion(cityId, centerX, centerZ, radius, height);
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

	removeRegionFromMemory(regionId);
	removeRegionFromCityHall(cityId, regionId);
	broadcastToCity(cityId, CityTerrainModificationType::MT_REMOVE, regionId, "", 0, 0, 0, 0, 0, 0, 0, 0);
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
	std::vector<ServerObject *> objectsInRange;
	ServerWorld::findObjectsInRange(searchCenter, radius + 10.0f, objectsInRange);

	int adjustedCount = 0;

	for (std::vector<ServerObject *>::iterator it = objectsInRange.begin(); it != objectsInRange.end(); ++it)
	{
		ServerObject * const obj = *it;
		if (!obj)
			continue;

		if (obj->isPlayerControlled())
			continue;

		CreatureObject * const creature = obj->asCreatureObject();
		if (creature)
			continue;

		Vector const pos = obj->getPosition_w();
		float dx = pos.x - centerX;
		float dz = pos.z - centerZ;
		float dist = std::sqrt(dx * dx + dz * dz);

		if (dist > radius)
			continue;

		Vector newPos(pos.x, newHeight, pos.z);
		float heightDiff = std::fabs(pos.y - newHeight);
		if (heightDiff < 0.5f)
			continue;

		obj->setPosition_w(newPos);
		adjustedCount++;

		LOG("CityTerrain", ("adjustObjectsInRegion: adjusted object %s from height %.1f to %.1f",
			obj->getNetworkId().getValueString().c_str(), pos.y, newHeight));
	}

	LOG("CityTerrain", ("adjustObjectsInRegion: adjusted %d objects", adjustedCount));
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
											   float height, float blendDist)
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

	// Pack region data: "type|shader|centerX|centerZ|radius|endX|endZ|width|height|blendDist"
	std::ostringstream oss;
	oss << modType << "|"
		<< shader << "|"
		<< centerX << "|"
		<< centerZ << "|"
		<< radius << "|"
		<< endX << "|"
		<< endZ << "|"
		<< width << "|"
		<< height << "|"
		<< blendDist;

	std::string regionData = oss.str();
	std::string regionObjvar = TERRAIN_OBJVAR_ROOT + "." + regionId;
	cityHall->setObjVarItem(regionObjvar, regionData);

	int regionCount = 0;
	cityHall->getObjVars().getItem(TERRAIN_OBJVAR_COUNT, regionCount);
	cityHall->setObjVarItem(TERRAIN_OBJVAR_COUNT, regionCount + 1);

	LOG("CityTerrain", ("saveRegionToCityHall: saved region %s (count now %d)", regionId.c_str(), regionCount + 1));
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

	std::string regionObjvar = TERRAIN_OBJVAR_ROOT + "." + regionId;

	if (cityHall->getObjVars().hasItem(regionObjvar))
	{
		cityHall->removeObjVarItem(regionObjvar);

		int regionCount = 0;
		cityHall->getObjVars().getItem(TERRAIN_OBJVAR_COUNT, regionCount);
		if (regionCount > 0)
		{
			cityHall->setObjVarItem(TERRAIN_OBJVAR_COUNT, regionCount - 1);
		}

		LOG("CityTerrain", ("removeRegionFromCityHall: removed region %s", regionId.c_str()));
	}
}

// ----------------------------------------------------------------------

bool CityTerrainService::validateMayorPermission(Client const & client, int32 cityId)
{
	CreatureObject * const playerCreature = client.getCharacterObject();
	if (!playerCreature)
	{
		LOG("CityTerrain", ("validateMayorPermission: no player creature"));
		return false;
	}

	if (playerCreature->getClient() && playerCreature->getClient()->isGod())
	{
		return true;
	}

	if (!CityInterface::cityExists(cityId))
	{
		LOG("CityTerrain", ("validateMayorPermission: city %d does not exist", cityId));
		return false;
	}

	NetworkId const leaderId = CityInterface::getCityLeaderId(cityId);
	if (leaderId == playerCreature->getNetworkId())
	{
		return true;
	}

	LOG("CityTerrain", ("validateMayorPermission: player is not mayor"));
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
