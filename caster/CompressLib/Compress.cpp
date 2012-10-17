#include "../AsyncLib/AsyncLib.hpp"
#include "../AsyncLib/Log.hpp"
#include "../AsyncLib/ForEach.hpp"
#include "Compress.hpp"
#include "fastlz.h"
#include "zlib.h"
#ifdef _WIN32
#pragma comment(lib, "CompressLib/zdll.lib")
#endif // _WIN32

string Compressor::compress(const void* in, unsigned inSize, CompressMethod method) {
	switch(method) {
		case CmFastLZ:
			{
				string out(inSize*110/100, 0);
				out[0] = CmFastLZ;
				int size = fastlz_compress(in, inSize, (byte*)out.c_str()+1);
				if(size == 0)
					throw runtime_error("buffer cannot be compressed using fastlz");
				out.resize(size + 1);
				return out;
			}

		case CmZlib:
			{
				string out(compressBound(inSize), 0);
				out[0] = CmZlib;
				uLongf size = out.size()-1;
				int error = compress2((Bytef*)out.c_str()+1, &size, (const Bytef*)in, inSize, 1);
				if(error != Z_OK)
					throw runtime_error("buffer cannot be compressed using zlib");
				out.resize(size+1);
				return out;
			}

		case CmNone:
			{
				string out(inSize+1, 0);
				out[0] = CmNone;
				memcpy((byte*)out.c_str()+1, in, inSize);
				return out;
			}

		default:
			throw runtime_error("not supported compression method");
	}
}

CompressMethod Compressor::method(const void* in, unsigned inSize) {
	if(inSize == 0)
		return CmUnknown;

	char* data = (char*)in;
	switch(data[0]) {
		case CmNone:
		case CmFastLZ:
		case CmZlib:
			return (CompressMethod)data[0];

		default:
			return CmUnknown;
	}
}

string Compressor::decompress(const void* in, unsigned inSize, unsigned outSize) {
	switch(method(in, inSize))
	{
	case CmFastLZ:
		{
			string out(outSize+1, 0);
			int size = fastlz_decompress((const byte*)in+1, inSize-1, (byte*)out.c_str(), out.size());
			if(size != outSize)
				throw runtime_error("invalid decompressed fastlz buffer size");
			out.resize(size);
			return out;
		}

	case CmZlib:
		{
			string out(outSize+1, 0);
			uLongf size = out.size();
			int error = uncompress((Bytef*)out.c_str(), &size, (const Bytef*)in+1, inSize-1);
			if(error != Z_OK || size != outSize)
				throw runtime_error("invalid decompressed zlib buffer size");
			out.resize(size);
			return out;
		}
		break;

	case CmNone:
		{
			if(inSize-1 != outSize)
				throw runtime_error("invalid decompressed nocompress buffer size");
			return string((const char*)in+1, inSize-1);
		}

	default:
		throw runtime_error("invalid compress header or compression method");
	}
}

string Compressor::recompress(const void* in, unsigned inSize, unsigned outSize, CompressMethod newMethod, bool forceValidate) {
	// no need to recompress anything
	CompressMethod oldMethod = method(in, inSize);
	if(oldMethod == newMethod) {
		// just validate data
		if(forceValidate)
			decompress(in, inSize, outSize);
		return string((const char*)in, inSize);
	}

	// decompress and than recompress
	string dein = decompress(in, inSize, outSize);
	return compress(dein.c_str(), dein.size(), newMethod);
}
