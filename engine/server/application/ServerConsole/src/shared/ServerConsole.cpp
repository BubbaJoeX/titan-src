// ServerConsole.cpp

#include "FirstServerConsole.h"
#include "ConfigServerConsole.h"
#include "sharedFoundation/Os.h"
#include "sharedNetwork/Connection.h"
#include "sharedNetworkMessages/ConsoleChannelMessages.h"
#include "ServerConsole.h"
#include "ServerConsoleConnection.h"

#include <iostream>
#include <string>
#include <unistd.h>
#include <sys/select.h>

//-----------------------------------------------------------------------

namespace ServerConsoleNamespace
{
    ServerConsoleConnection * s_serverConnection = 0;
    bool s_done = false;
}

using namespace ServerConsoleNamespace;

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

    s_serverConnection = new ServerConsoleConnection(address, port);

    std::cout << "Connected to " << address << ":" << port << std::endl;
    std::cout << "Type commands. 'quit' to exit." << std::endl;

    while (!s_done)
    {
        // ---- Pump Network Every Loop ----
        NetworkHandler::update();
        NetworkHandler::dispatch();

        // ---- Check stdin without blocking ----
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(STDIN_FILENO, &readfds);

        timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 10000; // 10ms

        int ret = select(STDIN_FILENO + 1, &readfds, NULL, NULL, &tv);

        if (ret > 0 && FD_ISSET(STDIN_FILENO, &readfds))
        {
            std::string line;
            if (std::getline(std::cin, line))
            {
                if (line == "quit" || line == "exit")
                    break;

                if (!line.empty())
                {
                    ConGenericMessage msg(line);
                    s_serverConnection->send(msg);
                }
            }
        }
    }

    std::cout << "ServerConsole exiting." << std::endl;
}