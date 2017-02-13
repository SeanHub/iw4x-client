#include "STDInclude.hpp"

namespace Components
{
	Utils::Time::Interval AntiCheat::LastCheck;
	std::string AntiCheat::Hash;
	Utils::Hook AntiCheat::LoadLibHook[4];
	unsigned long AntiCheat::Flags = NO_FLAG;

	// This function does nothing, it only adds the two passed variables and returns the value
	// The only important thing it does is to clean the first parameter, and then return
	// By returning, the crash procedure will be called, as it hasn't been cleaned from the stack
	__declspec(naked) void AntiCheat::NullSub()
	{
		__asm
		{
			push ebp
			push ecx
			mov ebp, esp

			xor eax, eax
			mov eax, [ebp + 8h]
			mov ecx, [ebp + 0Ch]
			add eax, ecx

			pop ecx
			pop ebp
			retn 4
		}
	}

	void AntiCheat::CrashClient()
	{
#ifdef DEBUG_DETECTIONS
		Logger::Flush();
		MessageBoxA(nullptr, "Check the log for more information!", "AntiCheat triggered", MB_ICONERROR);
		ExitProcess(0xFFFFFFFF);
#else
		static std::thread triggerThread;
		if (!triggerThread.joinable())
		{
			triggerThread = std::thread([] ()
			{
				std::this_thread::sleep_for(43s);
				Utils::Hook::Set<BYTE>(0x41BA2C, 0xEB);
			});
		}
#endif
	}

