// ======================================================================
//
// TaskSaveCalendarEvent.h
// Copyright 2026 Titan Project
//
// ======================================================================

#ifndef INCLUDED_TaskSaveCalendarEvent_H
#define INCLUDED_TaskSaveCalendarEvent_H

// ======================================================================

#include "sharedDatabaseInterface/DbTaskRequest.h"
#include "sharedFoundation/NetworkId.h"

#include <string>

// ======================================================================

class TaskSaveCalendarEvent : public DB::TaskRequest
{
public:
	TaskSaveCalendarEvent(
		std::string const & eventId,
		std::string const & title,
		std::string const & description,
		int eventType,
		int year, int month, int day,
		int hour, int minute,
		int duration,
		int guildId, int cityId,
		std::string const & serverEventKey,
		bool recurring, int recurrenceType,
		bool broadcastStart, bool active,
		NetworkId const & creatorId
	);
	virtual ~TaskSaveCalendarEvent();

	virtual bool process(DB::Session *session);
	virtual void onComplete();

private:
	std::string m_eventId;
	std::string m_title;
	std::string m_description;
	int m_eventType;
	int m_year, m_month, m_day;
	int m_hour, m_minute;
	int m_duration;
	int m_guildId, m_cityId;
	std::string m_serverEventKey;
	bool m_recurring;
	int m_recurrenceType;
	bool m_broadcastStart;
	bool m_active;
	NetworkId m_creatorId;
};

// ======================================================================

#endif

