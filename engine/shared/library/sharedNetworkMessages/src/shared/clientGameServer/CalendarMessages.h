// ======================================================================
//
// CalendarMessages.h
// Copyright 2026 Titan Project
//
// Calendar system network messages for client-server communication.
//
// ======================================================================

#ifndef INCLUDED_CalendarMessages_H
#define INCLUDED_CalendarMessages_H

// ======================================================================

#include "sharedNetworkMessages/GameNetworkMessage.h"
#include "sharedFoundation/NetworkId.h"
#include <vector>
#include <string>

// ======================================================================

// Calendar Event Data structure for network transfer
struct CalendarEventData
{
	std::string eventId;
	std::string title;
	std::string description;
	int32       eventType;      // 0=Staff, 1=Guild, 2=City, 3=Server
	int32       year;
	int32       month;
	int32       day;
	int32       hour;
	int32       minute;
	int32       duration;       // minutes
	int32       guildId;
	int32       cityId;
	std::string serverEventKey;
	bool        recurring;
	int32       recurrenceType; // 0=None, 1=Daily, 2=Weekly, 3=Monthly, 4=Yearly
	bool        broadcastStart;
	bool        active;
	NetworkId   creatorId;

	CalendarEventData();
};

// Archive functions for CalendarEventData
namespace Archive
{
	void get(ReadIterator & source, CalendarEventData & target);
	void put(ByteStream & target, CalendarEventData const & source);
}

// ======================================================================
// Client -> Server: Request calendar events for a player
// ======================================================================

class CalendarGetEventsMessage : public GameNetworkMessage
{
public:
	CalendarGetEventsMessage(int32 year, int32 month);
	explicit CalendarGetEventsMessage(Archive::ReadIterator & source);
	~CalendarGetEventsMessage();

	int32 getYear() const  { return m_year.get(); }
	int32 getMonth() const { return m_month.get(); }

private:
	CalendarGetEventsMessage();
	CalendarGetEventsMessage(CalendarGetEventsMessage const &);
	CalendarGetEventsMessage & operator=(CalendarGetEventsMessage const &);

	Archive::AutoVariable<int32> m_year;
	Archive::AutoVariable<int32> m_month;
};

// ======================================================================
// Server -> Client: Calendar events response
// ======================================================================

class CalendarEventsResponseMessage : public GameNetworkMessage
{
public:
	CalendarEventsResponseMessage(std::vector<CalendarEventData> const & events);
	explicit CalendarEventsResponseMessage(Archive::ReadIterator & source);
	~CalendarEventsResponseMessage();

	std::vector<CalendarEventData> const & getEvents() const { return m_events.get(); }

private:
	CalendarEventsResponseMessage();
	CalendarEventsResponseMessage(CalendarEventsResponseMessage const &);
	CalendarEventsResponseMessage & operator=(CalendarEventsResponseMessage const &);

	Archive::AutoArray<CalendarEventData> m_events;
};

// ======================================================================
// Client -> Server: Create a calendar event
// ======================================================================

class CalendarCreateEventMessage : public GameNetworkMessage
{
public:
	CalendarCreateEventMessage(CalendarEventData const & eventData);
	explicit CalendarCreateEventMessage(Archive::ReadIterator & source);
	~CalendarCreateEventMessage();

	CalendarEventData getEventData() const;

private:
	CalendarCreateEventMessage();
	CalendarCreateEventMessage(CalendarCreateEventMessage const &);
	CalendarCreateEventMessage & operator=(CalendarCreateEventMessage const &);

	Archive::AutoVariable<std::string> m_title;
	Archive::AutoVariable<std::string> m_description;
	Archive::AutoVariable<int32>       m_eventType;
	Archive::AutoVariable<int32>       m_year;
	Archive::AutoVariable<int32>       m_month;
	Archive::AutoVariable<int32>       m_day;
	Archive::AutoVariable<int32>       m_hour;
	Archive::AutoVariable<int32>       m_minute;
	Archive::AutoVariable<int32>       m_duration;
	Archive::AutoVariable<int32>       m_guildId;
	Archive::AutoVariable<int32>       m_cityId;
	Archive::AutoVariable<std::string> m_serverEventKey;
	Archive::AutoVariable<bool>        m_recurring;
	Archive::AutoVariable<int32>       m_recurrenceType;
	Archive::AutoVariable<bool>        m_broadcastStart;
};

