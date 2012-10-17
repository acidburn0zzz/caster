#define DEBUG_LEVEL IMAGE_DEBUG_LEVEL
#include "../AsyncLib/AsyncLib.hpp"
#include "../AsyncLib/Log.hpp"
#include "../AsyncLib/ForEach.hpp"
#include "../AsyncLib/Common.hpp"
#include "../HashLib/Hash.hpp"
#include "../CompressLib/Compress.hpp"
#include "Image.hpp"
#ifdef _WIN32
#include <direct.h>
#else
int mkdir(const char* fmt) {
	return mkdir(fmt, 0711);
}
#endif

#define IMAGEDB_CHUNK_SIZE (32*1024*1024)		//32MB

ImageDesc::ImageDesc() {
}

ImageDesc::~ImageDesc() {
}

DeviceDesc ImageDesc::findDevice(const string& name) {
	try {
			sqlite3x::sqlite3_command cmd(m_database, "SELECT Id FROM Device WHERE Name=? LIMIT 1");
			cmd.bind(1, name);
			return DeviceDesc(*this, cmd.executeint());
	}
	catch(sqlite3x::database_error&) {
		return DeviceDesc(*this, 0);
	}
}

BlockDesc ImageDesc::findBlock(Hash hash) {
	try {
		sqlite3x::sqlite3_command cmd(m_database, "SELECT Id FROM Block WHERE Hash=? LIMIT 1");
		cmd.bind(1, &hash, sizeof(hash));
		return BlockDesc(*this, cmd.executeint());
	}
	catch(sqlite3x::database_error& err) {
		return BlockDesc(*this, 0);
	}
}
	
unsigned ImageDesc::blockCount() {
	return sqlite3_command(m_database, "SELECT COUNT(*) FROM Block").executeint();
}
	
unsigned ImageDesc::blockList(vector<BlockDesc>& blockList) {
	sqlite3_command cmd(m_database, "SELECT Id FROM Block");
	sqlite3_reader reader = cmd.executereader();

	blockList.resize(0, BlockDesc(*this, 0));
	blockList.reserve(blockCount());

	while(reader.read())
		blockList.push_back(BlockDesc(*this, reader.getint(0)));

	return blockList.size();
}

BlockDesc ImageDesc::addBlock(const void* data, unsigned dataSize, unsigned realSize, const Hash& dataHash) {
	// Znajdz stary blok
	BlockDesc desc = findBlock(dataHash);
	if(desc) return desc;

	sqlite3x::sqlite3_transaction trans(m_database);

	sqlite3x::sqlite3_command cmd(m_database, "INSERT INTO Block (RealSize, DataSize, Hash, Data) VALUES(?,?,?,?)");
	cmd.bind(1, (int)realSize);
	cmd.bind(2, (int)dataSize);
	cmd.bind(3, &dataHash, sizeof(dataHash));
#ifdef USE_DISK_FILE
	cmd.bind(4);
#else
	cmd.bind(4, data, dataSize);
#endif
	cmd.executenonquery();
	desc = BlockDesc(*this, m_database.insertid());

#ifdef USE_DISK_FILE
	string fileName = blockFileName(desc.id());
	string::size_type offset = fileName.find_last_of('/');

	if(offset != string::npos)
		mkdir(fileName.substr(0, offset).c_str());

	if(!writeFile(fileName.c_str(), data, dataSize)) {
		throw runtime_error(va("failed to write block : %s", fileName.c_str()));
	}
#endif
	trans.commit();

	return desc;
}

#ifdef USE_DISK_FILE
string ImageDesc::blockFileName(unsigned id) const {
	return va("%s/%08x/%08x.bin", name().c_str(), id&0xFF00FF, id);
}
#endif

BlockDesc ImageDesc::addBlock(const void* realData, unsigned realSize) {
	Hash dataHash = Hash::calculateHash(realData, realSize);
	BlockDesc desc = findBlock(dataHash);
	if(desc) return desc;

	string data = Compressor::compress(realData, realSize, CmZlib);
	return addBlock(data.c_str(), data.size(), realSize, dataHash);
}

