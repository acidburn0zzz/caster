#pragma once

enum CompressMethod
{
	CmNone = 0x40,
	CmFastLZ = 0x45,
	CmZlib = 0x46,
	CmUnknown = 0xff,
};

struct Compressor {
	static string compress(const void* in, unsigned inSize, CompressMethod method);
	static string decompress(const void* in, unsigned inSize, unsigned outSize);
	static string recompress(const void* in, unsigned inSize, unsigned outSize, CompressMethod newMethod, bool forceValidate = false);
	static CompressMethod method(const void* in, unsigned inSize);
};
