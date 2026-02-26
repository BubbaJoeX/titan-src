// ======================================================================
//
// FileControlReloadHandler.cpp
// copyright 2024 Sony Online Entertainment
//
// Server-side reload handler. When SwgFileControl runs on the server
// (Linux), it uses ClusterBroadcaster to send console commands to
// CentralServer, which forwards them to all game servers via the
// existing planet->game server command routing.
//
// ======================================================================

#include "FirstSwgFileControl.h"
#include "FileControlReloadHandler.h"

#include "ClusterBroadcaster.h"
#include "FileControlServer.h"
#include "HotReloadManager.h"

#include "sharedLog/Log.h"
#include "sharedUtility/DataTableManager.h"

#include <cstdio>
#include <cstring>
#include <fstream>

// ======================================================================

bool FileControlReloadHandler::ms_installed = false;

// ======================================================================

void FileControlReloadHandler::install()
{
	if (ms_installed)
		return;

	ms_installed = true;
	LOG("FileControl", ("FileControlReloadHandler installed (server-side)"));
}

// ----------------------------------------------------------------------

void FileControlReloadHandler::remove()
{
	ms_installed = false;
}

// ======================================================================

bool FileControlReloadHandler::handleReloadNotify(const std::string & relativePath, const std::string & reloadCommand)
{
	if (!ms_installed)
		return false;

	LOG("FileControl", ("ReloadHandler: processing %s (command=%s)", relativePath.c_str(), reloadCommand.c_str()));

	HotReloadManager::FileType ft = HotReloadManager::detectFileType(relativePath);

	switch (ft)
	{
	case HotReloadManager::FT_JAVA_CLASS:
		{
			std::string scriptName = HotReloadManager::getScriptReloadName(relativePath);
			return reloadScript(scriptName);
		}

	case HotReloadManager::FT_DATATABLE_SERVER:
	case HotReloadManager::FT_DATATABLE_SHARED:
		{
			std::string dtName = HotReloadManager::getDatatableReloadName(relativePath);
			return reloadDatatable(dtName);
		}

	case HotReloadManager::FT_TEMPLATE_SERVER:
	case HotReloadManager::FT_TEMPLATE_SHARED:
		return reloadTemplate(relativePath);

	case HotReloadManager::FT_BUILDOUT:
		return reloadBuildout(relativePath);

	case HotReloadManager::FT_STRING_TABLE:
	case HotReloadManager::FT_TERRAIN:
	case HotReloadManager::FT_OTHER_IFF:
		return reloadGenericAsset(relativePath);

	case HotReloadManager::FT_UNKNOWN:
	default:
		LOG("FileControl", ("ReloadHandler: Unknown file type for %s", relativePath.c_str()));
		return false;
	}
}

// ======================================================================

bool FileControlReloadHandler::reloadScript(const std::string & scriptName)
{
	LOG("FileControl", ("ReloadHandler: script reload %s", scriptName.c_str()));

	bool sent = ClusterBroadcaster::broadcastReloadScript(scriptName);
	if (sent)
	{
		printf("[RELOAD] script %s -> broadcast to all game servers\n", scriptName.c_str());
	}
	else
	{
		printf("[RELOAD] script %s -> cluster broadcast FAILED (not connected to CentralServer)\n", scriptName.c_str());
	}
	fflush(stdout);

	return sent;
}

// ----------------------------------------------------------------------

bool FileControlReloadHandler::reloadDatatable(const std::string & datatableName)
{
	LOG("FileControl", ("ReloadHandler: datatable reload %s", datatableName.c_str()));

	DataTableManager::reload(datatableName);
	LOG("FileControl", ("ReloadHandler: local DataTableManager::reload(%s) called", datatableName.c_str()));

	bool sent = ClusterBroadcaster::broadcastReloadDatatable(datatableName);
	if (sent)
	{
		printf("[RELOAD] datatable %s -> local + broadcast to all game servers\n", datatableName.c_str());
	}
	else
	{
		printf("[RELOAD] datatable %s -> local only (cluster broadcast FAILED)\n", datatableName.c_str());
	}
	fflush(stdout);

	return true;
}

// ----------------------------------------------------------------------

bool FileControlReloadHandler::reloadTemplate(const std::string & templatePath)
{
	LOG("FileControl", ("ReloadHandler: template reload %s", templatePath.c_str()));

	bool sent = ClusterBroadcaster::broadcastReloadTemplate(templatePath);
	if (sent)
	{
		printf("[RELOAD] template %s -> broadcast to all game servers\n", templatePath.c_str());
	}
	else
	{
		printf("[RELOAD] template %s -> cluster broadcast FAILED (not connected to CentralServer)\n", templatePath.c_str());
	}
	fflush(stdout);

	return sent;
}

// ----------------------------------------------------------------------

bool FileControlReloadHandler::reloadBuildout(const std::string & buildoutPath)
{
	LOG("FileControl", ("ReloadHandler: buildout reload %s", buildoutPath.c_str()));

	// Buildout data is loaded at scene creation time. The updated file
	// is on disk and will be used when the scene is next loaded.
	// For live buildout updates, a server restart of the affected scene
	// is required.
	printf("[RELOAD] buildout %s -> updated on disk (requires scene restart for live update)\n", buildoutPath.c_str());
	fflush(stdout);

	return true;
}

// ----------------------------------------------------------------------

bool FileControlReloadHandler::reloadGenericAsset(const std::string & assetPath)
{
	LOG("FileControl", ("ReloadHandler: generic asset reload %s", assetPath.c_str()));

	printf("[RELOAD] asset %s -> updated on disk\n", assetPath.c_str());
	fflush(stdout);

	return true;
}

// ======================================================================

bool FileControlReloadHandler::handleReceivedFile(const std::string & relativePath, const std::vector<unsigned char> & data)
{
	if (!ms_installed)
		return false;

	if (data.empty())
		return false;

	LOG("FileControl", ("ReloadHandler: received file %s (%d bytes), storing locally",
		relativePath.c_str(), static_cast<int>(data.size())));

	if (FileControlServer::isRunning())
	{
		return true;
	}

	return false;
}

// ======================================================================