	void AntiCheat::AssertCalleeModule(void* callee)
	{
		HMODULE hModuleSelf = nullptr, hModuleTarget = nullptr, hModuleProcess = GetModuleHandleA(nullptr);
		GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, reinterpret_cast<char*>(callee), &hModuleTarget);
		GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, reinterpret_cast<char*>(AntiCheat::AssertCalleeModule), &hModuleSelf);

		if (!hModuleSelf || !hModuleTarget || !hModuleProcess || (hModuleTarget != hModuleSelf && hModuleTarget != hModuleProcess))
		{
#ifdef DEBUG_DETECTIONS
			char buffer[MAX_PATH] = { 0 };
			GetModuleFileNameA(hModuleTarget, buffer, sizeof buffer);

			Logger::Print(Utils::String::VA("AntiCheat: Callee assertion failed: %X %s", reinterpret_cast<uint32_t>(callee), buffer));
#endif

			AntiCheat::CrashClient();
		}
	}

	void AntiCheat::InitLoadLibHook()
	{
		static uint8_t kernel32Str[] = { 0xB4, 0x9A, 0x8D, 0xB1, 0x9A, 0x93, 0xCC, 0xCD, 0xD1, 0x9B, 0x93, 0x93 }; // KerNel32.dll
		static uint8_t loadLibAStr[] = { 0xB3, 0x90, 0x9E, 0x9B, 0xB3, 0x96, 0x9D, 0x8D, 0x9E, 0x8D, 0x86, 0xBE }; // LoadLibraryA
		static uint8_t loadLibWStr[] = { 0xB3, 0x90, 0x9E, 0x9B, 0xB3, 0x96, 0x9D, 0x8D, 0x9E, 0x8D, 0x86, 0xA8 }; // LoadLibraryW

		HMODULE kernel32 = GetModuleHandleA(Utils::String::XOR(std::string(reinterpret_cast<char*>(kernel32Str), sizeof kernel32Str), -1).data());
		if (kernel32)
		{
			FARPROC loadLibA = GetProcAddress(kernel32, Utils::String::XOR(std::string(reinterpret_cast<char*>(loadLibAStr), sizeof loadLibAStr), -1).data());
			FARPROC loadLibW = GetProcAddress(kernel32, Utils::String::XOR(std::string(reinterpret_cast<char*>(loadLibWStr), sizeof loadLibWStr), -1).data());

			std::string libExA = Utils::String::XOR(std::string(reinterpret_cast<char*>(loadLibAStr), sizeof loadLibAStr), -1);
			std::string libExW = Utils::String::XOR(std::string(reinterpret_cast<char*>(loadLibWStr), sizeof loadLibWStr), -1);

			libExA.insert(libExA.end() - 1, 'E');
			libExA.insert(libExA.end() - 1, 'x');

			libExW.insert(libExW.end() - 1, 'E');
			libExW.insert(libExW.end() - 1, 'x');

			FARPROC loadLibExA = GetProcAddress(kernel32, libExA.data());
			FARPROC loadLibExW = GetProcAddress(kernel32, libExW.data());

			if (loadLibA && loadLibW && loadLibExA && loadLibExW)
			{
#ifdef DEBUG_LOAD_LIBRARY
				AntiCheat::LoadLibHook[0].initialize(loadLibA, LoadLibaryAStub, HOOK_JUMP);
				AntiCheat::LoadLibHook[1].initialize(loadLibW, LoadLibaryWStub, HOOK_JUMP);
				AntiCheat::LoadLibHook[2].initialize(loadLibExA, LoadLibaryAStub, HOOK_JUMP);
				AntiCheat::LoadLibHook[3].initialize(loadLibExW, LoadLibaryWStub, HOOK_JUMP);
#else
				static uint8_t loadLibStub[] = { 0x33, 0xC0, 0xC2, 0x04, 0x00 }; // xor eax, eax; retn 04h
				static uint8_t loadLibExStub[] = { 0x33, 0xC0, 0xC2, 0x0C, 0x00 }; // xor eax, eax; retn 0Ch
				AntiCheat::LoadLibHook[0].initialize(loadLibA, loadLibStub, HOOK_JUMP);
				AntiCheat::LoadLibHook[1].initialize(loadLibW, loadLibStub, HOOK_JUMP);
				AntiCheat::LoadLibHook[2].initialize(loadLibExA, loadLibExStub, HOOK_JUMP);
				AntiCheat::LoadLibHook[3].initialize(loadLibExW, loadLibExStub, HOOK_JUMP);
#endif
			}
		}
	}

	void AntiCheat::ReadIntegrityCheck()
	{
#ifdef PROCTECT_PROCESS
		static Utils::Time::Interval check;

		if(check.elapsed(20s))
		{
			check.update();

			if (HANDLE h = OpenProcess(PROCESS_VM_READ, TRUE, GetCurrentProcessId()))
			{
#ifdef DEBUG_DETECTIONS
				Logger::Print("AntiCheat: Process integrity check failed");
#endif

				CloseHandle(h);
				AntiCheat::CrashClient();
			}
		}

		// Set the integrity flag
		AntiCheat::Flags |= AntiCheat::IntergrityFlag::READ_INTEGRITY_CHECK;
#endif
	}

	void AntiCheat::FlagIntegrityCheck()
	{
		static Utils::Time::Interval check;

		if (check.elapsed(30s))
		{
			check.update();

			unsigned long flags = ((AntiCheat::IntergrityFlag::MAX_FLAG - 1) << 1) - 1;

			if (AntiCheat::Flags != flags)
			{
#ifdef DEBUG_DETECTIONS
				Logger::Print(Utils::String::VA("AntiCheat: Flag integrity check failed: %X", AntiCheat::Flags));
#endif

				AntiCheat::CrashClient();
			}
		}
	}

	void AntiCheat::ScanIntegrityCheck()
	{
		// If there was no check within the last 40 seconds, crash!
		if (AntiCheat::LastCheck.elapsed(40s))
		{
#ifdef DEBUG_DETECTIONS
			Logger::Print("AntiCheat: Integrity check failed");
#endif

			AntiCheat::CrashClient();
		}

		// Set the integrity flag
		AntiCheat::Flags |= AntiCheat::IntergrityFlag::SCAN_INTEGRITY_CHECK;
	}

	void AntiCheat::PerformScan()
	{
		// Perform check only every 10 seconds
		if (!AntiCheat::LastCheck.elapsed(10s)) return;
		AntiCheat::LastCheck.update();

		// Hash .text segment
		// Add 1 to each value, so searching in memory doesn't reveal anything
		size_t textSize = 0x2D6001;
		uint8_t* textBase = reinterpret_cast<uint8_t*>(0x401001);
		std::string hash = Utils::Cryptography::SHA512::Compute(textBase - 1, textSize - 1, false);

		// Set the hash, if none is set
		if (AntiCheat::Hash.empty())
		{
			AntiCheat::Hash = hash;
		}
		// Crash if the hashes don't match
		else if (AntiCheat::Hash != hash)
		{
#ifdef DEBUG_DETECTIONS
			Logger::Print("AntiCheat: Memory scan failed");
#endif

			AntiCheat::CrashClient();
		}

		// Set the memory scan flag
		AntiCheat::Flags |= AntiCheat::IntergrityFlag::MEMORY_SCAN;
	}

