#pragma once

typedef vector<bool> FragList;

struct ClientBlockDesc {
	unsigned Id;
	unsigned DataSize, RealSize;
	::Hash Hash;
	vector<long long> Ranges;

	// Constructor
	ClientBlockDesc() {
		Id = 0;
		DataSize = RealSize = 0;
	}
};

struct ClientBlockData : ClientBlockDesc { 
	byte Data[1];

	static ClientBlockData* alloc(const ClientBlockDesc& desc, const void* data, unsigned size) {
		assert(desc.DataSize == size);
		ClientBlockData* self = new(sizeof(ClientBlockData) + size) ClientBlockData(desc);
		memcpy(self->Data, data, size);
		return self;
	}

	ClientBlockData(const ClientBlockDesc& desc) : ClientBlockDesc(desc) {
	}
};

typedef map<unsigned, ClientBlockDesc> ClientBlockList;

struct ClientBlockCloneDesc {
	//! Podstawowe przesuniecie
	long long Offset;

	//! Rozmiar danych
	unsigned Size;

	//! Obszary do zapisu
	unsigned RangeCount;
	long long Ranges[1];
	
	// Constructor
	ClientBlockCloneDesc() {
		Offset = 0;
		Size = 0;
		RangeCount = 0;
	}

	static ClientBlockCloneDesc* alloc(long long offset, unsigned size, unsigned rangeCount, long long* ranges);
};

typedef vector<ClientBlockCloneDesc*> ClientBlockCloneList;

struct CasterClientArgs
{
	//! Nazwa pliku
	string FileName;

	//! Kompresja wysylanych danych
	bool Compress;

	//! Port serwera
	unsigned Port;

	//! Adres serwera
	string Address;

	//! Nazwa urzadzenia
	string DeviceName;

	//! Gdzie przypiac gniazdo
	string BindAddress;
	
	//! Uaktualnia obraz, sciagajac tylko zmiany
	bool Update;
};

class CasterClient;

class CasterCastClient : public UdpCastClient
{
	CasterClient& Client;
	string Data;

public:
	CasterCastClient(CasterClient& client);

protected:
	void onJoinTimeout();
	void onJoin();
	bool onConsumeData(const string& data);
	void onLeave();
	void onAliveTimeout();
};

typedef deque<ClientBlockData*> ClientBlockFinishList;

class CasterClient : public PacketSock, public CasterClientArgs
{
	enum State {
		Idle,
		Image,
		Ready,
		Commit
	};

	// Fields
private:
	//! Aktualny stan
	State m_state;

	//! Plik docelowy
	FILE* m_file;

	//! Watek workera
	Thread m_worker;
	mutable Mutex m_mutex;
	mutable Cond m_workerCond;

	auto_ptr<CasterCastClient> m_castClient;

	// Getting Fields
private:
	//! Nazwa obrazu
	unsigned m_version;
	string m_imageName;
	unsigned m_maddress;
	//! Lista blokow do pobrania
	ClientBlockList m_blockList;
	unsigned m_blockCount;

	//! Lista blokow do zapisu
	ClientBlockCloneList m_blockCloneList;

	//! Lista bloków do zapisu
	ClientBlockFinishList m_blockFinishList;

	// Constructor
public:
	CasterClient(const CasterClientArgs& args);

	// Destructor
public:
	~CasterClient();

	// Helpers
private:
	void finishImage();
	void removeExistingBlocks();

	// Handlers
private:
	void onImagePacket(const CasterPacketImage& image);
	void onBlockDataPacket(const CasterPacketBlockData& block);
	void onBlockPacket(const CasterPacketBlock& block);
	void onReadyPacket();
	void onFinishedPacket(const CasterPacketFinished& finished);
	void onPacket(const void* data, unsigned size);
	void onPingPacket(const CasterPacketPing& ping);

	double onShowProgress(unsigned id);
	void* onWorkerThread(void*);

	bool waitForRead() const;

	// Getting Methods
public:
	void sendGetImage(const string& deviceName);
	void sendGetData(const vector<unsigned>& blockList);
	bool sendGetRemainingData();
	void sendGetBlockData(unsigned id, const FragList& frags);

	friend class CasterCastClient;
};
