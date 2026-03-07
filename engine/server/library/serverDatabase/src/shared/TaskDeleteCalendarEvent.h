// ======================================================================
//
// TaskDeleteCalendarEvent.h
// Copyright 2026 Titan Project
//
// ======================================================================

#ifndef INCLUDED_TaskDeleteCalendarEvent_H
#define INCLUDED_TaskDeleteCalendarEvent_H

// ======================================================================

#include "sharedDatabaseInterface/DbTaskRequest.h"

#include <string>

// ======================================================================

class TaskDeleteCalendarEvent : public DB::TaskRequest
{
public:
	explicit TaskDeleteCalendarEvent(std::string const & eventId);
	virtual ~TaskDeleteCalendarEvent();

	virtual bool process(DB::Session *session);
	virtual void onComplete();

private:
	std::string m_eventId;
};

// ======================================================================

#endif