#ifdef DEBUG_LOAD_LIBRARY
	HANDLE AntiCheat::LoadLibary(std::wstring library, HANDLE file, DWORD flags, void* callee)
	{
		HMODULE module;
		char buffer[MAX_PATH] = { 0 };

		GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, reinterpret_cast<char*>(callee), &module);
		GetModuleFileNameA(module, buffer, sizeof buffer);

		MessageBoxA(nullptr, Utils::String::VA("Loading library %s via %s %X", std::string(library.begin(), library.end()).data(), buffer, reinterpret_cast<uint32_t>(callee)), nullptr, 0);

		AntiCheat::LoadLibHook[3].uninstall();
		HANDLE h = LoadLibraryExW(library.data(), file, flags);
		AntiCheat::LoadLibHook[3].install();
		return h;
	}

	HANDLE WINAPI AntiCheat::LoadLibaryAStub(const char* library)
	{
		std::string lib(library);
		return AntiCheat::LoadLibary(std::wstring(lib.begin(), lib.end()), nullptr, 0, _ReturnAddress());
	}

	HANDLE WINAPI AntiCheat::LoadLibaryWStub(const wchar_t* library)
	{
		return AntiCheat::LoadLibary(library, nullptr, 0, _ReturnAddress());
	}

	HANDLE WINAPI AntiCheat::LoadLibaryExAStub(const char* library, HANDLE file, DWORD flags)
	{
		std::string lib(library);
		return AntiCheat::LoadLibary(std::wstring(lib.begin(), lib.end()), file, flags, _ReturnAddress());
	}

	HANDLE WINAPI AntiCheat::LoadLibaryExWStub(const wchar_t* library, HANDLE file, DWORD flags)
	{
		return AntiCheat::LoadLibary(library, file, flags, _ReturnAddress());
	}
