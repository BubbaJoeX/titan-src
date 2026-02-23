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

#include <iostream>
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

    // Connect once
    s_serverConnection = new ServerConsoleConnection(address, port);

    std::cout << "Pinged and connected to " << address << ":" << port << std::endl;
    std::cout << "Enter console commands. Type 'quit' to exit." << std::endl;

    while (!s_done)
    {
        // Process network first
        NetworkHandler::update();
        NetworkHandler::dispatch();

        // Blocking line input (this is what you want)
        std::string line;
        if (!std::getline(std::cin, line))
            break;

        if (line == "quit" || line == "exit")
            break;

        if (!line.empty())
        {
            ConGenericMessage msg(line);
            s_serverConnection->send(msg);
        }
    }

    std::cout << "ServerConsole exiting." << std::endl;
}

//-----------------------------------------------------------------------