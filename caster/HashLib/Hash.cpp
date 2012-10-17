#include "../AsyncLib/AsyncLib.hpp"
#include "../AsyncLib/Log.hpp"
#include "../AsyncLib/ForEach.hpp"
#include "Hash.hpp"
#include "md5.h"

unsigned Hash::crc32(const void* data, unsigned size) {
	static unsigned crcTab[256];
	static unsigned crcTabDone = 0;
	unsigned i, j;
	
	if(!crcTabDone) {
		for(i = 0; i < 256; ++i) {
			j = i;
			j = (j>>1) ^ ((j&1) ? 0xedb88320 : 0); 
			j = (j>>1) ^ ((j&1) ? 0xedb88320 : 0); 
			j = (j>>1) ^ ((j&1) ? 0xedb88320 : 0); 
			j = (j>>1) ^ ((j&1) ? 0xedb88320 : 0); 
			j = (j>>1) ^ ((j&1) ? 0xedb88320 : 0); 
			j = (j>>1) ^ ((j&1) ? 0xedb88320 : 0); 
			j = (j>>1) ^ ((j&1) ? 0xedb88320 : 0); 
			j = (j>>1) ^ ((j&1) ? 0xedb88320 : 0); 
			crcTab[i] = j;
		}
		crcTabDone = 1;
	}

	unsigned crc32 = 0;
	while(size-- > 0)
		crc32 = (crc32 >> 8) ^ crcTab[(unsigned char)(crc32 ^ *((byte*&)data)++)];
	return crc32;
}

Hash Hash::calculateHash(const void* data, unsigned size) {
	Hash hash(0);

	MD5_CTX context;
	memset(&context, 0, sizeof(context));
	MD5Init(&context);
	MD5Update(&context, (byte*)data, size);
	MD5Final(hash.Digest, &context);
	return hash;
}

bool Hash::operator < (const Hash& hash) const {
	return Data[0] < hash.Data[0] || 
		Data[0] == hash.Data[0] && (Data[1] < hash.Data[1] || 
		Data[1] == hash.Data[1] && (Data[2] < hash.Data[2] || 
		Data[2] == hash.Data[2] && (Data[3] < hash.Data[3])));
}

bool Hash::operator == (const Hash& hash) const {
	return Data[0] == hash.Data[0] && Data[1] == hash.Data[1] && Data[2] == hash.Data[2] && Data[3] == hash.Data[3];
}

HashList::HashList(unsigned buckets) : m_buckets(buckets) {
	assert(buckets > 1);
}

HashList::~HashList() {
	clear();
}

inline HashList::HashEntry*& HashList::getHashBucket(const Hash &hash) {
	return m_buckets[hash.Data[1] % m_buckets.size()];
}

HashList::HashEntry** HashList::findHashEntry(const Hash &hash) {
	HashEntry** bucket = &getHashBucket(hash);
	while(*bucket) {
		if(hash == **bucket)
			return bucket;
		bucket = &(*bucket)->Next;
	}
	return NULL;
}

bool HashList::addHash(const Hash &hash, unsigned id) {
	HashEntry** bucket = &getHashBucket(hash);
	while(*bucket) {
		if(hash == **bucket) {
			(*bucket)->Id = id;
			return false;
		}
		bucket = &(*bucket)->Next;
	}
	*bucket = new HashEntry(hash, id);
	return true;	
}

unsigned HashList::findHash(const Hash &hash) const {
	HashEntry** bucket = const_cast<HashList*>(this)->findHashEntry(hash);
	if(bucket)
		return (*bucket)->Id;
	return 0;
}

bool HashList::removeHash(const Hash &hash) {
	HashEntry** bucket = findHashEntry(hash);
	if(bucket) {
		HashEntry* oldBucket = *bucket;
		*bucket = (*bucket)->Next;
		delete oldBucket;
		return true;
	}
	return false;
}

void HashList::clear() {
	FurEach(vector<HashEntry*>, itor, m_buckets) {
		while(*itor) {
			HashEntry* bucket = *itor;
			*itor = bucket->Next;
			delete bucket;
		}
	}
}