#endif

	void AntiCheat::UninstallLibHook()
	{
		for (int i = 0; i < ARRAYSIZE(AntiCheat::LoadLibHook); ++i)
		{
			AntiCheat::LoadLibHook[i].uninstall();
		}
	}

	void AntiCheat::InstallLibHook()
	{
		AntiCheat::LoadLibHook[0].install();
		AntiCheat::LoadLibHook[1].install();
		AntiCheat::LoadLibHook[2].install();
		AntiCheat::LoadLibHook[3].install();
	}

	void AntiCheat::PatchWinAPI()
	{
		AntiCheat::UninstallLibHook();

		// Initialize directx
		Utils::Hook::Call<void()>(0x5078C0)();

		AntiCheat::InstallLibHook();
	}

	void AntiCheat::SoundInitStub(int a1, int a2, int a3)
	{
		AntiCheat::UninstallLibHook();

		Game::SND_Init(a1, a2, a3);

		AntiCheat::InstallLibHook();
	}

	void AntiCheat::SoundInitDriverStub()
	{
		AntiCheat::UninstallLibHook();

		Game::SND_InitDriver();

		AntiCheat::InstallLibHook();
	}

	void AntiCheat::LostD3DStub()
	{
		AntiCheat::UninstallLibHook();

		// Reset directx
		Utils::Hook::Call<void()>(0x508070)();

		AntiCheat::InstallLibHook();
	}

	__declspec(naked) void AntiCheat::CinematicStub()
	{
		__asm
		{
			pushad
			call AntiCheat::UninstallLibHook
			popad

			call Game::R_Cinematic_StartPlayback_Now

			pushad
			call AntiCheat::InstallLibHook
			popad

			retn
		}
	}

	__declspec(naked) void AntiCheat::DObjGetWorldTagPosStub()
	{
		__asm
		{
			pushad
			push [esp + 20h]

			call AntiCheat::AssertCalleeModule

			pop esi
			popad

			push ecx
			mov ecx, [esp + 10h]

			push 426585h
			retn
		}
	}

	__declspec(naked) void AntiCheat::AimTargetGetTagPosStub()
	{
		__asm
		{
			pushad
			push [esp + 20h]

			call AntiCheat::AssertCalleeModule

			pop esi
			popad

			sub esp, 14h
			cmp dword ptr[esi + 0E0h], 1
			push 56AC6Ah
			ret
		}
	}

	unsigned long AntiCheat::ProtectProcess()
	{
#ifdef PROCTECT_PROCESS
		Utils::Memory::Allocator allocator;

		HANDLE hToken = nullptr;
		if (!OpenProcessToken(GetCurrentProcess(), TOKEN_READ, &hToken))
		{
			if (!OpenThreadToken(GetCurrentThread(), TOKEN_READ, TRUE, &hToken))
			{
				return GetLastError();
			}
		}

		auto freeSid = [] (void* sid)
		{
			if (sid)
			{
				FreeSid(reinterpret_cast<PSID>(sid));
			}
		};

		allocator.reference(hToken, [] (void* hToken)
		{
			if (hToken)
			{
				CloseHandle(hToken);
			}
		});

		DWORD dwSize = 0;
		PVOID pTokenInfo = nullptr;
		if (GetTokenInformation(hToken, TokenUser, nullptr, 0, &dwSize) || GetLastError() != ERROR_INSUFFICIENT_BUFFER) return GetLastError();

		if (dwSize)
		{
			pTokenInfo = allocator.allocate(dwSize);
			if (!pTokenInfo) return GetLastError();
		}

		if (!GetTokenInformation(hToken, TokenUser, pTokenInfo, dwSize, &dwSize) || !pTokenInfo) return GetLastError();

		PSID psidCurUser = reinterpret_cast<TOKEN_USER*>(pTokenInfo)->User.Sid;

		PSID psidEveryone = nullptr;
		SID_IDENTIFIER_AUTHORITY sidEveryone = SECURITY_WORLD_SID_AUTHORITY;
		if (!AllocateAndInitializeSid(&sidEveryone, 1, SECURITY_WORLD_RID, 0, 0, 0, 0, 0, 0, 0, &psidEveryone) || !psidEveryone) return GetLastError();
		allocator.reference(psidEveryone, freeSid);

		PSID psidSystem = nullptr;
		SID_IDENTIFIER_AUTHORITY sidSystem = SECURITY_NT_AUTHORITY;
		if (!AllocateAndInitializeSid(&sidSystem, 1, SECURITY_LOCAL_SYSTEM_RID, 0, 0, 0, 0, 0, 0, 0, &psidSystem) || !psidSystem) return GetLastError();
		allocator.reference(psidSystem, freeSid);

		PSID psidAdmins = nullptr;
		SID_IDENTIFIER_AUTHORITY sidAdministrators = SECURITY_NT_AUTHORITY;
		if (!AllocateAndInitializeSid(&sidAdministrators, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &psidAdmins) || !psidAdmins) return GetLastError();
		allocator.reference(psidAdmins, freeSid);

		const PSID psidArray[] =
		{
			psidEveryone, /* Deny most rights to everyone */
			psidCurUser,  /* Allow what was not denied */
			psidSystem,   /* Full control */
			psidAdmins,   /* Full control */
		};

		// Determine required size of the ACL
		dwSize = sizeof(ACL);

		// First the DENY, then the ALLOW
		dwSize += GetLengthSid(psidArray[0]);
		dwSize += sizeof(ACCESS_DENIED_ACE) - sizeof(DWORD);

		for (UINT i = 1; i < _countof(psidArray); ++i)
		{
			// DWORD is the SidStart field, which is not used for absolute format
			dwSize += GetLengthSid(psidArray[i]);
			dwSize += sizeof(ACCESS_ALLOWED_ACE) - sizeof(DWORD);
		}

		PACL pDacl = reinterpret_cast<PACL>(allocator.allocate(dwSize));
		if (!pDacl || !InitializeAcl(pDacl, dwSize, ACL_REVISION)) return GetLastError();

		// Mimic Protected Process
		// http://www.microsoft.com/whdc/system/vista/process_vista.mspx
		// Protected processes allow PROCESS_TERMINATE, which is
		// probably not appropriate for high integrity software.
		static const DWORD dwPoison =
			/*READ_CONTROL |*/ WRITE_DAC | WRITE_OWNER |
			PROCESS_CREATE_PROCESS | PROCESS_CREATE_THREAD |
			PROCESS_DUP_HANDLE | PROCESS_QUERY_INFORMATION |
			PROCESS_SET_QUOTA | PROCESS_SET_INFORMATION |
			PROCESS_VM_OPERATION | PROCESS_VM_READ | PROCESS_VM_WRITE |
			// In addition to protected process
			PROCESS_SUSPEND_RESUME | PROCESS_TERMINATE;

		if (!AddAccessDeniedAce(pDacl, ACL_REVISION, dwPoison, psidArray[0])) return GetLastError();

		// Standard and specific rights not explicitly denied
		static const DWORD dwAllowed = (~dwPoison & 0x1FFF) | SYNCHRONIZE;
		if (!AddAccessAllowedAce(pDacl, ACL_REVISION, dwAllowed, psidArray[1])) return GetLastError();

		// Because of ACE ordering, System will effectively have dwAllowed even
		// though the ACE specifies PROCESS_ALL_ACCESS (unless software uses
		// SeDebugPrivilege or SeTcbName and increases access).
		// As an exercise, check behavior of tools such as Process Explorer under XP,
		// Vista, and above. Vista and above should exhibit slightly different behavior
		// due to Restricted tokens.
		if (!AddAccessAllowedAce(pDacl, ACL_REVISION, PROCESS_ALL_ACCESS, psidArray[2])) return GetLastError();

		// Because of ACE ordering, Administrators will effectively have dwAllowed
		// even though the ACE specifies PROCESS_ALL_ACCESS (unless the Administrator
		// invokes 'discretionary security' by taking ownership and increasing access).
		// As an exercise, check behavior of tools such as Process Explorer under XP,
		// Vista, and above. Vista and above should exhibit slightly different behavior
		// due to Restricted tokens.
		if (!AddAccessAllowedAce(pDacl, ACL_REVISION, PROCESS_ALL_ACCESS, psidArray[3])) return GetLastError();

		PSECURITY_DESCRIPTOR pSecDesc = allocator.allocate<SECURITY_DESCRIPTOR>();
		if (!pSecDesc) return GetLastError();

		// InitializeSecurityDescriptor initializes a security descriptor in
		// absolute format, rather than self-relative format. See
		// http://msdn.microsoft.com/en-us/library/aa378863(VS.85).aspx
		if (!InitializeSecurityDescriptor(pSecDesc, SECURITY_DESCRIPTOR_REVISION)) return GetLastError();
		if (!SetSecurityDescriptorDacl(pSecDesc, TRUE, pDacl, FALSE)) return GetLastError();

		return SetSecurityInfo(
			GetCurrentProcess(),
			SE_KERNEL_OBJECT, // process object
			OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
			psidCurUser, // NULL, // Owner SID
			nullptr, // Group SID
			pDacl,
			nullptr // SACL
		);
#else
		return 0;
#endif
	}

	AntiCheat::AntiCheat()
	{
		AntiCheat::Flags = NO_FLAG;
		AntiCheat::Hash.clear();

#ifdef DEBUG
		Command::Add("penis", [] (Command::Params*)
		{
			AntiCheat::CrashClient();
		});
#else

		Utils::Hook(0x507BD5, AntiCheat::PatchWinAPI, HOOK_CALL).install()->quick();
		Utils::Hook(0x5082FD, AntiCheat::LostD3DStub, HOOK_CALL).install()->quick();
		Utils::Hook(0x51C76C, AntiCheat::CinematicStub, HOOK_CALL).install()->quick();
		Utils::Hook(0x418209, AntiCheat::SoundInitStub, HOOK_CALL).install()->quick();
		Utils::Hook(0x60BE9D, AntiCheat::SoundInitStub, HOOK_CALL).install()->quick();
		Utils::Hook(0x60BE8E, AntiCheat::SoundInitDriverStub, HOOK_CALL).install()->quick();
		Utils::Hook(0x418204, AntiCheat::SoundInitDriverStub, HOOK_CALL).install()->quick();
		Renderer::OnFrame(AntiCheat::PerformScan);

		// Detect aimbots
		Utils::Hook(0x426580, AntiCheat::DObjGetWorldTagPosStub, HOOK_JUMP).install()->quick();
		Utils::Hook(0x56AC60, AntiCheat::AimTargetGetTagPosStub, HOOK_JUMP).install()->quick();

		// TODO: Probably move that :P
		if (!Dedicated::IsEnabled())
		{
			AntiCheat::InitLoadLibHook();
		}

		// Prevent external processes from accessing our memory
		AntiCheat::ProtectProcess();
		Renderer::OnDeviceRecoveryEnd([] ()
		{
			AntiCheat::ProtectProcess();
		});

		// Set the integrity flag
		AntiCheat::Flags |= AntiCheat::IntergrityFlag::INITIALIZATION;
#endif
	}

	AntiCheat::~AntiCheat()
	{
		AntiCheat::Flags = NO_FLAG;
		AntiCheat::Hash.clear();

		for (int i = 0; i < ARRAYSIZE(AntiCheat::LoadLibHook); ++i)
		{
			AntiCheat::LoadLibHook[i].uninstall();
		}
	}
}
