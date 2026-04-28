// ======================================================================
//
// TerrainWaterLevelDeveloperDelta.cpp
//
// ======================================================================

#include "sharedTerrain/FirstSharedTerrain.h"
#include "sharedTerrain/TerrainWaterLevelDeveloperDelta.h"

#include "sharedTerrain/TerrainObject.h"

#include <cctype>
#include <cstdio>
#include <string>

namespace TerrainWaterLevelDeveloperDeltaNamespace
{
	float s_delta = 0.f;

	std::string sanitizeSceneName (char const * sceneNameUtf8)
	{
		std::string s = sceneNameUtf8 ? sceneNameUtf8 : "default";
		for (size_t i = 0; i < s.size (); ++i)
		{
			char & c = s[i];
			if (!(std::isalnum (static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == '.'))
				c = '_';
		}
		return s;
	}

	std::string persistPathForScene (char const * sceneNameUtf8)
	{
		return std::string ("developer_local_water_") + sanitizeSceneName (sceneNameUtf8) + ".cfg";
	}
}

using namespace TerrainWaterLevelDeveloperDeltaNamespace;

float TerrainWaterLevelDeveloperDelta::getDelta ()
{
	return s_delta;
}

void TerrainWaterLevelDeveloperDelta::setDelta (float const deltaMeters)
{
	s_delta = deltaMeters;

	TerrainObject const * const terrain = TerrainObject::getConstInstance ();
	if (terrain)
		terrain->flushWaterHeightCache ();
}

bool TerrainWaterLevelDeveloperDelta::loadPersistedForScene (char const * sceneNameUtf8)
{
	std::string const path = persistPathForScene (sceneNameUtf8);
	FILE * const f = fopen (path.c_str (), "r");
	if (!f)
		return false;

	float d = 0.f;
	int const n = fscanf (f, "%f", &d);
	fclose (f);
	if (n != 1)
		return false;

	setDelta (d);
	return true;
}

bool TerrainWaterLevelDeveloperDelta::savePersistedForScene (char const * sceneNameUtf8, float const deltaMeters)
{
	std::string const path = persistPathForScene (sceneNameUtf8);
	FILE * const f = fopen (path.c_str (), "w");
	if (!f)
		return false;

	fprintf (f, "%.6f\n", deltaMeters);
	fclose (f);
	return true;
}
