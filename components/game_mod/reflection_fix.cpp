#include "stdafx.h"

#include <stdlib.h>
#include <stdio.h>
#include <shellapi.h>
#include <time.h>

char	g_mapName[256];
bool	g_reflectionsUpdated = false;
char	g_ffDir[MAX_PATH] = "\0";
int		g_probeCount = 0;

BOOL ReflectionsWereUpdated()
{
	return g_reflectionsUpdated;
}

BOOL IsReflectionMode()
{
	static bool hasChecked = false;
	static bool isReflectionMode = false;

	if(!hasChecked)
	{
		int argc = 0;
		LPWSTR* argv = CommandLineToArgvW(GetCommandLine(), &argc);

		int flags = 0;
		for(int i = 0; i < argc-1; i++)
		{
			if(wcscmp(argv[i], L"+devmap") == 0 && flags ^ 1)
			{
				size_t convCount = 0;
				wcstombs_s(&convCount, g_mapName, argv[i+1],256);

				flags |= 1;
			}
			else if(wcscmp(argv[i], L"r_reflectionProbeGenerate") == 0 && wcscmp(argv[i+1], L"0") != 0 && flags ^ 2)
			{
				isReflectionMode = true;
				flags |= 2;
			}
			else if(flags == 3)
			{
				return isReflectionMode;
			}
		}
	}
	return isReflectionMode;
}

BOOL IsInjectionMode()
{
	static bool hasChecked = false;
	static bool isInjectionMode = false;

	if (!hasChecked)
	{
		int argc = 0;
		LPWSTR* argv = CommandLineToArgvW(GetCommandLine(), &argc);

		for (int i = 0; i < argc - 2; i++)
		{
			if (wcscmp(argv[i], L"+set") == 0 && wcscmp(argv[i + 1], L"r_reflectionProbeInjectImages") == 0)
			{
				if (wcscmp(argv[i + 2], L"1") == 0)
				{
					isInjectionMode = true;
					return isInjectionMode;
				}
			}
		}
	}
	return isInjectionMode;
}

BOOL ReflectionMod_Init()
{
	if(!IsReflectionMode())
		return FALSE;

	//
	// Always force the cursor to be shown
	//
	//PatchMemory(0x00683C50, (PBYTE)"\xC3", 1); // sub_683C50
	//PatchMemory(0x00867927, (PBYTE)"\xEB", 1); // jz short loc_86793E
	//PatchMemory(0x006EB2CC, (PBYTE)"\xEB", 1); // jz short loc_6EB2DE

	//
	// Enable AutoExit Upon Completing Reflections
	//
	PatchMemory(0x006CF373, (PBYTE)"\x90\x90", 2);
	PatchMemory(0x006CF379, (PBYTE)"\x90\x90", 2);
	PatchMemory(0x006CF382, (PBYTE)"\x90\x90", 2);
	PatchMemory(0x006CF388, (PBYTE)"\x90\x90", 2);

	Detours::X86::DetourFunction((PBYTE)0x006CF150, (PBYTE)&hk_R_GenerateReflectionRawDataAll);
	Detours::X86::DetourFunction((PBYTE)0x006CEE30, (PBYTE)R_GenerateReflectionRawData);

	//
	// Check g_reflectionsUpdated for ReflectionExit (mov [g_reflectionsUpdate], 1)
	//
	BYTE buf[7] = {0xC6,0x05,0xFF,0xFF,0xFF,0xFF,0x01};
	bool* pTmp = &g_reflectionsUpdated;
	memcpy(buf+2,&pTmp,4);
	PatchMemory(0x006CF383, buf, 7);

	return TRUE;
}

char* formatTime(int seconds)
{
	static int index = 0;
	static char str[4][256]; //in case called by nested functions
	
	index = (index + 1) & 3;

	int hrs = seconds / 3600;
	seconds %= 3600;
	int mins = seconds / 60;
	seconds %= 60;

	if (hrs)
	{
		sprintf_s(str[index], "%d:%02d:%02d", hrs, mins, seconds);
	}
	else if (mins)
	{
		sprintf_s(str[index], "%d:%02d", mins, seconds);
	}
	else
	{
		sprintf_s(str[index], "%d seconds", seconds);
	}

	return str[index];
}

