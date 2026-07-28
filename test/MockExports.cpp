#include "Exports.h"

#include <imgui/imgui.h>

extern "C" __declspec(dllexport) void e3(const char* pString);
extern "C" __declspec(dllexport) void e5(ImVec4** pColors);
extern "C" __declspec(dllexport) uint64_t e6();
extern "C" __declspec(dllexport) uint64_t e7();
extern "C" __declspec(dllexport) void e9(cbtevent* pEvent, uint32_t pSignature);
extern "C" __declspec(dllexport) void e10(cbtevent* pEvent, uint32_t pSignature);

#pragma pack(push, 1)
namespace
{
	struct ArcModifiers
	{
		uint16_t _1 = VK_SHIFT;
		uint16_t _2 = VK_MENU;
		uint16_t Multi = 0;
		uint16_t Fill = 0;
	};
} // anonymous namespace
#pragma pack(pop)

void e3(const char* /*pString*/)
{
	return; // Logging, ignored
}

void e5(ImVec4** /*pColors*/)
{
	return; // Logging, ignored
}

uint64_t e6()
{
	return 0; // everything set to false
}

uint64_t e7()
{
	ArcModifiers mods;
	return *reinterpret_cast<uint64_t*>(&mods);
}

void e9(cbtevent*, uint32_t)
{
	return; // Ignore, can be overridden by specific test if need be
}

void e10(cbtevent*, uint32_t)
{
	return; // Ignore, can be overridden by specific test if need be
}