// ======================================================================
// Server -> Client: Create event response
// ======================================================================

class CalendarCreateEventResponseMessage : public GameNetworkMessage
{
public:
	CalendarCreateEventResponseMessage(bool success, std::string const & eventId, std::string const & errorMessage);
	explicit CalendarCreateEventResponseMessage(Archive::ReadIterator & source);
	~CalendarCreateEventResponseMessage();

	bool               getSuccess() const      { return m_success.get(); }
	std::string const & getEventId() const     { return m_eventId.get(); }
	std::string const & getErrorMessage() const { return m_errorMessage.get(); }

private:
	CalendarCreateEventResponseMessage();
	CalendarCreateEventResponseMessage(CalendarCreateEventResponseMessage const &);
	CalendarCreateEventResponseMessage & operator=(CalendarCreateEventResponseMessage const &);

	Archive::AutoVariable<bool>        m_success;
	Archive::AutoVariable<std::string> m_eventId;
	Archive::AutoVariable<std::string> m_errorMessage;
};

// ======================================================================
// Client -> Server: Delete a calendar event
// ======================================================================

class CalendarDeleteEventMessage : public GameNetworkMessage
{
public:
	CalendarDeleteEventMessage(std::string const & eventId);
	explicit CalendarDeleteEventMessage(Archive::ReadIterator & source);
	~CalendarDeleteEventMessage();

	std::string const & getEventId() const { return m_eventId.get(); }

private:
	CalendarDeleteEventMessage();
	CalendarDeleteEventMessage(CalendarDeleteEventMessage const &);
	CalendarDeleteEventMessage & operator=(CalendarDeleteEventMessage const &);

	Archive::AutoVariable<std::string> m_eventId;
};

// ======================================================================
// Server -> Client: Delete event response
// ======================================================================

class CalendarDeleteEventResponseMessage : public GameNetworkMessage
{
public:
	CalendarDeleteEventResponseMessage(bool success, std::string const & eventId);
	explicit CalendarDeleteEventResponseMessage(Archive::ReadIterator & source);
	~CalendarDeleteEventResponseMessage();

	bool               getSuccess() const  { return m_success.get(); }
	std::string const & getEventId() const { return m_eventId.get(); }

private:
	CalendarDeleteEventResponseMessage();
	CalendarDeleteEventResponseMessage(CalendarDeleteEventResponseMessage const &);
	CalendarDeleteEventResponseMessage & operator=(CalendarDeleteEventResponseMessage const &);

	Archive::AutoVariable<bool>        m_success;
	Archive::AutoVariable<std::string> m_eventId;
};

// ======================================================================
// Server -> Client: Event notification (push when another player changes data)
// ======================================================================

class CalendarEventNotificationMessage : public GameNetworkMessage
{
public:
	enum NotificationType
	{
		NT_Created = 0,
		NT_Updated = 1,
		NT_Deleted = 2,
		NT_Started = 3,
		NT_Ended   = 4
	};

	CalendarEventNotificationMessage(int32 type, CalendarEventData const & eventData);
	explicit CalendarEventNotificationMessage(Archive::ReadIterator & source);
	~CalendarEventNotificationMessage();

	int32             getNotificationType() const { return m_notificationType.get(); }
	CalendarEventData getEventData() const;

private:
	CalendarEventNotificationMessage();
	CalendarEventNotificationMessage(CalendarEventNotificationMessage const &);
	CalendarEventNotificationMessage & operator=(CalendarEventNotificationMessage const &);

