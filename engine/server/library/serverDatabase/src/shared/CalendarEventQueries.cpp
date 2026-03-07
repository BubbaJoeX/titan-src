// ======================================================================
//
// CalendarEventQueries.cpp
// Copyright 2026 Titan Project
//
// Database queries for the calendar system.
//
// ======================================================================

#include "serverDatabase/FirstServerDatabase.h"
#include "serverDatabase/CalendarEventQueries.h"

#include "serverDatabase/DatabaseProcess.h"

using namespace DBQuery;

// ======================================================================
// SaveCalendarEventQuery
// ======================================================================

SaveCalendarEventQuery::SaveCalendarEventQuery(
	std::string const & eventId,
	std::string const & title,
	std::string const & description,
	int eventType,
	int year,
	int month,
	int day,
	int hour,
	int minute,
	int duration,
	int guildId,
	int cityId,
	std::string const & serverEventKey,
	bool recurring,
	int recurrenceType,
	bool broadcastStart,
	bool active,
	NetworkId const & creatorId
)
{
	m_eventId.setValue(eventId);
	m_title.setValue(title);
	m_description.setValue(description);
	m_eventType.setValue(eventType);
	m_year.setValue(year);
	m_month.setValue(month);
	m_day.setValue(day);
	m_hour.setValue(hour);
	m_minute.setValue(minute);
	m_duration.setValue(duration);
	m_guildId.setValue(guildId);
	m_cityId.setValue(cityId);
	m_serverEventKey.setValue(serverEventKey);
	m_recurring.setValue(recurring ? "Y" : "N");
	m_recurrenceType.setValue(recurrenceType);
	m_broadcastStart.setValue(broadcastStart ? "Y" : "N");
	m_active.setValue(active ? "Y" : "N");
	m_creatorId.setValue(creatorId);
}

void SaveCalendarEventQuery::getSQL(std::string &sql)
{
	sql = std::string("begin ")
		+ DatabaseProcess::getInstance().getSchemaQualifier()
		+ "persister.save_calendar_event("
		  ":event_id, :title, :description, :event_type, "
		  ":event_year, :event_month, :event_day, :event_hour, :event_minute, "
		  ":duration, :guild_id, :city_id, :server_event_key, "
		  ":recurring, :recurrence_type, :broadcast_start, :active, :creator_id"
		  "); end;";
}

bool SaveCalendarEventQuery::bindParameters()
{
	if (!bindParameter(m_eventId)) return false;
	if (!bindParameter(m_title)) return false;
	if (!bindParameter(m_description)) return false;
	if (!bindParameter(m_eventType)) return false;
	if (!bindParameter(m_year)) return false;
	if (!bindParameter(m_month)) return false;
	if (!bindParameter(m_day)) return false;
	if (!bindParameter(m_hour)) return false;
	if (!bindParameter(m_minute)) return false;
	if (!bindParameter(m_duration)) return false;
	if (!bindParameter(m_guildId)) return false;
	if (!bindParameter(m_cityId)) return false;
	if (!bindParameter(m_serverEventKey)) return false;
	if (!bindParameter(m_recurring)) return false;
	if (!bindParameter(m_recurrenceType)) return false;
	if (!bindParameter(m_broadcastStart)) return false;
	if (!bindParameter(m_active)) return false;
	if (!bindParameter(m_creatorId)) return false;
	return true;
}

bool SaveCalendarEventQuery::bindColumns()
{
	return true;
}

DB::Query::QueryMode SaveCalendarEventQuery::getExecutionMode() const
{
	return MODE_PROCEXEC;
}

// ======================================================================
// DeleteCalendarEventQuery
// ======================================================================

DeleteCalendarEventQuery::DeleteCalendarEventQuery(std::string const & eventId)
{
	m_eventId.setValue(eventId);
}

void DeleteCalendarEventQuery::getSQL(std::string &sql)
{
	sql = std::string("begin ")
		+ DatabaseProcess::getInstance().getSchemaQualifier()
		+ "persister.delete_calendar_event(:event_id); end;";
}

bool DeleteCalendarEventQuery::bindParameters()
{
	if (!bindParameter(m_eventId)) return false;
	return true;
}

bool DeleteCalendarEventQuery::bindColumns()
{
	return true;
}

DB::Query::QueryMode DeleteCalendarEventQuery::getExecutionMode() const
{
	return MODE_PROCEXEC;
}

// ======================================================================
// LoadCalendarEventsQuery
// ======================================================================

LoadCalendarEventsQuery::LoadCalendarEventsQuery()
{
}

void LoadCalendarEventsQuery::getSQL(std::string &sql)
{
	sql = std::string("begin :rc := ")
		+ DatabaseProcess::getInstance().getSchemaQualifier()
		+ "loader.load_calendar_events; end;";
}

bool LoadCalendarEventsQuery::bindParameters()
{
	return true;
}