DeviceDesc ImageDesc::addDevice(const string& name, const DeviceBlockOffsetList& offsetList) {
	sqlite3x::sqlite3_transaction trans(m_database);

	// find device
	DeviceDesc desc = findDevice(name);
	if(!desc) {
		sqlite3_command cmd(m_database, "INSERT INTO Device (Name) VALUES (?)");
		cmd.bind(1, name);
		cmd.executenonquery();
		desc.m_id = m_database.insertid();
	}

	// remove obsolete blocks
	sqlite3_command(m_database, "DELETE FROM BlockOffset WHERE DeviceBlockId IN (SELECT Id FROM DeviceBlock WHERE DeviceId='%i')", desc.id()).executenonquery();

	sqlite3_command db(m_database, "INSERT OR IGNORE INTO DeviceBlock (DeviceId, BlockId) VALUES (?,?)");
	db.bind(1, (int)desc.id());
	
	sqlite3_command dbq(m_database, "SELECT Id FROM DeviceBlock WHERE DeviceId=? AND BlockId=?");
	dbq.bind(1, (int)desc.id());

	sqlite3_command bo(m_database, "INSERT INTO BlockOffset (DeviceBlockId, Offset) VALUES (?,?)");

	// insert blocks
	cFurEach(DeviceBlockOffsetList, blockOffset, offsetList) {
		db.bind(2, (int)blockOffset->first);
		db.executenonquery();
		long long id = m_database.insertid();

		if(id == 0) {
			dbq.bind(2, (int)blockOffset->first);
			id = dbq.executeint64();
		}

		bo.bind(1, id);
		cFurEach(BlockOffsetList, offset, blockOffset->second) {
			bo.bind(2, (long long)*offset);
			bo.executenonquery();
		}
	}

	// remove empty device blocks
	sqlite3_command(m_database, "DELETE FROM DeviceBlock WHERE DeviceId='%i' AND Id NOT IN (SELECT DISTINCT DeviceBlockId FROM BlockOffset)", desc.id()).executenonquery();

	sqlite3_command(m_database, "UPDATE Device SET ModifiedTime=CURRENT_TIMESTAMP WHERE Id='%i'", desc.id());
	trans.commit();

	return desc;
}

void ImageDesc::addImage(const string& fileName, const string& deviceId, unsigned blockSize) {
	debugp("image", "adding %s to %s as %s...", fileName.c_str(), m_name.c_str(), deviceId.c_str());

 	//if(!checkBlockSize(blockSize))
	//	throw std::invalid_argument("block");

	FILE* dataFile = fopen64(fileName.c_str(), "rb");
	if(!dataFile)
		throw std::runtime_error("addImage: couldn't open image");

	try {
		DeviceBlockOffsetList offsetList;

		// Zarezerwuj miejsce na dane
		std::string data;
		data.resize(blockSize);

		// Wczytaj wszystkie dane
		while(!feof(dataFile)) {	
			// Wypelnij informacje o blokze
			long long offset = ftello64(dataFile);

			// Wczytaj dane
			unsigned dataSize = fread(&data[0], 1, blockSize, dataFile);
			if(!dataSize)
				continue;

			// Dodaj blok
			BlockDesc desc = addBlock(&data[0], dataSize);
			offsetList[desc.id()].push_back(offset);
		}

		addDevice(deviceId, offsetList);

		// Zamknij plik
		fclose(dataFile);
	}
	catch(exception&) {
		fclose(dataFile);
		throw;
	}
}