void __cdecl R_GenerateReflectionRawDataAll(DiskGfxReflectionProbe *probeRawData, int probeCount, bool *generateProbe)
{
	printf("----------------------------------------\n");
	printf("Compiling reflections...\n");

	g_probeCount = probeCount;

	time_t initTime;
	time_t cTime;
	time(&initTime);

	for (int probeIndex = 0; probeIndex < probeCount; probeIndex++)
	{
		if (generateProbe[probeIndex])
			R_GenerateReflectionRawData(&probeRawData[probeIndex]);

		time(&cTime);
		float percentComplete = (float)(probeIndex + 1) / (float)probeCount;
		float elapsedTime = (float)difftime(cTime, initTime);
		float remainingTime = elapsedTime / percentComplete - elapsedTime;

		printf("%.1f%% complete, %s done, %s remaining\r", percentComplete * 100.0f, formatTime((int)elapsedTime), formatTime((int)remainingTime));
	}

	printf("Finished in %s.\n", formatTime((int)difftime(cTime, initTime)));
	printf("----------------------------------------\n");

	g_reflectionsUpdated = true;
}

void __declspec(naked) hk_R_GenerateReflectionRawDataAll()
{
	__asm
	{
		push[esp + 8]	//generateProbe
		push[esp + 8]	//probeCount
		push eax		//probeRawData
		call R_GenerateReflectionRawDataAll
		add esp, 12
		retn
	}
}

void __cdecl R_GenerateReflectionRawData(DiskGfxReflectionProbe* probeRawData)
{
	if (!*g_ffDir)
	{
		sprintf_s(g_ffDir, "%s/%s/", Dvar_GetString("fs_basepath"), Dvar_GetString("fs_game"));
	}

	refdef_s refdef;
	char* ptr = *((char**)0x02FF5354);
	refdef_s* refsrc = (refdef_s*)(ptr + 0x8C100);
	memcpy(&refdef, refsrc, sizeof(refdef_s));

	refdef.vieworg[1] = probeRawData->origin[0];
	refdef.vieworg[2] = probeRawData->origin[1];
	refdef.yaw = probeRawData->origin[2];

	R_InitPrimaryLights(refdef.primaryLights[0].dir);

	for (int cubemapShot = 1; cubemapShot < 7; cubemapShot++)
	{
		R_BeginCubemapShot(256, 0);
		R_BeginFrame();

		GfxCmdArray* s_cmdList = *(GfxCmdArray**)0x03B370C0;
		GfxBackEndData* frontEndDataOut = *(GfxBackEndData**)0x03B3708C;
		frontEndDataOut->cmds = &s_cmdList->cmds[s_cmdList->usedTotal];
		frontEndDataOut->viewInfo[frontEndDataOut->viewInfoCount].cmds = NULL;

		R_ClearScene(0);
		FX_BeginUpdate(0);
		R_CalcCubeMapViewValues(&refdef, cubemapShot, 256);

		float* vec = (float*)0x3AC3060;
		vec[0] = refdef.vieworg[0];
		vec[1] = refdef.vieworg[1];
		vec[2] = refdef.vieworg[2];

		int* unk1 = (int*)0x3AC3058;		*unk1 = refdef.time;
		float* unk2 = (float*)0x3AC305C;	*unk2 = refdef.time * 0.001f;

		R_UpdateFrameFog(refdef.localClientNum);
		R_UpdateFrameSun();

		float zFar = R_GetFarPlaneDist();
		FxCameraUpdate fxUpdateCmd;
		memset(&fxUpdateCmd, 0, sizeof(FxCameraUpdate));
		FxCmd cmd;
		memset(&cmd, 0, sizeof(FxCmd));

		FX_GetCameraUpdateFromRefdefAndZFar(&fxUpdateCmd, &refdef, zFar);
		FX_SetNextUpdateCamera(0, &fxUpdateCmd);
		FX_FillUpdateCmd(0, &cmd);
		Sys_ResetUpdateSpotLightEffectEvent();
		Sys_AddWorkerCmdInternal((void*)0x00BA5420, &cmd, 0);
		Sys_ResetUpdateNonDependentEffectsEvent();
		Sys_AddWorkerCmdInternal((void*)0x00BA52E0, &cmd, 0);
		Sys_AddWorkerCmdInternal((void*)0x00BA5300, &cmd, 0);
		Sys_AddWorkerCmdInternal((void*)0x00BA5330, &cmd, 0);

		R_RenderScene(&refdef, 0);
		R_EndFrame();

		R_IssueRenderCommands(3);
		R_EndCubemapShot(cubemapShot);
	}

	R_CreateReflectionRawDataFromCubemapShot(probeRawData);
}

