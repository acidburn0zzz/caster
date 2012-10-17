#pragma once

class CasterServer;
class CasterSession;

class CasterSessionSender : public PacketSock
{
	CasterServer& Server;
	string m_deviceName;
	DeviceBlockOffsetList m_offsetList;

	// Constructor
public:
	CasterSessionSender(CasterServer& server, CasterSession& session);

	// Destructor
public:
	~CasterSessionSender();

	// Handlers
public:
	void onSendImage(const CasterPacketSendImage& image);
	void onSendData(const CasterPacketSenderBlockData& block);
	void onSendBlock(const CasterPacketSenderBlock& block);
	void onSendCommit();

	void onPacket(const void* data, unsigned size);

	// Methods
public:
	//! Pobiera aktywny obraz
	ImageDesc& imageDesc();

	void sendSendImage();
	void sendBlockList();
	void sendBlock(unsigned id, Hash hash);
	void sendFinished(CasterFinishedErrorCode errorCode = Finished);

	friend class CasterServer;
	friend class CasterUdpServer;
};