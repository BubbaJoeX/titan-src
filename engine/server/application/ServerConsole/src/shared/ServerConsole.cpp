// ServerConsole.cpp
// Copyright 2000-02, Sony Online Entertainment Inc., all rights reserved. 
// Author: Justin Randall

//-----------------------------------------------------------------------

#include "FirstServerConsole.h"
#include "ConfigServerConsole.h"
#include "sharedFoundation/Os.h"
#include "sharedNetwork/Connection.h"
#include "sharedNetworkMessages/ConsoleChannelMessages.h"
#include "ServerConsole.h"
#include "ServerConsoleConnection.h"

#include <cstdio>
#include <cstring>
#include <string>

//-----------------------------------------------------------------------

namespace ServerConsoleNamespace
{
    ServerConsoleConnection * s_serverConnection = 0;
    bool s_done = false;
}

using namespace ServerConsoleNamespace;

//-----------------------------------------------------------------------

ServerConsole::ServerConsole()
{
}

//-----------------------------------------------------------------------

ServerConsole::~ServerConsole()
{
}

//-----------------------------------------------------------------------

void ServerConsole::done()
{
    s_done = true;
}

//-----------------------------------------------------------------------

void ServerConsole::run()
{
    const char * address = ConfigServerConsole::getServerAddress();
    const uint16 port = ConfigServerConsole::getServerPort();

    if (!address || !port)
        return;

    if (!stdin)
        return;

    std::string input;
    char inBuf[1024];

    // FIXED: read partial blocks correctly from stdin
    size_t bytesRead = 0;
    while ((bytesRead = fread(inBuf, 1, sizeof(inBuf), stdin)) > 0)
    {
        input.append(inBuf, bytesRead);
    }

    if (!input.empty())
    {
        // connect to the server
        s_serverConnection = new ServerConsoleConnection(address, port);

        ConGenericMessage msg(input);
        s_serverConnection->send(msg);

        while (!s_done)
        {
            NetworkHandler::update();
            NetworkHandler::dispatch();
            Os::sleep(1);
        }
    }
    else
    {
        fprintf(stderr, "Nothing to send to the server. Aborting\n");
    }
}

//-----------------------------------------------------------------------