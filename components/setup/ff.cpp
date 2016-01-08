#include "ff.h"
#include "steam.h"
#include "arg.h"
#include "files.h"
#include <iostream>
#include <Windows.h>

typedef int __cdecl zlib_func(BYTE *dest, unsigned int* destLen, const BYTE* source, unsigned int sourceLen);
static zlib_func* compress = nullptr;
static zlib_func* uncompress = nullptr;

char* FindRawfileString(BYTE* start, BYTE* end)
{
	while (start < end - 5)
	{
		if (strncmp(".atr", (char*)start, 4) == 0 ||
			strncmp(".gsc", (char*)start, 4) == 0 ||
			strncmp(".csc", (char*)start, 4) == 0)
		{
			return (char*)start;
		}

		start++;
	}

	return nullptr;
}

char* FindRawfileStringReverseLookup(BYTE* start)
{
	for (char* strStart = (char*)start; strStart > strStart - MAX_PATH; strStart--)
	{
		if (*(DWORD*)strStart == 0xFFFFFFFF)
		{
			if (*(strStart + 4))
			{
				return strStart + 4;
			}
			
			return nullptr;
		}
	}

	return nullptr;
}

int FF_FFExtractRawfile(XAssetRawfileHeader* rawfileHeader, const char* rawfilePath)
{
	printf("Extracting file: \"%s\"...	", rawfilePath, rawfileHeader);

	char qpath[1024] = "";
	sprintf_s(qpath, "%s/%s", AppInfo_RawDir(), rawfilePath);

	//
	// If not in overwrite mode AND the file exists
	// skip it before performing decompression
	//
	if (!ARG_FLAG_OVERWRITE)
	{
		if (FILE* h = fopen(qpath, "r"))
		{
			printf("SKIPPED\n");

			fclose(h);
			return 0;
		}
	}

	if (FS_CreatePath(rawfilePath) != 0)
	{
		printf("ERROR\n");
		return 0;
	}

	//
	// Catch incorrect rawfile data to prevent massive allocations
	//
	if (rawfileHeader->uncompressedSize > 1024 * 1024 * 16)
	{
		printf("IGNORED\n");
		return 0;
	}

	BYTE* dBuf = new BYTE[rawfileHeader->uncompressedSize];
	unsigned int dSize = rawfileHeader->uncompressedSize;
	if (uncompress(dBuf, &dSize, &rawfileHeader->fileData, rawfileHeader->compressedSize) != 0)
	{
		printf("ERROR\n");
		return 0;
	}

	if (FILE* h = fopen(qpath, "wb"))
	{
		fwrite(dBuf, 1, rawfileHeader->uncompressedSize, h);
		fclose(h);

		printf("SUCCESS\n");

		delete[] dBuf;
		return 1;
	}

	delete[] dBuf;

	printf("ERROR\n");
	return 0;
}

int FF_FFExtractRawfiles(BYTE* searchData, DWORD searchSize)
{
	int extractedFileCount = 0;

	BYTE* endofBuffer = searchData + searchSize;

	BYTE* lastSearchLoc = 0;
	while (searchData < searchData + searchSize)
	{
		char* rawfileString = FindRawfileString(searchData, endofBuffer);

		if (!rawfileString)
		{
			return extractedFileCount;
		}

		char* tmpString = FindRawfileStringReverseLookup((BYTE*)rawfileString);

		if (!tmpString)
		{
			return extractedFileCount;
		}

		if ((BYTE*)tmpString < searchData || !IsCharAlphaNumericA(*tmpString))
		{
			searchData += strlen(rawfileString) + 1;
			continue;
		}

		rawfileString = tmpString;

		XAssetRawfileHeader* rawfileHeader = (XAssetRawfileHeader*)(rawfileString + strlen(rawfileString) + 1);
		if (!FF_FFExtractRawfile(rawfileHeader, rawfileString))
		{
			searchData = (BYTE*)rawfileString + strlen(rawfileString) + 1;
			continue;
		}

		extractedFileCount++;
		searchData = (BYTE*)rawfileHeader + rawfileHeader->compressedSize;
	}

	return extractedFileCount;
}

int FF_FFExtract(const char* filepath, const char* filename)
{
	printf("Extracting rawfiles from \"%s\"...\n", filename);

	FILE* h = nullptr;
	if (fopen_s(&h, filepath, "r+b") != 0)
	{
		printf("ERROR: Fastfile %s could not be found\n\n", filepath);
		return FALSE;
	}
	rewind(h);

	HMODULE zlib = LoadLibrary(L"zlib1.dll");
	if (!zlib)
	{
		fclose(h);
		printf("ERROR: zlib1.dll could not be found\n\n");
		return  FALSE;
	}

	compress = (zlib_func*)GetProcAddress(zlib, "compress");
	uncompress = (zlib_func*)GetProcAddress(zlib, "uncompress");

	if (!compress || !uncompress)
	{
		printf("ERROR: zlib1.dll appears to be corrupt\n");
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


	/*XAssetList* xal = (XAssetList*)(dBuf + 36);
	printf("XAssetList\n");
	printf("	StringList\n");
	printf("		count 0x%X %d\n", xal->stringList.count, xal->stringList.count);
	printf("		strings 0x%X\n", xal->stringList.strings);
	printf("	assetCount 0x%X %d\n", xal->assetCount, xal->assetCount);
	printf("	assets 0x%X\n", xal->assets);

	char* stringList = (char*)(dBuf + 36 + sizeof(XAssetList));
	stringList += xal->stringList.count * sizeof(char*);

	for (int i = 0; i < xal->stringList.count; i++)
	{
		//fix for fastfiles where the count doesnt match
		//the array size
		if (!stringList[1])
		{
			DWORD* tmp = (DWORD*)stringList;
			if (tmp[1] == 0xFFFFFFFF)
			{
				break;
			}
		}

		stringList += strlen(stringList) + 1;
	}
	
	XAsset* AassetList = (XAsset*)stringList;

	int assetTypeCounter[0x1D];
	memset(assetTypeCounter, 0, 128 * 4);

	XAsset* assetList = (XAsset*)stringList;
	for (int i = 0; i < xal->assetCount; i++)
	{
		assetTypeCounter[assetList->type]++;
		assetList++;
	}*/

	FF_FFExtractRawfiles((BYTE*)dBuf, ffInfo.size + 36);

	delete[] dBuf;
	return 0;
}