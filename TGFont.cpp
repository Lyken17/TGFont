#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdint>
#include <map>
#include <string>

#include "winmm.h"
#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/encodings.h"
#include "rapidjson/filereadstream.h"
#include "rapidjson/encodedstream.h"

using namespace rapidjson;

#pragma pack(push, 1)
struct jmp
{
	uint8_t opcode;
	size_t address;
};
#pragma pack(pop)

typedef HFONT(WINAPI* fnCreateFontIndirectW)(const LOGFONTW *lplf);
fnCreateFontIndirectW origAddr = nullptr;

struct font
{
	std::wstring replace;
	bool overrideSize;
	size_t size;
};

std::map<std::wstring, font> fontsMap;


__declspec(naked) HFONT WINAPI CallOrigFn(const LOGFONTW *lplf)
{
	_asm
	{
		mov edi, edi
		push ebp
		mov ebp, esp
		jmp origAddr
	}
}

HFONT WINAPI MyCreateFontIndirectW(LOGFONTW *lplf)
{
	auto it = fontsMap.find(lplf->lfFaceName);
	if (it != fontsMap.end())
	{
		size_t len = it->second.replace.copy(lplf->lfFaceName, LF_FACESIZE);
		lplf->lfFaceName[len] = L'\0';

		if (it->second.overrideSize)
			lplf->lfHeight = it->second.size;
	}
	return CallOrigFn(lplf);
}

bool Utf8ToUtf16(const char *source, GenericStringBuffer<UTF16<>> &target)
{
	bool success = true;
	GenericStringStream<UTF8<>> sourceStream(source);
	while (sourceStream.Peek() != '\0')
		if (!rapidjson::Transcoder<UTF8<>, UTF16<>>::Transcode(sourceStream, target))
		{
			success = true;
			break;
		}
	return success;
}

bool LoadSettings()
{
	bool ret = false;
	FILE *file;
	if (_wfopen_s(&file, L"TGFont.json", L"rb") == 0)
	{
		do {
			char readBuffer[512];
			FileReadStream is(file, readBuffer, sizeof(readBuffer));
			EncodedInputStream<UTF8<>, FileReadStream> eis(is);

			GenericDocument<UTF16<>> dom;

			if (dom.ParseStream<0, UTF8<>>(eis).HasParseError() || !dom.IsObject())
				break;

			auto member = dom.FindMember(L"fonts");
			if (member != dom.MemberEnd() && member->value.IsArray())
			{
				for (auto it = member->value.Begin(); it != member->value.End(); ++it)
				{
					if (it->IsObject())
					{
						auto find = it->FindMember(L"find");
						auto replace = it->FindMember(L"replace");
						auto size = it->FindMember(L"size");
						if (find != it->MemberEnd() && replace != it->MemberEnd() && find->value.IsString() && replace->value.IsString())
						{
							bool overrideSize = size != it->MemberEnd() && size->value.IsInt();
							size_t _size = overrideSize ? size->value.GetInt() : 0;
							std::wstring _find, _replace;

							_find = std::wstring(find->value.GetString(), find->value.GetStringLength());

							_replace = std::wstring(replace->value.GetString(), replace->value.GetStringLength());

							font fontInfo = { _replace, overrideSize, _size };
							fontsMap[_find] = fontInfo;
						}
					}
				}
			}
			ret = true;
		} while (0);
		fclose(file);
	}
	return ret;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
	if (ul_reason_for_call == DLL_PROCESS_ATTACH)
	{
		DisableThreadLibraryCalls(hModule);

#if _DEBUG
		MessageBox(0, L"DLL_PROCESS_ATTACH", L"", 0);
#endif

		if (!LoadDLL())
			return FALSE;

		if (!LoadSettings())
			return FALSE;

		size_t pfnCreateFontIndirectW = (size_t)GetProcAddress(GetModuleHandle(L"gdi32.dll"), "CreateFontIndirectW");
		if (pfnCreateFontIndirectW)
		{
			DWORD oldProtect;
			if (VirtualProtect((LPVOID)pfnCreateFontIndirectW, 5, PAGE_EXECUTE_READWRITE, &oldProtect))
			{
				jmp *hook = (jmp *)pfnCreateFontIndirectW;
				hook->opcode = 0xE9; // jmp
				hook->address = (size_t)MyCreateFontIndirectW - (size_t)pfnCreateFontIndirectW - 5;
				origAddr = (fnCreateFontIndirectW)(pfnCreateFontIndirectW + 5);
				VirtualProtect((LPVOID)pfnCreateFontIndirectW, 5, oldProtect, &oldProtect);
			}
		}
	}
	return TRUE;
}
