#pragma once

class CasterServer;
class CasterSession;
class CasterSessionClient;
class CasterSessionSender;

struct CasterServerArgs
{
	string ImageName;
	unsigned Port;
	string Address;
	string Maddress;
	long long Rate;
	unsigned FragSize;
};

class CasterUdpServer : public UdpCastServer 
{
	CasterServer& Server;

	unsigned Id;
#ifdef USE_DISK_FILE
	FILE* Data;
#else
	string Data;
	unsigned Offset;
#endif 

public:
	CasterUdpServer(CasterServer& server);
	~CasterUdpServer();
	bool onGetData(string& data, unsigned maxSize);
	bool onJoin(const SockDesc& desc);
	void onLeave(const SockDesc& desc);
	void onTimeout(const SockDesc& desc);
	void onReset();

	bool empty() const { 
#ifdef USE_DISK_FILE
		return !Data || feof(Data);
#else // USE_DISK_FILE
		return Offset >= Data.size(); 
#endif // USE_DISK_FILE
	}

	unsigned id() const { return Id; }
};

typedef vector<CasterSession*> CasterSessionList;
typedef vector<CasterSessionClient*> CasterSessionClientList;
typedef vector<CasterSessionSender*> CasterSessionSenderList;
typedef map<unsigned, unsigned> CasterServerBlockUsage;

class CasterServer : public TcpSock, public CasterServerArgs
{
	// Fields
private:
	CasterSessionList m_sessionList;
	CasterSessionClientList m_clientList;
	CasterSessionSenderList m_senderList;

	auto_ptr<ImageDesc> m_imageDesc;
	auto_ptr<CasterUdpServer> m_castServer;

	CasterServerBlockUsage m_blockList;

	// Constructor
public:
	CasterServer(const CasterServerArgs& args);

	// Destructor
public:
	~CasterServer();

	// Handlers
protected:
	void onSockAccept(const SocketType& type);

private:
	double showServerStats(unsigned);

	// Methods
public:
	void sendBlockInfo(unsigned id, Hash hash);
#ifdef USE_DISK_FILE
	bool getNextBlock(unsigned& id, FILE*& data, unsigned& dataSize, Hash& hash);
#else
	bool getNextBlock(unsigned& id, string& data, unsigned& dataSize, Hash& hash);
#endif
	void finishedBlock(unsigned id);

	void addBlockToSend(CasterSessionClient* owner, unsigned id);
	void removeBlockFromSend(CasterSessionClient* owner, unsigned id);

	CasterSessionClient* findClient(const SockDesc& desc);

	friend class CasterSession;
	friend class CasterSessionClient;
	friend class CasterSessionSender;
	friend class CasterUdpServer;
};
