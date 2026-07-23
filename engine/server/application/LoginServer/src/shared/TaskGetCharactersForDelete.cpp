// ======================================================================
//
// TaskGetCharactersForDelete.cpp
// copyright (c) 2001 Sony Online Entertainment
//
// ======================================================================

#include "FirstLoginServer.h"
#include "TaskGetCharactersForDelete.h"

#include "PurgeManager.h"
#include "serverNetworkMessages/AvatarList.h"

// ======================================================================

namespace
{
	std::vector<uint32> const s_noSlotCountClusters;
}

// ======================================================================

TaskGetCharactersForDelete::TaskGetCharactersForDelete (StationId stationId, int clusterGroupId) :
		TaskGetAvatarList(stationId, clusterGroupId, s_noSlotCountClusters, nullptr)
{
}

// ----------------------------------------------------------------------

TaskGetCharactersForDelete::~TaskGetCharactersForDelete()
{
}

// ----------------------------------------------------------------------

void TaskGetCharactersForDelete::onComplete()
{
	bool success=true;
	AvatarList const & theList = getAvatars();
	for (AvatarList::const_iterator i=theList.begin(); i!=theList.end(); ++i)
	{
		if (!LoginServer::getInstance().deleteCharacter(i->m_clusterId, i->m_networkId, getStationId()))
			success=false;
	}

	PurgeManager::onAllCharactersDeleted(getStationId(), success);
}

// ======================================================================
