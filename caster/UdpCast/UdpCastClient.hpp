#pragma once

#include "../AsyncLib/AsyncLib.hpp"
#include "UdpCastPacket.hpp"

const int UDPCAST_MAX_RECV_WINDOW = 64;
const float UDPCAST_JOIN_TIMEOUT = 1;
const int UDPCAST_JOIN_RETRIES = 3;
const float UDPCAST_SENDER_TIMEOUT = 20;

class UdpCastClient : public UdpSock {
public:
	enum EnumState {
		Idle,
		Join,
		Data,
		Leave
	};
	
private:
	auto_ptr<string> m_recvQueue[UDPCAST_MAX_RECV_WINDOW];
	unsigned m_recvOffset;
	unsigned short m_seq;
	EnumState m_state; 
	double m_keepAliveTime;
	long long m_clientId;

public:
	unsigned ContentLength;
	unsigned KeepAliveCount;
	unsigned UpdateCount;
	unsigned DataCount, DataDuplicate;
	unsigned InvalidDataCount;

public:
	UdpCastClient();
	~UdpCastClient();

public:
	unsigned windowSize() const;
	unsigned pendingSize() const;

	double sendJoin(unsigned retries = UDPCAST_JOIN_RETRIES);
	void sendUpdate(unsigned tick, unsigned short maxSeq);
	double sendLeave(unsigned retries = UDPCAST_JOIN_RETRIES);
	void flushRecvWindow(bool update = false);
	bool receiving() const { return m_state == Data; }

private:
	void gotJoinResponse(MclJoinResponsePacket& join, int size, const SockDesc& desc);
	void gotData(MclDataPacket2& data, int size, const SockDesc& desc);
	void gotKeepAlive(MclKeepAlivePacket& keepAlive, int size, const SockDesc& desc);
	void gotLeaveResponse(MclLeaveResponsePacket& leave, int size, const SockDesc& desc);

private:
	void onSockRead(const SockData& data);
	void onFdTick();

protected:
	virtual void onJoinTimeout() { throw std::runtime_error("join timeout"); }
	virtual void onJoin() { }
	virtual bool onConsumeData(const string& data) = 0;
	virtual void onLeave() { }
	virtual void onAliveTimeout() { }
};
