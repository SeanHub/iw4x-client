#include "STDInclude.hpp"

namespace Main
{
	void Initialize()
	{
		Utils::SetEnvironment();
		Utils::Cryptography::Initialize();
		Components::Loader::Initialize();

#if defined(DEBUG) || defined(FORCE_UNIT_TESTS)
		if (Components::Loader::IsPerformingUnitTests())
		{
			auto result = (Components::Loader::PerformUnitTests() ? 0 : -1);
			Components::Loader::Uninitialize();
			ExitProcess(result);
		}
#else
		if (Components::Flags::HasFlag("tests"))
		{
			Components::Logger::Print("Unit tests are disabled outside the debug environment!\n");
		}
#endif
	}

	void Uninitialize()
	{
		Components::Loader::Uninitialize();
		google::protobuf::ShutdownProtobufLibrary();
	}

	__declspec(naked) void EntryPoint()
	{
		__asm
		{
			pushad
			call Main::Initialize
			popad

			push 6BAA2Fh // Continue init routine
			push 6CA062h // __security_init_cookie
			retn
		}
	}
}

BOOL APIENTRY DllMain(HINSTANCE /*hinstDLL*/, DWORD fdwReason, LPVOID /*lpvReserved*/)
{
	if (fdwReason == DLL_PROCESS_ATTACH)
	{
		SetProcessDEPPolicy(PROCESS_DEP_ENABLE);
		Steam::Proxy::RunMod();

		std::srand(std::uint32_t(std::time(nullptr)) ^ ~(GetTickCount() * GetCurrentProcessId()));

#ifndef DISABLE_BINARY_CHECK
		// Ensure we're working with our desired binary

#ifndef DEBUG_BINARY_CHECK
		const auto* binary = reinterpret_cast<const char*>(0x6F9358);
		if (binary == nullptr || std::strcmp(binary, BASEGAME_NAME) != 0)
#endif
		{
			MessageBoxA(nullptr, 
				"Failed to load game binary.\n"
				"You did not install the iw4x-rawfiles!\n"
				"Please use the XLabs launcher to run the game. For support, please visit https://xlabs.dev/support_iw4x_client",
				"ERROR",
				MB_ICONERROR
			);
			return FALSE;
		}
#endif

		// Install entry point hook
		Utils::Hook(0x6BAC0F, Main::EntryPoint, HOOK_JUMP).install()->quick();
	}
	else if (fdwReason == DLL_PROCESS_DETACH)
	{
		Main::Uninitialize();
	}

	return TRUE;
}
