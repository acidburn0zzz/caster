#pragma once

#pragma pack(1)
struct Hash {
	union {
		unsigned Data[4];
		byte Digest[16];
	};

	// Constructor
	Hash() {
		Data[0] = 
		Data[1] = 
		Data[2] = 
		Data[3] = 0;
	}
	Hash(unsigned) {
	}

	// Methods
	static Hash calculateHash(const void* data, unsigned size);
	static unsigned crc32(const void* data, unsigned size);

	// Operators
	bool operator < (const Hash& hash) const;
	bool operator == (const Hash& hash) const;
	bool operator != (const Hash& hash) const {
		return !(*this == hash);
	}
};
#pragma pack()

class HashList {
	struct HashEntry : Hash {
		unsigned Id;
		HashEntry* Next;
		
		HashEntry(const Hash& hash, unsigned id) : Hash(hash) {
			Id = id;
			Next = NULL;
		}
		
		HashEntry() {
			Id = ~0;
			Next = NULL;
		}
	};
	
	vector<HashEntry*> m_buckets;
	
private:
	HashEntry*& getHashBucket(const Hash &hash);
	
	HashEntry** findHashEntry(const Hash &hash);
	
public:
	HashList(unsigned buckets);
	
	~HashList();
	
	bool addHash(const Hash &hash, unsigned id);
	
	unsigned findHash(const Hash &hash) const;
	
	bool removeHash(const Hash &hash);
	
	void clear();
};