unsigned int padded(unsigned int i)
{
	return (i + 3) & 0xFFFFFFFC;
}

//
// Loads the raw reflection data directly from a *.d3dbsp file and stores it in the "dest" buffer
// Returns TRUE if successful
// Will fail if destSize does not match the length of the reflection data in the file
//
BOOL LoadBSPReflectionData(const char* bspPath, BYTE* dest, size_t destSize)
{
	FILE* h;
	if (fopen_s(&h, bspPath, "rb") != 0)
	{
		printf("ERROR: Could not open file %s\n", bspPath);
		return FALSE;
	}

	DWORD magic = 0;
	fread(&magic, 4, 1, h);
	if (magic != 'PSBI')
	{
		printf("ERROR: File %s appears to be corrupt\n", bspPath);
		fclose(h);
		return FALSE;
	}

	DWORD version = 0;
	fread(&version, 4, 1, h);
	if (version != 45)
	{
		printf("ERROR: D3DBSP %s is version %d when it should be version %d\n", bspPath, version, 45);
		fclose(h);
		return FALSE;
	}

	DWORD lumpCount = 0;
	fread(&lumpCount, 4, 1, h);
	DWORD* index = new DWORD[lumpCount * 2];
	fread(index, 8, lumpCount, h);

	DWORD offset = ftell(h);
	DWORD size = 0;

	for (DWORD i = 0; i < lumpCount*2; i+=2)
	{
		if (index[i] == 0x29)
		{
			size = index[i + 1];
			break;
		}
		else
			offset += padded(index[i + 1]);
	}

	//
	// If the size of the reflection data in the D3DBSP file does not match the size
	// of the reflection data in the fastfile, the user likely added or removed one
	// or more reflection probes from the map and then rebuilt the BSP
	//
	if (padded(size) != padded(destSize))
	{
		printf("ERROR: Fastfile probe count does not match the BSP probe count - please rebuild fastfile\n");
		fclose(h);
		return FALSE;
	}

	fseek(h, offset, SEEK_SET);
	fread(dest, 1, size, h);
	fclose(h);
	
	return TRUE;
}

//
// Locates a sequence of bytes within a larger array of bytes
// Returns a pointer to the first match, or NULL if no matches are found
//
BYTE* findByteSequence(BYTE* src, size_t srcLen, BYTE* key, size_t keyLen)
{
	unsigned int iterCount = (srcLen + 1) - keyLen;
	for (unsigned int i = 0; i < iterCount; i++)
	{
		if (memcmp(src + i, key, keyLen) == 0)
			return src + i;
	}

	return nullptr;
}