	Archive::AutoVariable<int32>       m_notificationType;
	Archive::AutoVariable<std::string> m_eventId;
	Archive::AutoVariable<std::string> m_title;
	Archive::AutoVariable<std::string> m_description;
	Archive::AutoVariable<int32>       m_eventType;
	Archive::AutoVariable<int32>       m_year;
	Archive::AutoVariable<int32>       m_month;
	Archive::AutoVariable<int32>       m_day;
	Archive::AutoVariable<int32>       m_hour;
	Archive::AutoVariable<int32>       m_minute;
	Archive::AutoVariable<int32>       m_duration;
	Archive::AutoVariable<int32>       m_guildId;
	Archive::AutoVariable<int32>       m_cityId;
	Archive::AutoVariable<std::string> m_serverEventKey;
	Archive::AutoVariable<bool>        m_recurring;
	Archive::AutoVariable<int32>       m_recurrenceType;
	Archive::AutoVariable<bool>        m_broadcastStart;
	Archive::AutoVariable<bool>        m_active;
	Archive::AutoVariable<std::string> m_creatorId;
};

// ======================================================================
// Client -> Server: Get calendar settings (staff only)
// ======================================================================

class CalendarGetSettingsMessage : public GameNetworkMessage
{
public:
	CalendarGetSettingsMessage();
	explicit CalendarGetSettingsMessage(Archive::ReadIterator & source);
	~CalendarGetSettingsMessage();

private:
	CalendarGetSettingsMessage(CalendarGetSettingsMessage const &);
	CalendarGetSettingsMessage & operator=(CalendarGetSettingsMessage const &);
};

// ======================================================================
// Server -> Client: Settings response
// ======================================================================

class CalendarSettingsResponseMessage : public GameNetworkMessage
{
public:
	CalendarSettingsResponseMessage(std::string const & bgTexture, int32 srcX, int32 srcY, int32 srcW, int32 srcH);
	explicit CalendarSettingsResponseMessage(Archive::ReadIterator & source);
	~CalendarSettingsResponseMessage();

	std::string const & getBgTexture() const { return m_bgTexture.get(); }
	int32 getSrcX() const { return m_srcX.get(); }
	int32 getSrcY() const { return m_srcY.get(); }
	int32 getSrcW() const { return m_srcW.get(); }
	int32 getSrcH() const { return m_srcH.get(); }

private:
	CalendarSettingsResponseMessage();
	CalendarSettingsResponseMessage(CalendarSettingsResponseMessage const &);
	CalendarSettingsResponseMessage & operator=(CalendarSettingsResponseMessage const &);

	Archive::AutoVariable<std::string> m_bgTexture;
	Archive::AutoVariable<int32>       m_srcX;
	Archive::AutoVariable<int32>       m_srcY;
	Archive::AutoVariable<int32>       m_srcW;
	Archive::AutoVariable<int32>       m_srcH;
};

// ======================================================================
// Client -> Server: Apply calendar settings (staff only)
// ======================================================================

class CalendarApplySettingsMessage : public GameNetworkMessage
{
public:
	CalendarApplySettingsMessage(std::string const & bgTexture, int32 srcX, int32 srcY, int32 srcW, int32 srcH);
	explicit CalendarApplySettingsMessage(Archive::ReadIterator & source);
	~CalendarApplySettingsMessage();

	std::string const & getBgTexture() const { return m_bgTexture.get(); }
	int32 getSrcX() const { return m_srcX.get(); }
	int32 getSrcY() const { return m_srcY.get(); }
	int32 getSrcW() const { return m_srcW.get(); }
	int32 getSrcH() const { return m_srcH.get(); }

private:
	CalendarApplySettingsMessage();
	CalendarApplySettingsMessage(CalendarApplySettingsMessage const &);
	CalendarApplySettingsMessage & operator=(CalendarApplySettingsMessage const &);

	Archive::AutoVariable<std::string> m_bgTexture;
	Archive::AutoVariable<int32>       m_srcX;
	Archive::AutoVariable<int32>       m_srcY;
	Archive::AutoVariable<int32>       m_srcW;
	Archive::AutoVariable<int32>       m_srcH;
};

// ======================================================================

#endif // INCLUDED_CalendarMessages_H

