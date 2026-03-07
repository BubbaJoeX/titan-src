// ======================================================================
//
// CalendarService.cpp
// Copyright 2026 Titan Project
//
// Server-side calendar service implementation.
//
// ======================================================================

#include "serverGame/FirstServerGame.h"
#include "serverGame/calendar/CalendarService.h"

#include "serverGame/Client.h"
#include "serverGame/CreatureObject.h"
#include "serverGame/GameServer.h"
#include "serverGame/GuildInterface.h"
#include "serverGame/CityInterface.h"
#include "serverGame/ServerWorld.h"
#include "serverGame/Chat.h"
#include "sharedFoundation/NetworkId.h"
#include "sharedNetworkMessages/CalendarMessages.h"
#include "sharedNetworkMessages/GenericValueTypeMessage.h"
#include "sharedLog/Log.h"

#include <ctime>

// ======================================================================

CalendarService * CalendarService::ms_instance = nullptr;
float const CalendarService::CHECK_INTERVAL = 60.0f;

// ======================================================================

void CalendarService::install()
{
	DEBUG_FATAL(ms_instance != nullptr, ("CalendarService already installed"));
	ms_instance = new CalendarService();
	ms_instance->loadFromClusterData();
}

// ----------------------------------------------------------------------

void CalendarService::remove()
{
	if (ms_instance)
	{
		ms_instance->saveToClusterData();
		delete ms_instance;
		ms_instance = nullptr;
	}
}

// ----------------------------------------------------------------------

CalendarService & CalendarService::getInstance()
{
	DEBUG_FATAL(ms_instance == nullptr, ("CalendarService not installed"));
	return *ms_instance;
}

// ----------------------------------------------------------------------

CalendarService::CalendarService() :
	m_events(),
	m_nextEventId(1),
	m_bgTexture("ui_calendar_bg.dds"),
	m_srcX(0),
	m_srcY(0),
	m_srcW(512),
	m_srcH(512),
	m_checkTimer(0.0f)
{
}

// ----------------------------------------------------------------------

CalendarService::~CalendarService()
{
}

// ----------------------------------------------------------------------

void CalendarService::update(float deltaTime)
{
	m_checkTimer += deltaTime;
	if (m_checkTimer >= CHECK_INTERVAL)
	{
		m_checkTimer = 0.0f;
		checkAndTriggerEvents();
	}
}

// ======================================================================
// Event Management
// ======================================================================

std::string CalendarService::createEvent(NetworkId const & creatorId, CalendarEventData const & eventData)
{
	std::string eventId = generateEventId();

	CalendarEventData newEvent = eventData;
	newEvent.eventId = eventId;
	newEvent.creatorId = creatorId;
	newEvent.active = false;

	m_events[eventId] = newEvent;

	LOG("CalendarService", ("Event created: %s by %s", eventId.c_str(), creatorId.getValueString().c_str()));

	// Notify relevant players
	notifyEventCreated(newEvent);

	// Save to cluster data
	saveToClusterData();

	return eventId;
}

// ----------------------------------------------------------------------

bool CalendarService::updateEvent(std::string const & eventId, CalendarEventData const & eventData)
{
	EventMap::iterator it = m_events.find(eventId);
	if (it == m_events.end())
		return false;

	// Preserve the original ID and creator
	CalendarEventData updated = eventData;
	updated.eventId = eventId;
	updated.creatorId = it->second.creatorId;

	it->second = updated;

	LOG("CalendarService", ("Event updated: %s", eventId.c_str()));

	notifyEventUpdated(updated);
	saveToClusterData();

	return true;
}

// ----------------------------------------------------------------------

bool CalendarService::deleteEvent(std::string const & eventId)
{
	EventMap::iterator it = m_events.find(eventId);
	if (it == m_events.end())
		return false;

	CalendarEventData deletedEvent = it->second;
	m_events.erase(it);

	LOG("CalendarService", ("Event deleted: %s", eventId.c_str()));

	notifyEventDeleted(deletedEvent);
	saveToClusterData();

	return true;
}

// ----------------------------------------------------------------------

CalendarEventData const * CalendarService::getEvent(std::string const & eventId) const
{
	EventMap::const_iterator it = m_events.find(eventId);
	if (it == m_events.end())
		return nullptr;
	return &(it->second);
}