BOOL InjectReflections()
{
	if (!IsReflectionMode() || !IsInjectionMode())
		return FALSE;

	char filepath[MAX_PATH] = "\0";
	sprintf_s(filepath, MAX_PATH, "%s%s.ff", g_ffDir, g_mapName);

	FILE* h = nullptr;
	if (fopen_s(&h, filepath, "r+b") != 0)
	{
		printf("ERROR: Fastfile %s could not be found\n\n", filepath);
		return FALSE;
	}
	rewind(h);

	HMODULE zlib = LoadLibrary(L"bin/zlib1.dll");
	if (!zlib)
	{
		fclose(h);
		printf("ERROR: bin/zlib1.dll could not be found\n\n");
		return  FALSE;
	}

	typedef int __cdecl zlib_func(BYTE *dest, unsigned int* destLen, const BYTE* source, unsigned int sourceLen);
	zlib_func* compress = (zlib_func*)GetProcAddress(zlib, "compress");
	zlib_func* uncompress = (zlib_func*)GetProcAddress(zlib, "uncompress");

	if (!compress || !uncompress)
	{
		printf("ERROR: bin/zlib1.dll appears to be corrupt\n");
		FreeLibrary(zlib);
		fclose(h);
		return FALSE;
	}

	fseek(h, 0, SEEK_END);
	size_t fileSize = ftell(h);

	// Get Compressed FileSize and Allocate a Storage Buffer for Compressed Data
	size_t cSize = fileSize - 12;
	BYTE* cBuf = new BYTE[cSize | 0x8000];

	fseek(h, 12, SEEK_SET);
	fread(cBuf, 1, cSize, h);

	XFile ffInfo;
	size_t dSize = sizeof(XFile);
	uncompress((BYTE*)&ffInfo, &dSize, cBuf, 0x8000);

	BYTE* dBuf = new BYTE[ffInfo.size + 36];
	dSize = ffInfo.size + 36;
	uncompress(dBuf, &dSize, cBuf, cSize);
	delete[] cBuf;

	char bspPath[MAX_PATH] = "\0";
	sprintf_s(bspPath, "raw/maps/%s.d3dbsp", g_mapName);

	size_t diskProbesSize = g_probeCount * sizeof(DiskGfxReflectionProbe);
	DiskGfxReflectionProbe* diskProbes = new DiskGfxReflectionProbe[g_probeCount + 1]; //+1 adds extra space for disk padding

	if (!LoadBSPReflectionData(bspPath, (BYTE*)diskProbes, diskProbesSize))
	{
		fclose(h);
		FreeLibrary(zlib);
		delete[] diskProbes;
		delete[] dBuf;
		return FALSE;
	}

	BYTE* searchLoc = dBuf;
	for (int i = 0; i < g_probeCount; i++)
	{
		//
		// Generate Probe Key
		//
		char buf[MAX_PATH];
		sprintf_s(buf, "*reflection_probe%i", i + 1); //*reflection_probe0 is static and always null
		char key[32];
		sprintf_s(key, "%.*s", 20, buf);
		memcpy(&key[strlen(key) + 1], "\0\x4\0\0DXT1", 8);
		int keyLen = strlen(key) + 9; //probename + null + [0x00 04 00 00] + DXT1

		BYTE* ffProbeImage = findByteSequence(searchLoc, dSize, (BYTE*)key, keyLen); //Theres an extra DWORD after the DXT1 identifier
		if (!ffProbeImage)
		{
			printf("ERROR: Could not find reflection probe %d of %d in %s\n", i, g_probeCount, filepath);
			fclose(h);
			FreeLibrary(zlib);
			delete[] diskProbes;
			delete[] dBuf;
			return FALSE;
		}

		ffProbeImage += keyLen;
		if (*(DWORD*)ffProbeImage != 262224)
		{
			printf("ERROR: Image for reflection probe %d has incorrect size\n", i);
			fclose(h);
			FreeLibrary(zlib);
			delete[] diskProbes;
			delete[] dBuf;
			return FALSE;
		}

		ZoneCubemapFace256* zoneCubemap = (ZoneCubemapFace256*)(ffProbeImage + 4); //Ignore Extra DWORD after DXT1 Identifier
		for (int f = 0; f < 6; f++)
		{
			DiskGfxCubemap256* diskCubemap = (DiskGfxCubemap256*)diskProbes[i].pixels;

			memcpy(zoneCubemap[f].mip256, &diskCubemap->mip256[f], 32768);

			memcpy(zoneCubemap[f].mip128, diskCubemap->mips[f].mip128, 8192);
			memcpy(zoneCubemap[f].mip64, diskCubemap->mips[f].mip64, 2048);
			memcpy(zoneCubemap[f].mip32, diskCubemap->mips[f].mip32, 512);
			memcpy(zoneCubemap[f].mip16, diskCubemap->mips[f].mip16, 128);
			memcpy(zoneCubemap[f].mip8, diskCubemap->mips[f].mip8, 32);
			memcpy(zoneCubemap[f].mip4, diskCubemap->mips[f].mip4, 8);
			memcpy(zoneCubemap[f].mip2, diskCubemap->mips[f].mip2, 8);
			memcpy(zoneCubemap[f].mip1, diskCubemap->mips[f].mip1, 8);
		}
	}

	delete[] diskProbes;

	cSize = dSize;
	cBuf = new BYTE[cSize];
	compress(cBuf, &cSize, dBuf, dSize);
	delete[] dBuf;

	fseek(h, 12, SEEK_SET);
	fwrite(cBuf, 1, cSize, h);
	fclose(h);

	delete[] cBuf;
	FreeLibrary(zlib);

	return TRUE;
}