auto_ptr<ImageDesc> ImageDesc::newImage(const string& name) {
	auto_ptr<ImageDesc> self(new ImageDesc());
	
	unlink((name + ".db").c_str());

	debugo("image", self.get(), "new... [name=%s]", name.c_str());
	self->m_database.replace((name + ".db").c_str());
	self->m_database.setChunkSize(IMAGEDB_CHUNK_SIZE);
	self->m_name = name;

	sqlite3x::sqlite3_transaction tran(self->m_database);
	self->m_database.executenonquery("DROP TABLE IF EXISTS [Block]");
	self->m_database.executenonquery("CREATE TABLE [Block] ( \
		[Id] INTEGER  NOT NULL PRIMARY KEY AUTOINCREMENT, \
		[DataSize] INTEGER DEFAULT '0' NOT NULL, \
		[RealSize] INTEGER DEFAULT '0' NOT NULL, \
		[Hash] BLOB(16) UNIQUE NOT NULL, \
		[Data] BLOB NULL \
		)");

	self->m_database.executenonquery("DROP TABLE IF EXISTS [BlockOffset]");
	self->m_database.executenonquery("CREATE TABLE [BlockOffset] ( \
		[DeviceBlockId] INTEGER  NOT NULL, \
		[Offset] BIGINT  NOT NULL \
		)");

	self->m_database.executenonquery("DROP TABLE IF EXISTS [Device]");
	self->m_database.executenonquery("CREATE TABLE [Device] ( \
		[Id] INTEGER  NOT NULL PRIMARY KEY AUTOINCREMENT, \
		[Name] VARCHAR(255)  UNIQUE NOT NULL, \
		[CreateTime] TIMESTAMP DEFAULT CURRENT_TIMESTAMP NOT NULL, \
		[ModifiedTime] TIMESTAMP DEFAULT CURRENT_TIMESTAMP NOT NULL \
		)");

	self->m_database.executenonquery("DROP TABLE IF EXISTS [DeviceBlock]");
	self->m_database.executenonquery("CREATE TABLE [DeviceBlock] ( \
		[Id] INTEGER  NOT NULL PRIMARY KEY AUTOINCREMENT, \
		[DeviceId] INTEGER  NOT NULL, \
		[BlockId] INTEGER  NOT NULL \
		)");
	
	self->m_database.executenonquery("DROP INDEX IF EXISTS [IDX_DEVICEBLOCK_DEVICEID]");
	self->m_database.executenonquery("CREATE INDEX [IDX_DEVICEBLOCK_DEVICEID] ON [DeviceBlock]( \
																	 [DeviceId]  ASC \
																	 )");

	self->m_database.executenonquery("DROP INDEX IF EXISTS [IDX_BLOCKOFFSET]");
	self->m_database.executenonquery("CREATE INDEX [IDX_BLOCKOFFSET] ON [BlockOffset]( \
																	 [DeviceBlockId]  ASC \
																	 )");

	self->m_database.executenonquery("DROP INDEX IF EXISTS [IDX_DEVICEBLOCK_DEVICEID]");
	self->m_database.executenonquery("CREATE UNIQUE INDEX [IDX_DEVICEBLOCK_DEVICEID] ON [DeviceBlock]( \
		[DeviceId]  ASC, \
		[BlockId]  ASC \
		)");
	tran.commit();
	self->m_name = name;

#ifdef USE_DISK_FILE
	mkdir(name.c_str());
#endif // USE_DISK_FILE
	return self;
}

auto_ptr<ImageDesc> ImageDesc::loadImageFromFile(const string& name) {
	auto_ptr<ImageDesc> self(new ImageDesc());
	debugo("image", self.get(), "loading... [name=%s]", name.c_str());
	self->m_database.open((name + ".db").c_str());
	self->m_database.setChunkSize(IMAGEDB_CHUNK_SIZE);
	self->m_name = name;

#ifdef USE_DISK_FILE
	mkdir(name.c_str());
#endif // USE_DISK_FILE
	return self;
}

void ImageDesc::removeUnusedBlocks() {
	// delete unused device blocks
	m_database.executenonquery("DELETE FROM DeviceBlock WHERE DeviceId NOT IN (SELECT Id FROM Device);");
	m_database.executenonquery("DELETE FROM BlockOffset WHERE DeviceBlockId NOT IN (SELECT Id FROM DeviceBlock);");
	
	// delete block physically
	vector<unsigned> blockList;
	if(1) {
		sqlite3_command cmd(m_database, "SELECT Id FROM Block WHERE Id NOT IN (SELECT DISTINCT BlockId From DeviceBlock)");
		sqlite3_reader r = cmd.executereader();
		blockList.reserve(1000);
		while(r.read())	{
			blockList.push_back(r.getint64(0));
		}
	}

	sqlite3_command deleteBlock(m_database, "DELETE FROM Block WHERE Id=?");

	for(int i = 0; i < blockList.size(); ++i)	{
		sqlite3x::sqlite3_transaction trans(m_database);
		deleteBlock.bind(1, (long long)blockList[i]);
		deleteBlock.executenonquery();
		string blockName = blockFileName(blockList[i]);
		fprintf(stderr, "* Deleting '%s'...\n", blockName.c_str());
		unlink(blockName.c_str());
		trans.commit();
	}
}