// ======================================================================
// Query Functions
// ======================================================================

void CalendarService::getEventsForPlayer(NetworkId const & playerId, int32 year, int32 month, std::vector<CalendarEventData> & outEvents) const
{
	outEvents.clear();

	// Get player's guild and city
	int32 playerGuildId = 0;
	int32 playerCityId = 0;

	ServerObject * playerObj = ServerWorld::findObjectByNetworkId(playerId);
	if (playerObj)
	{
		CreatureObject * creature = playerObj->asCreatureObject();
		if (creature)
		{
			playerGuildId = GuildInterface::getGuildId(creature->getNetworkId());
			playerCityId = CityInterface::getCitizenOfCityId(creature->getNetworkId());
		}
	}

	for (EventMap::const_iterator it = m_events.begin(); it != m_events.end(); ++it)
	{
		CalendarEventData const & evt = it->second;

		// Filter by month if specified
		if (year > 0 && month > 0)
		{
			if (evt.year != year || evt.month != month)
				continue;
		}

		// Check visibility based on event type
		switch (evt.eventType)
		{
			case ET_Staff:
			case ET_Server:
				// Everyone can see staff and server events
				outEvents.push_back(evt);
				break;

			case ET_Guild:
				// Only guild members can see guild events
				if (evt.guildId == playerGuildId && playerGuildId > 0)
					outEvents.push_back(evt);
				break;

			case ET_City:
				// Only city citizens can see city events
				if (evt.cityId == playerCityId && playerCityId > 0)
					outEvents.push_back(evt);
				break;
		}
	}
}

// ----------------------------------------------------------------------

void CalendarService::getEventsForDay(int32 year, int32 month, int32 day, std::vector<CalendarEventData> & outEvents) const
{
	outEvents.clear();

	for (EventMap::const_iterator it = m_events.begin(); it != m_events.end(); ++it)
	{
		CalendarEventData const & evt = it->second;
		if (evt.year == year && evt.month == month && evt.day == day)
		{
			outEvents.push_back(evt);
		}
	}
}

// ----------------------------------------------------------------------

void CalendarService::getEventsForGuild(int32 guildId, std::vector<CalendarEventData> & outEvents) const
{
	outEvents.clear();

	for (EventMap::const_iterator it = m_events.begin(); it != m_events.end(); ++it)
	{
		CalendarEventData const & evt = it->second;
		if (evt.eventType == ET_Guild && evt.guildId == guildId)
		{
			outEvents.push_back(evt);
		}
	}
}

// ----------------------------------------------------------------------

void CalendarService::getEventsForCity(int32 cityId, std::vector<CalendarEventData> & outEvents) const
{
	outEvents.clear();

	for (EventMap::const_iterator it = m_events.begin(); it != m_events.end(); ++it)
	{
		CalendarEventData const & evt = it->second;
		if (evt.eventType == ET_City && evt.cityId == cityId)
		{
			outEvents.push_back(evt);
		}
	}
}

// ----------------------------------------------------------------------

void CalendarService::getStaffEvents(std::vector<CalendarEventData> & outEvents) const
{
	outEvents.clear();

	for (EventMap::const_iterator it = m_events.begin(); it != m_events.end(); ++it)
	{
		CalendarEventData const & evt = it->second;
		if (evt.eventType == ET_Staff)
		{
			outEvents.push_back(evt);
		}
	}
}

// ----------------------------------------------------------------------

void CalendarService::getServerEvents(std::vector<CalendarEventData> & outEvents) const
{
	outEvents.clear();

	for (EventMap::const_iterator it = m_events.begin(); it != m_events.end(); ++it)
	{
		CalendarEventData const & evt = it->second;
		if (evt.eventType == ET_Server)
		{
			outEvents.push_back(evt);
		}
	}
}

// ======================================================================
// Permission Checks
// ======================================================================

