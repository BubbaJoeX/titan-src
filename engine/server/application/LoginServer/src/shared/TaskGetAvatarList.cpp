// ======================================================================
//
// TaskGetAvatarList.cpp
// copyright (c) 2001 Sony Online Entertainment
//
// ======================================================================

#include "FirstLoginServer.h"
#include "TaskGetAvatarList.h"

#include "serverNetworkMessages/AvatarList.h"
#include "CentralServerConnection.h"
#include "ConfigLoginServer.h"
#include "DatabaseConnection.h"
#include "sharedLog/Log.h"
#include "serverNetworkMessages/TransferAccountData.h"
#include "serverNetworkMessages/TransferAccountDataArchive.h"
#include "serverNetworkMessages/TransferCharacterData.h"
#include "sharedDatabaseInterface/DbSession.h"
#include "sharedNetworkMessages/GenericValueTypeMessage.h"
#include "TaskUpgradeAccount.h"
#include <set>
#include <vector>

// ======================================================================

namespace
{
	std::set<uint32> s_reportedMissingNormalSlotPolicy;
}

// ======================================================================

TaskGetAvatarList::TaskGetAvatarList (StationId stationId, int clusterGroupId, const std::vector<uint32> & clusterIds, const TransferCharacterData * transferCharacterData) :
		TaskRequest(),
		m_stationId(stationId),
		m_stationIdNumberJediSlot(0),
		m_clusterGroupId(clusterGroupId),
		m_clusterIds(clusterIds),
		m_avatars(),
		m_availableCharacterSlots(),
		m_transferCharacterData(0),
		m_transferAccountData(0)
{
	if(transferCharacterData)
	{
		m_transferCharacterData = new TransferCharacterData(*transferCharacterData);
	}
}

// ----------------------------------------------------------------------

TaskGetAvatarList::TaskGetAvatarList (int clusterGroupId, const TransferAccountData * transferAccountData) :
		TaskRequest(),
		m_stationId(),
		m_stationIdNumberJediSlot(0),
		m_clusterGroupId(clusterGroupId),
		m_clusterIds(),
		m_avatars(),
		m_availableCharacterSlots(),
		m_transferCharacterData(0),
		m_transferAccountData(0)
{
	if(transferAccountData)
	{
		m_transferAccountData = new TransferAccountData(*transferAccountData);
		m_stationId = m_transferAccountData->getSourceStationId();
	}
}

// ----------------------------------------------------------------------

TaskGetAvatarList::~TaskGetAvatarList()
{
	delete m_transferCharacterData;
	m_transferCharacterData = 0;
	delete m_transferAccountData;
	m_transferAccountData = 0;
}

// ----------------------------------------------------------------------