void ImageDesc::stats(ImageStats& stats) {
	stats = ImageStats();
	stats.BlockCount = m_database.executeint64("SELECT COUNT(*) FROM Block");
	stats.CompressedBlockSize = m_database.executeint64("SELECT SUM(DataSize) FROM Block");
	stats.RealBlockSize = m_database.executeint64("SELECT SUM(RealSize) FROM Block");
	stats.UnusedBlockCount = m_database.executeint64("SELECT COUNT(*) FROM Block WHERE Id NOT IN (SELECT DISTINCT BlockId FROM DeviceBlock)");
	stats.UnusedCompressedBlockSize = m_database.executeint64("SELECT SUM(DataSize) FROM Block WHERE Id NOT IN (SELECT DISTINCT BlockId FROM DeviceBlock)");
	stats.UnusedRealBlockSize = m_database.executeint64("SELECT SUM(RealSize) FROM Block WHERE Id NOT IN (SELECT DISTINCT BlockId FROM DeviceBlock)");
	stats.DeviceCount = m_database.executeint64("SELECT COUNT(*) FROM Device");
	stats.DeviceBlockCount = m_database.executeint64("SELECT COUNT(*) FROM DeviceBlock");
	stats.AvgBlockUsage = m_database.executedouble("SELECT AVG((SELECT COUNT(*) FROM DeviceBlock db WHERE db.BlockId=b.Id)) FROM Block b");
	stats.AvgCompressedSizeUsage = m_database.executedouble("SELECT AVG(RealSize*(SELECT COUNT(*) FROM DeviceBlock db WHERE db.BlockId=b.Id)) FROM Block b");
	stats.AvgRealSizeUsage = m_database.executedouble("SELECT AVG(DataSize*(SELECT COUNT(*) FROM DeviceBlock db WHERE db.BlockId=b.Id)) FROM Block b");

	sqlite3_command cmd(m_database, "SELECT Id, Name FROM Device");
	sqlite3_reader r = cmd.executereader();
	while(r.read()) {
		DeviceStats deviceStats;
		deviceStats.Id = r.getint(0);
		deviceStats.Name = r.getstring(1);
		deviceStats.BlockCount = m_database.executeint64(va("SELECT COUNT(*) FROM Block WHERE Id IN (SELECT DISTINCT BlockId FROM DeviceBlock WHERE DeviceId=%u)", deviceStats.Id));
		deviceStats.CompressedSize = m_database.executeint64(va("SELECT SUM(DataSize) FROM Block WHERE Id IN (SELECT DISTINCT BlockId FROM DeviceBlock WHERE DeviceId=%u)", deviceStats.Id));
		deviceStats.RealSize = m_database.executeint64(va("SELECT SUM(RealSize) FROM Block WHERE Id IN (SELECT DISTINCT BlockId FROM DeviceBlock WHERE DeviceId=%u)", deviceStats.Id));
		deviceStats.AvgReuseCount = m_database.executedouble(va("SELECT AVG((SELECT COUNT(*) FROM DeviceBlock db WHERE db.BlockId=b.Id)) FROM Block b WHERE b.Id IN (SELECT DISTINCT BlockId FROM DeviceBlock WHERE DeviceId=%u)", deviceStats.Id));
		deviceStats.OffsetCount = m_database.executeint64(va("SELECT COUNT(*) FROM BlockOffset bo JOIN DeviceBlock db ON bo.DeviceBlockId=db.Id AND db.DeviceId=%u", deviceStats.Id));
		stats.DeviceList.push_back(deviceStats);
	}
}
