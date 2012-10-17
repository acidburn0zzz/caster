#pragma once

#define IMAGE_DEBUG_LEVEL 5

#define USE_DISK_FILE

#include "../sqlite3x/sqlite3x.hpp"

using sqlite3x::sqlite3_connection;
using sqlite3x::sqlite3_command;
using sqlite3x::sqlite3_reader;

typedef vector<long long> BlockOffsetList;
typedef map<unsigned, BlockOffsetList> DeviceBlockOffsetList;

class ImageDesc;

struct BlockInfo {
	unsigned Id;
	unsigned DataSize, RealSize;
	::Hash Hash;
};

class DeviceDesc {
	ImageDesc& m_image;
	unsigned m_id;

public:
	DeviceDesc(const DeviceDesc& desc) : m_image(desc.m_image), m_id(desc.m_id) {}
	DeviceDesc(ImageDesc& desc, unsigned id);
	unsigned id() const { return m_id; }
	unsigned createdTime() const;
	unsigned modifiedTime() const;
	unsigned blockCount() const;
	unsigned blockOffsetCount(unsigned blockId) const;
	unsigned blockList(vector<unsigned>& list) const;
	unsigned blockList(vector<BlockInfo>& list) const;
	unsigned blockOffsetList(unsigned blockId, vector<long long>& offsetList) const;
	unsigned blockOffsetList(DeviceBlockOffsetList& offsetList) const;
	DeviceDesc& operator = (const DeviceDesc& desc) { m_id = desc.m_id; return *this; }
	void remove();
	operator bool () const { return m_id != 0; }
	friend class ImageDesc;
};

class BlockDesc {
	ImageDesc& m_image;
	unsigned m_id;

public:
	BlockDesc(const BlockDesc& desc) : m_image(desc.m_image), m_id(desc.m_id) {}
	BlockDesc(ImageDesc& desc, unsigned id);
	unsigned id() const { return m_id; }
	string data() const;
#ifdef USE_DISK_FILE
	FILE* dataOpen() const;
#endif // USE_DISK_FILE
	unsigned dataSize() const;
	unsigned realSize() const;
	Hash hash() const;
	void remove(bool noCheck = false);
	operator bool () const { return m_id != 0; }
	bool valid() const;
	BlockDesc& operator = (const BlockDesc& desc) { m_id = desc.m_id; return *this; }

	friend class ImageDesc;
};

struct DeviceStats {
	unsigned Id;
	string Name;
	unsigned BlockCount;
	long long CompressedSize;
	long long RealSize;
	float AvgReuseCount;
	unsigned OffsetCount;
};

struct ImageStats {
	unsigned BlockCount;
	long long CompressedBlockSize;
	long long RealBlockSize;
	unsigned UnusedBlockCount;
	long long UnusedCompressedBlockSize;
	long long UnusedRealBlockSize;

	unsigned DeviceCount;
	unsigned DeviceBlockCount;

	float AvgBlockUsage;
	float AvgCompressedSizeUsage;
	float AvgRealSizeUsage;

	vector<DeviceStats> DeviceList;
};

class ImageDesc {
	// Fields
public:
	//! Nazwa obrazu
	string m_name;

	//! Po³¹czenie do bazy danych
	sqlite3x::sqlite3_connection m_database;

	// Constructor
private:
	ImageDesc();

	// Destructor
public:
	~ImageDesc();

	// Methods
public:
	const string& name() const {
		return m_name;
	}

#ifdef USE_DISK_FILE
	string blockFileName(unsigned id) const;
#endif // USE_DISK_FILE

	//! Znajdz urzadzenie o podanej nazwie
	DeviceDesc findDevice(const string& name);

	//! Znajdz blok o podanej sumie kontrolnej
	BlockDesc findBlock(Hash hash);

	unsigned blockCount();
	unsigned blockList(vector<BlockDesc>& blockList);

	//! Zwraca numer bloka dla danego wolumenu danych
	BlockDesc addBlock(const void* data, unsigned dataSize, unsigned realSize, const Hash& dataHash);
	BlockDesc addBlock(const void* realData, unsigned realSize);

	//! Dodaje nowe urzadzenie z opisu i zapisuje zmiany na dysk
	DeviceDesc addDevice(const string& name, const DeviceBlockOffsetList& offsetList);

	//! Dodaje obraz do opisu dla podanego urzadzenia i o podanej wielkosci bloka
	void addImage(const string& fileName, const string& deviceId, unsigned blockSize);

	//! Usuwa stare bloky
	void removeUnusedBlocks();

	//! Pobiera informacje statystyczne o obrazie
	void stats(ImageStats& stats);

	// Functions
public:
	//! Tworzy nowy obraz
	static auto_ptr<ImageDesc> newImage(const string& name);

	//! £aduje obraz z pliku
	static auto_ptr<ImageDesc> loadImageFromFile(const string& name);

	friend class BlockDesc;
	friend class DeviceDesc;
};
