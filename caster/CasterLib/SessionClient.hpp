#pragma once

class CasterServer;
class CasterSession;

typedef set<unsigned> SessionBlockList;

class CasterSessionClient : public PacketSock
{
	CasterServer& Server;
	SessionBlockList m_blockList;
	bool m_multiCast, m_sendLocally;
	unsigned m_gotGetData;
	unsigned m_timedOut;

	// Constructor
public:
	CasterSessionClient(CasterServer& server, CasterSession& session);

	// Destructor
public:
	~CasterSessionClient();

	// Handlers
public:
	void onGetImage(const CasterPacketGetImage& image);
	void onGetData(const CasterPacketGetData& data);
	void onGetPong(const CasterPacketGetPong& pong);
	void onPacket(const void* data, unsigned size);

	void onSockWrite();

	bool waitForWrite() const {
		return PacketSock::waitForWrite() || (!m_multiCast || m_sendLocally) && m_blockList.size();
	}

private:
	double sendPeriodicPing(unsigned);

	// Methods
public:
	//! Pobiera aktywny obraz
	ImageDesc& imageDesc();

	void sendImage();
	void sendBlockData(unsigned id);
	void sendFinished(CasterFinishedErrorCode errorCode = Finished);

	bool removeBlockFromList(unsigned id);
	bool wantsBlock(unsigned id);
	
	unsigned blockCount() const { return m_blockList.size(); }

	friend class CasterServer;
	friend class CasterUdpServer;
};
