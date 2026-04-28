// ======================================================================
//
// TerrainWaterLevelDeveloperDelta.h
// Additive offset (meters) applied to procedural water height queries.
//
// ======================================================================

#ifndef INCLUDED_TerrainWaterLevelDeveloperDelta_H
#define INCLUDED_TerrainWaterLevelDeveloperDelta_H

class TerrainWaterLevelDeveloperDelta
{
public:

	static float getDelta ();
	static void  setDelta (float deltaMeters);

	/** Load/save one float line to cwd: developer_local_water_<scene>.cfg */
	static bool loadPersistedForScene (char const * sceneNameUtf8);
	static bool savePersistedForScene (char const * sceneNameUtf8, float deltaMeters);
};

#endif
