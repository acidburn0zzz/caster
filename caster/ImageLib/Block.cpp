#define DEBUG_LEVEL IMAGE_DEBUG_LEVEL
#include "../AsyncLib/AsyncLib.hpp"
#include "../AsyncLib/Common.hpp"
#include "../AsyncLib/Log.hpp"
#include "../AsyncLib/ForEach.hpp"
#include "../HashLib/Hash.hpp"
#include "../CompressLib/Compress.hpp"
#include "Image.hpp"

BlockDesc::BlockDesc(ImageDesc& desc, unsigned id) : m_image(desc), m_id(id) {
}

bool BlockDesc::valid() const {
	return sqlite3_command(m_image.m_database, "SELECT COUNT(*) FROM Block WHERE Id='%i'", m_id).executeint() == 1;
}

void BlockDesc::remove(bool noCheck) {
	sqlite3_command(m_image.m_database, "DELETE FROM Block WHERE Id='%i'", m_id).executenonquery();
	m_id = 0;
}

string BlockDesc::data() const {
	string data;
#ifdef USE_DISK_FILE
	if(readFile(m_image.blockFileName(m_id), data)) {
		if(data.size() != dataSize())
			throw runtime_error(va("read block is corrupt : %i", m_id));
		return data;
	}
	// Fallback to database storage
#endif // USE_DISK_FILE

	return sqlite3_command(m_image.m_database, "SELECT Data FROM Block WHERE Id='%i'", m_id).executeblob();
}

#ifdef USE_DISK_FILE
FILE* BlockDesc::dataOpen() const {
	return fopen(m_image.blockFileName(m_id).c_str(), "rb");
}
#endif

unsigned BlockDesc::dataSize() const {
	return sqlite3_command(m_image.m_database, "SELECT DataSize FROM Block WHERE Id='%i'", m_id).executeint();
}

unsigned BlockDesc::realSize() const {
	return sqlite3_command(m_image.m_database, "SELECT RealSize FROM Block WHERE Id='%i'", m_id).executeint();
}

Hash BlockDesc::hash() const {
	string hash = sqlite3_command(m_image.m_database, "SELECT Hash FROM Block WHERE Id='%i'", m_id).executeblob();
	return *(Hash*)&hash[0];
}
