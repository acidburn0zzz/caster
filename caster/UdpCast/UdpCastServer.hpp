#pragma once

#include "../AsyncLib/AsyncLib.hpp"
#include "UdpCastPacket.hpp"

const int UDPCAST_MAX_SEND_WINDOW = 256;
const float UDPCAST_KEEPALIVE_TIME = 1.0;
const float UDPCAST_CLIENT_TIMEOUT = 15;

typedef set<unsigned short> UdpCastSeqNakList;

class UdpCastServer : public UdpSock {
public:
	struct Client {
		SockDesc Sock;
		unsigned short Seq;
		float Window;
		unsigned short WinSeq;
		unsigned RTT;
		UdpCastSeqNakList NakList;
		unsigned short KeepAliveTimeout;
		double UpdateTime, KeepAliveTime;
		double NextKeepAliveTime;
		long long ID;
		unsigned LostCount;
		float Rate;

		Client() {
			Seq = 0;
			Window = 0;
			WinSeq = 0;
			RTT = 0;
			KeepAliveTimeout = 0;
			UpdateTime = KeepAliveTime = 0;
			NextKeepAliveTime = 0;
			LostCount = 0;
			Rate = 0;
		}
	};

	typedef map<SockDesc, Client> ClientList;
	
private:
	ClientList m_clientList;
	unsigned short m_seq, m_maxSeq;
	bool m_update;
	auto_ptr<string> m_queue[UDPCAST_MAX_SEND_WINDOW];
	unsigned m_queueOffset;
	set<unsigned short> m_nakList;
	double m_updateTime;
	double m_lastSlownessCheck;

public: // configuration
	double SlownessFactor;
	unsigned MaxSize;

public:	// statistics
	unsigned ContentLength;
	unsigned SendLength;
	unsigned SendCount;
	unsigned NakCount;
	unsigned KeepAliveCount;
	unsigned UpdateCount, InvalidUpdateCount, SlowStart;
	unsigned RTT;

public:
	UdpCastServer();
	~UdpCastServer();

private:
	double resendKeepAlive(unsigned count);
	
public:
	void sendKeepAliveAfter(unsigned time);
	void sendKeepAlive(bool resetKeepAliveTimeout = false, Client* client = NULL);
	void sendSeqData(unsigned short seq, bool keepAlive = false);
	void sendLeave(const SockDesc* desc = NULL);
	unsigned disconnect(const SockDesc& desc);
	unsigned windowSize() const;
	unsigned clientCount() const { return m_clientList.size(); }

private:
	void updateRTT();
	void gotUpdate(MclUpdatePacket2& update, int size, const SockDesc& desc);
	void gotJoin(MclJoinPacket& update, int size, const SockDesc& desc);
	void gotJoinResponse(MclJoinResponsePacket& update, int size, const SockDesc& desc);
	void gotLeave(MclLeavePacket& update, int size, const SockDesc& desc);

	bool allDataAccepted() const;

private:
	void onSockRead(const SockData& data);
	void onSockWrite();
	void onFdTick();
	bool waitForWrite() const;

protected:
	virtual bool onGetData(string& data, unsigned maxSize) = 0;
	virtual bool onJoin(const SockDesc& desc) { return true; }
	virtual void onLeave(const SockDesc& desc) { }
	virtual void onTimeout(const SockDesc& desc) { }
	virtual bool onTooSlow(const SockDesc& desc) { return false; }
	virtual void onReset() { }
};