bool LoadCalendarEventsQuery::bindColumns()
{
	if (!bindCol(m_eventId)) return false;
	if (!bindCol(m_title)) return false;
	if (!bindCol(m_description)) return false;
	if (!bindCol(m_eventType)) return false;
	if (!bindCol(m_year)) return false;
	if (!bindCol(m_month)) return false;
	if (!bindCol(m_day)) return false;
	if (!bindCol(m_hour)) return false;
	if (!bindCol(m_minute)) return false;
	if (!bindCol(m_duration)) return false;
	if (!bindCol(m_guildId)) return false;
	if (!bindCol(m_cityId)) return false;
	if (!bindCol(m_serverEventKey)) return false;
	if (!bindCol(m_recurring)) return false;
	if (!bindCol(m_recurrenceType)) return false;
	if (!bindCol(m_broadcastStart)) return false;
	if (!bindCol(m_active)) return false;
	if (!bindCol(m_creatorId)) return false;
	return true;
}

void LoadCalendarEventsQuery::getData(
	std::string & eventId,
	std::string & title,
	std::string & description,
	long & eventType,
	long & year,
	long & month,
	long & day,
	long & hour,
	long & minute,
	long & duration,
	long & guildId,
	long & cityId,
	std::string & serverEventKey,
	std::string & recurring,
	long & recurrenceType,
	std::string & broadcastStart,
	std::string & active,
	NetworkId & creatorId
) const
{
	m_eventId.getValue(eventId);
	m_title.getValue(title);
	m_description.getValue(description);
	m_eventType.getValue(eventType);
	m_year.getValue(year);
	m_month.getValue(month);
	m_day.getValue(day);
	m_hour.getValue(hour);
	m_minute.getValue(minute);
	m_duration.getValue(duration);
	m_guildId.getValue(guildId);
	m_cityId.getValue(cityId);
	m_serverEventKey.getValue(serverEventKey);
	m_recurring.getValue(recurring);
	m_recurrenceType.getValue(recurrenceType);
	m_broadcastStart.getValue(broadcastStart);
	m_active.getValue(active);
	m_creatorId.getValue(creatorId);
}

DB::Query::QueryMode LoadCalendarEventsQuery::getExecutionMode() const
{
	return MODE_PLSQL_REFCURSOR;
}

// ======================================================================
// SaveCalendarSettingsQuery
// ======================================================================

SaveCalendarSettingsQuery::SaveCalendarSettingsQuery(
	std::string const & bgTexture,
	int srcX, int srcY, int srcW, int srcH,
	NetworkId const & modifiedBy
)
{
	m_bgTexture.setValue(bgTexture);
	m_srcX.setValue(srcX);
	m_srcY.setValue(srcY);
	m_srcW.setValue(srcW);
	m_srcH.setValue(srcH);
	m_modifiedBy.setValue(modifiedBy);
}

void SaveCalendarSettingsQuery::getSQL(std::string &sql)
{
	sql = std::string("begin ")
		+ DatabaseProcess::getInstance().getSchemaQualifier()
		+ "persister.save_calendar_settings("
		  ":bg_texture, :src_x, :src_y, :src_w, :src_h, :modified_by"
		  "); end;";
}

bool SaveCalendarSettingsQuery::bindParameters()
{
	if (!bindParameter(m_bgTexture)) return false;
	if (!bindParameter(m_srcX)) return false;
	if (!bindParameter(m_srcY)) return false;
	if (!bindParameter(m_srcW)) return false;
	if (!bindParameter(m_srcH)) return false;
	if (!bindParameter(m_modifiedBy)) return false;
	return true;
}

bool SaveCalendarSettingsQuery::bindColumns()
{
	return true;
}

DB::Query::QueryMode SaveCalendarSettingsQuery::getExecutionMode() const
{
	return MODE_PROCEXEC;
}

// ======================================================================
// LoadCalendarSettingsQuery
// ======================================================================

LoadCalendarSettingsQuery::LoadCalendarSettingsQuery()
{
}

void LoadCalendarSettingsQuery::getSQL(std::string &sql)
{
	sql = std::string("begin :rc := ")
		+ DatabaseProcess::getInstance().getSchemaQualifier()
		+ "loader.load_calendar_settings; end;";
}

bool LoadCalendarSettingsQuery::bindParameters()
{
	return true;
}

bool LoadCalendarSettingsQuery::bindColumns()
{
	if (!bindCol(m_bgTexture)) return false;
	if (!bindCol(m_srcX)) return false;
	if (!bindCol(m_srcY)) return false;
	if (!bindCol(m_srcW)) return false;
	if (!bindCol(m_srcH)) return false;
	return true;
}

void LoadCalendarSettingsQuery::getData(
	std::string & bgTexture,
	long & srcX, long & srcY, long & srcW, long & srcH
) const
{
	m_bgTexture.getValue(bgTexture);
	m_srcX.getValue(srcX);
	m_srcY.getValue(srcY);
	m_srcW.getValue(srcW);
	m_srcH.getValue(srcH);
}

DB::Query::QueryMode LoadCalendarSettingsQuery::getExecutionMode() const
{
	return MODE_PLSQL_REFCURSOR;
}

// ======================================================================