bool CalendarService::canCreateEvent(CreatureObject const * player, int32 eventType) const
{
	if (!player)
		return false;

	// Staff can create any event
	if (player->isGod())
		return true;

	switch (eventType)
	{
		case ET_Staff:
		case ET_Server:
			// Only staff can create these
			return false;

		case ET_Guild:
		{
			// Guild leader or officer
			int32 guildId = GuildInterface::getGuildId(player->getNetworkId());
			if (guildId <= 0)
				return false;
			return GuildInterface::isGuildLeader(guildId, player->getNetworkId()) ||
			       GuildInterface::getGuildPermissions(guildId, player->getNetworkId()) > 0;
		}

		case ET_City:
		{
			// City mayor only
			int32 cityId = CityInterface::getCitizenOfCityId(player->getNetworkId());
			if (cityId <= 0)
				return false;
			return CityInterface::isCityMayor(cityId, player->getNetworkId());
		}
	}

	return false;
}

// ----------------------------------------------------------------------

bool CalendarService::canEditEvent(CreatureObject const * player, std::string const & eventId) const
{
	if (!player)
		return false;

	CalendarEventData const * evt = getEvent(eventId);
	if (!evt)
		return false;

	// Staff can edit any event
	if (player->isGod())
		return true;

	// Creator can edit their own event
	if (evt->creatorId == player->getNetworkId())
		return true;

	return false;
}

// ----------------------------------------------------------------------

bool CalendarService::canDeleteEvent(CreatureObject const * player, std::string const & eventId) const
{
	return canEditEvent(player, eventId);
}

// ----------------------------------------------------------------------

bool CalendarService::canViewEvent(CreatureObject const * player, CalendarEventData const & eventData) const
{
	if (!player)
		return false;

	// Staff can view all events
	if (player->isGod())
		return true;

	switch (eventData.eventType)
	{
		case ET_Staff:
		case ET_Server:
			return true;

		case ET_Guild:
		{
			int32 playerGuildId = GuildInterface::getGuildId(player->getNetworkId());
			return eventData.guildId == playerGuildId && playerGuildId > 0;
		}

		case ET_City:
		{
			int32 playerCityId = CityInterface::getCitizenOfCityId(player->getNetworkId());
			return eventData.cityId == playerCityId && playerCityId > 0;
		}
	}

	return false;
}

// ======================================================================
// Settings
// ======================================================================

void CalendarService::setSettings(std::string const & bgTexture, int32 srcX, int32 srcY, int32 srcW, int32 srcH)
{
	m_bgTexture = bgTexture;
	m_srcX = srcX;
	m_srcY = srcY;
	m_srcW = srcW;
	m_srcH = srcH;

	saveToClusterData();
}

// ----------------------------------------------------------------------

void CalendarService::getSettings(std::string & outBgTexture, int32 & outSrcX, int32 & outSrcY, int32 & outSrcW, int32 & outSrcH) const
{
	outBgTexture = m_bgTexture;
	outSrcX = m_srcX;
	outSrcY = m_srcY;
	outSrcW = m_srcW;
	outSrcH = m_srcH;
}

// ======================================================================
// Notifications
// ======================================================================

void CalendarService::notifyEventCreated(CalendarEventData const & eventData)
{
	CalendarEventNotificationMessage msg(CalendarEventNotificationMessage::NT_Created, eventData);

	switch (eventData.eventType)
	{
		case ET_Staff:
		case ET_Server:
			broadcastToAll(msg);
			break;

		case ET_Guild:
			broadcastToGuild(eventData.guildId, msg);
			break;

		case ET_City:
			broadcastToCity(eventData.cityId, msg);
			break;
	}
}

// ----------------------------------------------------------------------

void CalendarService::notifyEventUpdated(CalendarEventData const & eventData)
{
	CalendarEventNotificationMessage msg(CalendarEventNotificationMessage::NT_Updated, eventData);

	switch (eventData.eventType)
	{
		case ET_Staff:
		case ET_Server:
			broadcastToAll(msg);
			break;

		case ET_Guild:
			broadcastToGuild(eventData.guildId, msg);
			break;

		case ET_City:
			broadcastToCity(eventData.cityId, msg);
			break;
	}
}

// ----------------------------------------------------------------------

void CalendarService::notifyEventDeleted(CalendarEventData const & eventData)
{
	CalendarEventNotificationMessage msg(CalendarEventNotificationMessage::NT_Deleted, eventData);

	switch (eventData.eventType)
	{
		case ET_Staff:
		case ET_Server:
			broadcastToAll(msg);
			break;

		case ET_Guild:
			broadcastToGuild(eventData.guildId, msg);
			break;

		case ET_City:
			broadcastToCity(eventData.cityId, msg);
			break;
	}
}