bool TaskGetAvatarList::process(DB::Session *session)
{
	if (m_transferAccountData)
	{
		// if we are doing an account transfer, we need to also check the number of avatars in the destination station id 
		GetCharactersQuery qry;
		int rowsFetched;
		
		qry.station_id = m_transferAccountData->getDestinationStationId();
		qry.cluster_group_id = m_clusterGroupId;

		if (! (session->exec(&qry)))
			return false;

		bool hasAvatars = false;
		while ((rowsFetched = qry.fetch()) > 0)
		{
			hasAvatars = true;
		}

		m_transferAccountData->setDestinationHasAvatars(hasAvatars);
	}

	m_stationIdNumberJediSlot = 0;
	if (!m_transferAccountData)
	{
		TaskUpgradeAccount::QueryJediQuery qryHasJediSlot;
		qryHasJediSlot.station_id = m_stationId; //lint !e713 // loss of precision unsigned long to long
		qryHasJediSlot.character_type = 2;

		if (session->exec(&qryHasJediSlot))
		{
			m_stationIdNumberJediSlot = qryHasJediSlot.result.getValue();
		}

		qryHasJediSlot.done();
	}

	GetCharactersQuery qry;
	int rowsFetched;
	
	qry.station_id = m_stationId; //lint !e713 // loss of precision unsigned long to long
	qry.cluster_group_id = m_clusterGroupId;

	if (! (session->exec(&qry)))
		return false;

	AvatarRecord temp;
	
	while ((rowsFetched = qry.fetch()) > 0)
	{
		qry.character_name.getValue(temp.m_name);
		qry.object_template_id.getValue(temp.m_objectTemplateId);
		qry.object_id.getValue(temp.m_networkId);
		qry.cluster_id.getValue(temp.m_clusterId);
		qry.character_type.getValue(temp.m_characterType);

		m_avatars.push_back(temp);
	}
	qry.done();
	if (rowsFetched < 0)
		return false;

	if (!m_transferAccountData)
	{
		int const configuredMaximum = std::max(0, ConfigLoginServer::getMaxCharactersPerAccount());
		for (std::vector<uint32>::const_iterator clusterId = m_clusterIds.begin(); clusterId != m_clusterIds.end(); ++clusterId)
		{
			GetOpenCharacterSlotsQuery slotsQuery;
			slotsQuery.station_id = static_cast<long>(m_stationId);
			slotsQuery.cluster_id = static_cast<long>(*clusterId);

			if (!session->exec(&slotsQuery))
				return false;

			int slotsByType[4] = { 0, 0, 0, 0 };
			bool hasNormalSlotPolicy = false;
			int slotRows = 0;
			while ((rowsFetched = slotsQuery.fetch()) > 0)
			{
				++slotRows;
				int const type = static_cast<int>(slotsQuery.character_type_id.getValue());
				if (type >= 0 && type < 4)
				{
					slotsByType[type] = std::max(0, static_cast<int>(slotsQuery.num_open_slots.getValue()));
					if (type == 1)
						hasNormalSlotPolicy = true;
				}
			}
			slotsQuery.done();
			if (rowsFetched < 0)
				return false;

			// LOGIN.get_open_character_slots is the same authoritative policy used
			// by creation validation. Its values are already net of existing
			// characters and the account/cluster limits; do not subtract or clamp
			// them through a second policy source.
			int const normalRemaining = hasNormalSlotPolicy ? slotsByType[1] : 0;
			if (!hasNormalSlotPolicy && s_reportedMissingNormalSlotPolicy.insert(*clusterId).second)
				REPORT_LOG(true, ("AvailableCharacterSlotsV1 station %u cluster %u has no authoritative type-1 row; reporting zero (further warnings suppressed)\n",
					static_cast<unsigned int>(m_stationId), static_cast<unsigned int>(*clusterId)));

			// Existing validation explicitly requires both the unlocked (type 2)
			// and spectral backing (type 3) slots to create that character type.
			int const unlockedRemaining = (slotsByType[2] > 0 && slotsByType[3] > 0)
				? std::min(slotsByType[2], slotsByType[3])
				: 0;
			int const packageNetRemaining = normalRemaining + unlockedRemaining;
			int const availableCount = std::max(0, packageNetRemaining);
			int activeCharacterCount = 0;
			for (AvatarList::const_iterator avatar = m_avatars.begin(); avatar != m_avatars.end(); ++avatar)
			{
				if (avatar->m_clusterId == *clusterId)
					++activeCharacterCount;
			}
			REPORT_LOG(true, ("AvailableCharacterSlotsV1Calc station=%u cluster=%u group=%d async=complete rows=%d type1=%d type2=%d type3=%d active=%d packageNet=%d configuredMax=%d available=%d\n",
				static_cast<unsigned int>(m_stationId), static_cast<unsigned int>(*clusterId), m_clusterGroupId,
				slotRows, slotsByType[1], slotsByType[2], slotsByType[3], activeCharacterCount,
				packageNetRemaining, configuredMaximum, availableCount));
			m_availableCharacterSlots.push_back(std::make_pair(*clusterId, availableCount));
		}
	}

	return true;
}

// ----------------------------------------------------------------------

