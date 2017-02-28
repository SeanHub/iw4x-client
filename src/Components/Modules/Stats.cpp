#include "STDInclude.hpp"

namespace Components
{
	bool Stats::IsMaxLevel()
	{
		// 2516000 should be the max experience.
		return (Game::Live_GetXp(0) >= Game::CL_GetMaxXP());
	}

	void Stats::SendStats()
	{
		// check if we're connected to a server...
		if (*reinterpret_cast<std::uint32_t*>(0xB2C540) >= 7)
		{
			for (unsigned char i = 0; i < 7; i++)
			{
				Game::Com_Printf(0, "Sending stat packet %i to server.\n", i);

				// alloc
				Game::msg_t msg;
				ZeroMemory(&msg, sizeof(msg));

				Utils::Memory::Allocator allocator;
				char* buffer = allocator.allocateArray<char>(2048);

				// init
				Game::MSG_Init(&msg, buffer, 2048);
				Game::MSG_WriteString(&msg, "stats");

				// get stat buffer
				char *statbuffer = nullptr;
				if (Utils::Hook::Call<int(int)>(0x444CA0)(0))
				{
					statbuffer = &Utils::Hook::Call<char *(int)>(0x4C49F0)(0)[1240 * i];
				}

				// Client port?
				Game::MSG_WriteShort(&msg, *reinterpret_cast<short*>(0xA1E878));

				// Stat packet index
				Game::MSG_WriteByte(&msg, i);

				// calculate packet size
				size_t size = 8192 - (i * 1240);
				if (size > 1240)
					size = 1240;

				// write stat packet data
				if (statbuffer)
				{
					Game::MSG_WriteData(&msg, statbuffer, size);
				}

				// send statpacket
				Network::SendRaw(Game::NS_CLIENT, *reinterpret_cast<Game::netadr_t*>(0xA1E888), std::string(msg.data, msg.cursize));
			}
		}
	}

	void Stats::UpdateClasses(UIScript::Token)
	{
		SendStats();
	}

	Stats::Stats()
	{
		// This UIScript should be added in the onClose code of the cac_popup menu,
		// so everytime the create-a-class window is closed, and a client is connected
		// to a server, the stats data of the client will be reuploaded to the server.
		// allowing the player to change their classes while connected to a server.
		UIScript::Add("UpdateClasses", Stats::UpdateClasses);

		// Allow playerdata to be changed while connected to a server
		Utils::Hook::Set<BYTE>(0x4376FD, 0xEB);

		// ToDo: Allow playerdata changes in setPlayerData UI script.
	}
	Stats::~Stats()
	{

	}
}