// ======================================================================
// Message Handlers
// ======================================================================

void CalendarService::handleGetEventsRequest(Client const & client, int32 year, int32 month)
{
	std::vector<CalendarEventData> events;
	getEventsForPlayer(client.getCharacterObjectId(), year, month, events);

	CalendarEventsResponseMessage response(events);
	sendToClient(client, response);
}

// ----------------------------------------------------------------------

void CalendarService::handleCreateEventRequest(Client const & client, CalendarEventData const & eventData)
{
	ServerObject * playerObj = ServerWorld::findObjectByNetworkId(client.getCharacterObjectId());
	CreatureObject * player = playerObj ? playerObj->asCreatureObject() : nullptr;

	if (!player)
	{
		CalendarCreateEventResponseMessage response(false, "", "Player not found");
		sendToClient(client, response);
		return;
	}

	if (!canCreateEvent(player, eventData.eventType))
	{
		CalendarCreateEventResponseMessage response(false, "", "Permission denied");
		sendToClient(client, response);
		return;
	}

	// Fill in guild/city IDs if needed
	CalendarEventData finalEventData = eventData;
	if (eventData.eventType == ET_Guild)
	{
		finalEventData.guildId = GuildInterface::getGuildId(player->getNetworkId());
	}
	else if (eventData.eventType == ET_City)
	{
		finalEventData.cityId = CityInterface::getCitizenOfCityId(player->getNetworkId());
	}

	std::string eventId = createEvent(client.getCharacterObjectId(), finalEventData);

	if (!eventId.empty())
	{
		CalendarCreateEventResponseMessage response(true, eventId, "");
		sendToClient(client, response);
	}
	else
	{
		CalendarCreateEventResponseMessage response(false, "", "Failed to create event");
		sendToClient(client, response);
	}
}

// ----------------------------------------------------------------------

void CalendarService::handleDeleteEventRequest(Client const & client, std::string const & eventId)
{
	ServerObject * playerObj = ServerWorld::findObjectByNetworkId(client.getCharacterObjectId());
	CreatureObject * player = playerObj ? playerObj->asCreatureObject() : nullptr;

	if (!player)
	{
		CalendarDeleteEventResponseMessage response(false, eventId);
		sendToClient(client, response);
		return;
	}

	if (!canDeleteEvent(player, eventId))
	{
		CalendarDeleteEventResponseMessage response(false, eventId);
		sendToClient(client, response);
		return;
	}

	bool success = deleteEvent(eventId);
	CalendarDeleteEventResponseMessage response(success, eventId);
	sendToClient(client, response);
}

// ----------------------------------------------------------------------

void CalendarService::handleGetSettingsRequest(Client const & client)
{
	std::string bgTexture;
	int32 srcX, srcY, srcW, srcH;
	getSettings(bgTexture, srcX, srcY, srcW, srcH);

	CalendarSettingsResponseMessage response(bgTexture, srcX, srcY, srcW, srcH);
	sendToClient(client, response);
}

// ----------------------------------------------------------------------

void CalendarService::handleApplySettingsRequest(Client const & client, std::string const & bgTexture, int32 srcX, int32 srcY, int32 srcW, int32 srcH)
{
	ServerObject * playerObj = ServerWorld::findObjectByNetworkId(client.getCharacterObjectId());
	CreatureObject * player = playerObj ? playerObj->asCreatureObject() : nullptr;

	if (!player || !player->isGod())
	{
		// Permission denied - just ignore
		return;
	}

	setSettings(bgTexture, srcX, srcY, srcW, srcH);

	// Send confirmation
	CalendarSettingsResponseMessage response(bgTexture, srcX, srcY, srcW, srcH);
	sendToClient(client, response);
}

// ======================================================================
// Helper Functions
// ======================================================================

