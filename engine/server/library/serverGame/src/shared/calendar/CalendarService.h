// ======================================================================
//
// CalendarService.h
// Copyright 2026 Titan Project
//
// Server-side calendar service for managing calendar events.
// Events are stored in memory with periodic persistence to the database.
//
// ======================================================================

#ifndef INCLUDED_CalendarService_H
#define INCLUDED_CalendarService_H

// ======================================================================

#include "sharedFoundation/NetworkId.h"
#include "sharedNetworkMessages/CalendarMessages.h"
#include "sharedMessageDispatch/Receiver.h"

#include <map>
#include <vector>
#include <string>

// ======================================================================

class Client;
class ServerObject;
class CreatureObject;
class LoadCalendarEventsMessage;
class LoadCalendarSettingsMessage;

// ======================================================================

class CalendarService : public MessageDispatch::Receiver
{
public:
	// Event types
	enum EventType
	{
		ET_Staff  = 0,
		ET_Guild  = 1,
		ET_City   = 2,
		ET_Server = 3
	};

	// Singleton access
	static CalendarService & getInstance();

	// Lifecycle
	static void install();
	static void remove();
	void        update(float deltaTime);

	// Event management
	std::string createEvent(NetworkId const & creatorId, CalendarEventData const & eventData);
	bool        updateEvent(std::string const & eventId, CalendarEventData const & eventData);
	bool        deleteEvent(std::string const & eventId);

	CalendarEventData const * getEvent(std::string const & eventId) const;

	// Query functions
	void getEventsForPlayer(NetworkId const & playerId, int32 year, int32 month, std::vector<CalendarEventData> & outEvents) const;
	void getEventsForDay(int32 year, int32 month, int32 day, std::vector<CalendarEventData> & outEvents) const;
	void getEventsForGuild(int32 guildId, std::vector<CalendarEventData> & outEvents) const;
	void getEventsForCity(int32 cityId, std::vector<CalendarEventData> & outEvents) const;
	void getStaffEvents(std::vector<CalendarEventData> & outEvents) const;
	void getServerEvents(std::vector<CalendarEventData> & outEvents) const;

	// Permission checks
	bool canCreateEvent(CreatureObject const * player, int32 eventType) const;
	bool canEditEvent(CreatureObject const * player, std::string const & eventId) const;
	bool canDeleteEvent(CreatureObject const * player, std::string const & eventId) const;
	bool canViewEvent(CreatureObject const * player, CalendarEventData const & eventData) const;

	// Settings (staff only)
	void        setSettings(std::string const & bgTexture, int32 srcX, int32 srcY, int32 srcW, int32 srcH);
	void        getSettings(std::string & outBgTexture, int32 & outSrcX, int32 & outSrcY, int32 & outSrcW, int32 & outSrcH) const;

	// Notification system
	void notifyEventCreated(CalendarEventData const & eventData);
	void notifyEventUpdated(CalendarEventData const & eventData);
	void notifyEventDeleted(CalendarEventData const & eventData);

	// Message handlers (called from Client.cpp)
	void handleGetEventsRequest(Client const & client, int32 year, int32 month);
	void handleCreateEventRequest(Client const & client, CalendarEventData const & eventData);
	void handleDeleteEventRequest(Client const & client, std::string const & eventId);
	void handleGetSettingsRequest(Client const & client);
	void handleApplySettingsRequest(Client const & client, std::string const & bgTexture, int32 srcX, int32 srcY, int32 srcW, int32 srcH);

	// MessageDispatch::Receiver
	void receiveMessage(MessageDispatch::Emitter const & source, MessageDispatch::MessageBase const & message);

	// Handlers for data loaded from database
	void handleLoadedEvents(LoadCalendarEventsMessage const & msg);
	void handleLoadedSettings(LoadCalendarSettingsMessage const & msg);

private:
	CalendarService();
	~CalendarService();
	CalendarService(CalendarService const &);
	CalendarService & operator=(CalendarService const &);

	// Event storage
	typedef std::map<std::string, CalendarEventData> EventMap;
	EventMap m_events;

	// Event counter for generating IDs
	uint32 m_nextEventId;

	// Settings
	std::string m_bgTexture;
	int32       m_srcX;
	int32       m_srcY;
	int32       m_srcW;
	int32       m_srcH;

	// Periodic check timer
	float m_checkTimer;
	static float const CHECK_INTERVAL;

	// Deferred DB load flag
	bool m_needsLoad;

	// Helper functions
	void        checkAndTriggerEvents();
	void        broadcastToGuild(int32 guildId, CalendarEventNotificationMessage const & msg);
	void        broadcastToCity(int32 cityId, CalendarEventNotificationMessage const & msg);
	void        broadcastToAll(CalendarEventNotificationMessage const & msg);
	void        sendToClient(Client const & client, GameNetworkMessage const & msg);
	std::string generateEventId();

	// Persistence (save/load to database)
	void        saveToClusterData();
	void        loadFromClusterData();
	void        saveEventToDatabase(CalendarEventData const & evt);
	void        deleteEventFromDatabase(std::string const & eventId);
	void        saveSettingsToDatabase();

	// Singleton instance
	static CalendarService * ms_instance;
};

// ======================================================================

#endif // INCLUDED_CalendarService_H

