#define DEBUG_LEVEL IMAGE_DEBUG_LEVEL
#include "../AsyncLib/AsyncLib.hpp"
#include "../AsyncLib/Log.hpp"
#include "../AsyncLib/ForEach.hpp"
#include "../HashLib/Hash.hpp"
#include "../CompressLib/Compress.hpp"
#include "Image.hpp"

DeviceDesc::DeviceDesc(ImageDesc& desc, unsigned id) : m_image(desc), m_id(id) {
}

unsigned DeviceDesc::createdTime() const {
	return sqlite3_command(m_image.m_database, "SELECT CreateTime FROM Device WHERE Id='%i'", m_id).executeint();
}

unsigned DeviceDesc::modifiedTime() const {
	return sqlite3_command(m_image.m_database, "SELECT ModifiedTime FROM Device WHERE Id='%i'", m_id).executeint();
}

unsigned DeviceDesc::blockCount() const {
	return sqlite3_command(m_image.m_database, "SELECT COUNT(*) FROM DeviceBlock WHERE DeviceId='%i'", m_id).executeint();
}

unsigned DeviceDesc::blockOffsetCount(unsigned blockId) const {
	return sqlite3_command(m_image.m_database, "SELECT COUNT(*) FROM DeviceBlock WHERE BlockId='%i' AND DeviceId='%i'", blockId, m_id).executeint();
}

unsigned DeviceDesc::blockList(vector<unsigned>& blockList) const {
	sqlite3x::sqlite3_command cmd(m_image.m_database, "SELECT BlockId FROM DeviceBlock WHERE DeviceId='%i'", m_id);
	sqlite3x::sqlite3_reader reader = cmd.executereader();

	blockList.resize(0);
	blockList.reserve(blockCount());
	while(reader.read())
		blockList.push_back(reader.getint(0));
	return blockList.size();
}

unsigned DeviceDesc::blockList(vector<BlockInfo>& blockList) const {
	sqlite3x::sqlite3_command cmd(m_image.m_database, "SELECT BlockId,DataSize,RealSize,Hash FROM DeviceBlock JOIN Block ON Block.Id=BlockId WHERE DeviceId='%i'", m_id);
	sqlite3x::sqlite3_reader reader = cmd.executereader();

	BlockInfo info;

	blockList.clear();
	blockList.reserve(3000);

	while(reader.read()) {
		info.Id = reader.getint(0);
		info.DataSize = reader.getint(1);
		info.RealSize = reader.getint(2);
		info.Hash = *(Hash*)reader.getblob(3).c_str();
		blockList.push_back(info);
	}
	return blockList.size();
}

unsigned DeviceDesc::blockOffsetList(unsigned blockId, vector<long long>& offsetList) const {
	sqlite3x::sqlite3_command cmd(m_image.m_database, "SELECT Offset FROM DeviceBlock JOIN BlockOffset ON DeviceBlockId=Id WHERE BlockId='%i' AND DeviceId='%i'", blockId, m_id);
	sqlite3x::sqlite3_reader reader = cmd.executereader();

	offsetList.resize(0);
	offsetList.reserve(blockOffsetCount(blockId));
	while(reader.read())
		offsetList.push_back(reader.getint64(0));
	return offsetList.size();
}

unsigned DeviceDesc::blockOffsetList(DeviceBlockOffsetList& offsetList) const {
	vector<unsigned> in;
	blockList(in);

	offsetList.clear();
	cFurEach(vector<unsigned>, id, in) {
		blockOffsetList(*id, offsetList[*id]);
	}
	return offsetList.size();
}

void DeviceDesc::remove() {
	sqlite3x::sqlite3_transaction trans(m_image.m_database);
	sqlite3_command(m_image.m_database, "DELETE FROM BlockOffset WHERE DeviceBlockId IN (SELECT Id FROM DeviceBlock WHERE DeviceId='%i')", m_id);
	sqlite3_command(m_image.m_database, "DELETE FROM DeviceBlock WHERE db.DeviceId='%i'", m_id);
	sqlite3_command(m_image.m_database, "DELETE FROM Device WHERE Id='%i'", m_id);
	m_id = 0;
	trans.commit();
}