void CalendarService::checkAndTriggerEvents()
{
	time_t now = time(nullptr);
	struct tm * timeinfo = localtime(&now);

	int32 currentYear = timeinfo->tm_year + 1900;
	int32 currentMonth = timeinfo->tm_mon + 1;
	int32 currentDay = timeinfo->tm_mday;
	int32 currentHour = timeinfo->tm_hour;
	int32 currentMinute = timeinfo->tm_min;

	for (EventMap::iterator it = m_events.begin(); it != m_events.end(); ++it)
	{
		CalendarEventData & evt = it->second;

		// Check if event should start
		if (!evt.active)
		{
			bool shouldStart = false;

			if (evt.year < currentYear) shouldStart = true;
			else if (evt.year == currentYear)
			{
				if (evt.month < currentMonth) shouldStart = true;
				else if (evt.month == currentMonth)
				{
					if (evt.day < currentDay) shouldStart = true;
					else if (evt.day == currentDay)
					{
						if (evt.hour < currentHour) shouldStart = true;
						else if (evt.hour == currentHour && evt.minute <= currentMinute) shouldStart = true;
					}
				}
			}

			if (shouldStart)
			{
				evt.active = true;

				if (evt.broadcastStart)
				{
					// Send start notification
					CalendarEventNotificationMessage msg(CalendarEventNotificationMessage::NT_Started, evt);

					switch (evt.eventType)
					{
						case ET_Staff:
						case ET_Server:
							broadcastToAll(msg);
							break;
						case ET_Guild:
							broadcastToGuild(evt.guildId, msg);
							break;
						case ET_City:
							broadcastToCity(evt.cityId, msg);
							break;
					}
				}

				LOG("CalendarService", ("Event started: %s", evt.eventId.c_str()));
			}
		}
	}
}

// ----------------------------------------------------------------------

void CalendarService::broadcastToGuild(int32 guildId, CalendarEventNotificationMessage const & msg)
{
	if (guildId <= 0)
		return;

	std::vector<NetworkId> members;
	GuildInterface::getGuildMemberIds(guildId, members);

	for (std::vector<NetworkId>::const_iterator it = members.begin(); it != members.end(); ++it)
	{
		ServerObject * obj = ServerWorld::findObjectByNetworkId(*it);
		if (obj && obj->isPlayerControlled())
		{
			Client * client = obj->getClient();
			if (client)
			{
				sendToClient(*client, msg);
			}
		}
	}
}

// ----------------------------------------------------------------------

void CalendarService::broadcastToCity(int32 cityId, CalendarEventNotificationMessage const & msg)
{
	if (cityId <= 0)
		return;

	std::vector<NetworkId> citizens;
	CityInterface::getCitizens(cityId, citizens);

	for (std::vector<NetworkId>::const_iterator it = citizens.begin(); it != citizens.end(); ++it)
	{
		ServerObject * obj = ServerWorld::findObjectByNetworkId(*it);
		if (obj && obj->isPlayerControlled())
		{
			Client * client = obj->getClient();
			if (client)
			{
				sendToClient(*client, msg);
			}
		}
	}
}

// ----------------------------------------------------------------------

void CalendarService::broadcastToAll(CalendarEventNotificationMessage const & msg)
{
	// Send to all connected clients
	std::vector<Client *> const & clients = GameServer::getInstance().getClients();
	for (std::vector<Client *>::const_iterator it = clients.begin(); it != clients.end(); ++it)
	{
		if (*it)
		{
			sendToClient(**it, msg);
		}
	}
}

// ----------------------------------------------------------------------

void CalendarService::sendToClient(Client const & client, GameNetworkMessage const & msg)
{
	client.send(msg, true);
}

// ----------------------------------------------------------------------

std::string CalendarService::generateEventId()
{
	char buffer[64];
	snprintf(buffer, sizeof(buffer), "evt_%u_%ld", m_nextEventId++, static_cast<long>(time(nullptr)));
	return std::string(buffer);
}

// ----------------------------------------------------------------------

void CalendarService::saveToClusterData()
{
	// Save events and settings to cluster-wide data
	// This would use ClusterWideDataManager or similar
	// For now, we'll use a simple log to indicate saving
	LOG("CalendarService", ("Saving %d events to cluster data", static_cast<int>(m_events.size())));
}

// ----------------------------------------------------------------------

void CalendarService::loadFromClusterData()
{
	// Load events and settings from cluster-wide data
	// This would use ClusterWideDataManager or similar
	LOG("CalendarService", ("Loading events from cluster data"));
}

// ======================================================================