void TaskGetAvatarList::onComplete()
{
	if (m_transferAccountData)
	{
		if (m_transferAccountData->getDestinationHasAvatars())
		{
			// send a message back to the transfer server
			LOG("CustomerService", ("CharacterTransfer: Cannot complete account transfer from %lu to %lu: destination stationId contains avatars\n", m_transferAccountData->getSourceStationId(), m_transferAccountData->getDestinationStationId()));
			const GenericValueTypeMessage<TransferAccountData> response("TransferAccountFailedDestinationNotEmpty", *m_transferAccountData);
			CentralServerConnection::sendToCentralServer(m_transferAccountData->getStartGalaxy(), response);
		}
		else
			DatabaseConnection::getInstance().onAvatarListRetrievedAccountTransfer(m_avatars, m_transferAccountData);

	}
	else
	{
		DatabaseConnection::getInstance().onAvatarListRetrieved(m_stationId, m_stationIdNumberJediSlot, m_avatars, m_availableCharacterSlots, m_transferCharacterData);
	}
}

// ----------------------------------------------------------------------

AvatarList const & TaskGetAvatarList::getAvatars() const
{
	return m_avatars;
}

// ----------------------------------------------------------------------

StationId TaskGetAvatarList::getStationId() const
{
	return m_stationId;
}

// ----------------------------------------------------------------------

const std::vector<std::pair<uint32, int> > & TaskGetAvatarList::getAvailableCharacterSlots() const
{
	return m_availableCharacterSlots;
}

// ======================================================================

void TaskGetAvatarList::GetCharactersQuery::getSQL(std::string &sql)
{
	sql=std::string("begin :result := ")+DatabaseConnection::getInstance().getSchemaQualifier()+"login.get_avatar_list(:station_id, :cluster_group_id); end;";
 	// DEBUG_REPORT_LOG(true, ("TaskGetAvatarList SQL: %s\n", sql.c_str()));
}

// ----------------------------------------------------------------------

bool TaskGetAvatarList::GetCharactersQuery::bindParameters()
{
	if (!bindParameter(station_id)) return false;
	if (!bindParameter(cluster_group_id)) return false;

	return true;	
}

// ----------------------------------------------------------------------

bool TaskGetAvatarList::GetCharactersQuery::bindColumns()
{
	if (!bindCol(character_name)) return false;
	if (!bindCol(object_template_id)) return false;
	if (!bindCol(object_id)) return false;
	if (!bindCol(cluster_id)) return false;
	if (!bindCol(character_type)) return false;

	return true;
}

// ----------------------------------------------------------------------

DB::Query::QueryMode TaskGetAvatarList::GetCharactersQuery::getExecutionMode() const
{
	return MODE_PLSQL_REFCURSOR;
}

// ----------------------------------------------------------------------

TaskGetAvatarList::GetCharactersQuery::GetCharactersQuery() :
		Query(),
		station_id(),
		cluster_group_id(),
		character_name(),
		object_template_id(),
		object_id(),
		cluster_id(),
		character_type()
{
}

// ----------------------------------------------------------------------

void TaskGetAvatarList::GetOpenCharacterSlotsQuery::getSQL(std::string &sql)
{
	sql = std::string("begin :rc := ") + DatabaseConnection::getInstance().getSchemaQualifier() + "login.get_open_character_slots(:station_id,:cluster_id); end;";
}

// ----------------------------------------------------------------------

bool TaskGetAvatarList::GetOpenCharacterSlotsQuery::bindParameters()
{
	if (!bindParameter(station_id)) return false;
	if (!bindParameter(cluster_id)) return false;
	return true;
}

// ----------------------------------------------------------------------

bool TaskGetAvatarList::GetOpenCharacterSlotsQuery::bindColumns()
{
	if (!bindCol(character_type_id)) return false;
	if (!bindCol(num_open_slots)) return false;
	return true;
}

// ----------------------------------------------------------------------

DB::Query::QueryMode TaskGetAvatarList::GetOpenCharacterSlotsQuery::getExecutionMode() const
{
	return MODE_PLSQL_REFCURSOR;
}

// ----------------------------------------------------------------------

TaskGetAvatarList::GetOpenCharacterSlotsQuery::GetOpenCharacterSlotsQuery() :
	Query(),
	station_id(),
	cluster_id(),
	character_type_id(),
	num_open_slots()
{
}

// ======================================================================
