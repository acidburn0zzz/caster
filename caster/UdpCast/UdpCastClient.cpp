#include "../AsyncLib/AsyncLib.hpp"
#include "../AsyncLib/Log.hpp"
#include "../AsyncLib/ForEach.hpp"
#include "UdpCastPacket.hpp"
#include "UdpCastClient.hpp"

UdpCastClient::UdpCastClient() {
	m_clientId = ticks();
}

UdpCastClient::~UdpCastClient() {
	Timer::cancel(this);
}

double UdpCastClient::sendJoin(unsigned retries) {
	if(m_state != Idle && m_state != Join)
		throw runtime_error("invalid state, expected Idle or Join");
		
	if(retries == 0) {
		onJoinTimeout();
		m_state = Idle;
		return -1;
	}
		
	MclJoinPacket packet;
	packet.Type = MclJoin;
	packet.ID = m_clientId;
	sendAllData(&packet, sizeof(packet));
	m_state = Join;
	
	Timer::after(TimerDelegate(this, &UdpCastClient::sendJoin), UDPCAST_JOIN_TIMEOUT, retries-1);
	
	return DESTROY_TIMER;
}

void UdpCastClient::sendUpdate(unsigned tick, unsigned short maxSeq) {
	if(m_state != Data)
		throw runtime_error("invalid state, expected Data");
		
	unsigned short offset = pendingSize();

	MclUpdatePacket2 packet;
	memset(packet.Data, 0, sizeof(packet.Data));
	packet.Type = MclUpdate;
	packet.Tick = tick;
	packet.Seq = m_seq + offset;
	packet.MaxSeq = m_seq + offset;
	packet.Rate = recvLastRate();

	for(unsigned i = offset; i < UDPCAST_MAX_RECV_WINDOW; ++i) {
		if(short(m_seq + i - maxSeq) >= 0)
			break;

		if(m_recvQueue[(i + m_recvOffset) % UDPCAST_MAX_RECV_WINDOW].get()) {
			packet.MaxSeq = m_seq + i + 1;
			packet.Data[(i - offset) >> 3] |= 1<<((i - offset)&7);
		}
	}

	vdebugf(5, "got keepAlive [ms=%i] [s=%i,of=%i,wn=%i]",
		maxSeq, m_seq, offset, windowSize());

	vdebugf(5, "sending update [ps=%i,pms=%i]", packet.Seq, packet.MaxSeq);
	
	if(packet.Seq == packet.MaxSeq && short(packet.MaxSeq - maxSeq) < 0 && offset < UDPCAST_MAX_RECV_WINDOW) // advance seq by one
		++packet.MaxSeq;
	
	unsigned dataSize = (short(packet.MaxSeq - packet.Seq) + 7) >> 3;
	sendAllData(&packet, sizeof(MclUpdatePacket) + dataSize);
	
	++UpdateCount;
}

unsigned UdpCastClient::windowSize() const {
	unsigned count = 0;
	for(unsigned i = 0; i < UDPCAST_MAX_RECV_WINDOW; ++i) {
		if(m_recvQueue[i].get())
			++count;
	}
	return count;
}

unsigned UdpCastClient::pendingSize() const {
	unsigned count = 0;
	for(unsigned i = 0; i < UDPCAST_MAX_RECV_WINDOW; ++i) {
		if(m_recvQueue[(i + m_recvOffset) % UDPCAST_MAX_RECV_WINDOW].get())
			++count;
		else
			return count;
	}
	return count;
}

double UdpCastClient::sendLeave(unsigned retries) {
	if(m_state != Data && m_state != Leave)
		throw runtime_error("invalid state, expected Data or Leave");
		
	MclLeavePacket packet;
	packet.Type = MclLeave;
	sendData(&packet, sizeof(packet));
	m_state = Leave;
	
	Timer::after(TimerDelegate(this, &UdpCastClient::sendLeave), UDPCAST_JOIN_TIMEOUT, retries-1);
	
	return DESTROY_TIMER;
}

void UdpCastClient::flushRecvWindow(bool update) {
	bool update2 = false;
	
	while(m_recvQueue[m_recvOffset].get()) {
		if(!onConsumeData(*m_recvQueue[m_recvOffset]))
			return;

		ContentLength += m_recvQueue[m_recvOffset]->size();
		m_recvQueue[m_recvOffset].reset();
		m_recvOffset = (m_recvOffset + 1) % UDPCAST_MAX_RECV_WINDOW;
		++m_seq;
		update2 = true;
	}
	if(update && update2)
		sendUpdate(0, m_seq);
}

void UdpCastClient::gotJoinResponse(MclJoinResponsePacket& join, int size, const SockDesc& desc) {
	if(m_state != Join && m_state != Data)
		return;

	if(join.ID != m_clientId)
		return;

	vdebugf(5, "got joinResponse [seq=%i]", join.Seq);

	Timer::cancel(TimerDelegate(this, &UdpCastClient::sendJoin));
	
	sendAllData(&join, sizeof(join));
	m_state = Data;
	m_seq = join.Seq;
	m_keepAliveTime = timef();
	
	onJoin();
}

void UdpCastClient::gotData(MclDataPacket2& data, int size, const SockDesc& desc) {
	if(m_state != Data)
		return;
	
	short index = data.Seq - m_seq;

	if(index >= 0 && index < UDPCAST_MAX_RECV_WINDOW) {
		++DataCount;

		unsigned dataSize = size - sizeof(MclDataPacket);
		if(m_recvQueue[(index + m_recvOffset) % UDPCAST_MAX_RECV_WINDOW].get())
			DataDuplicate++;

		m_recvQueue[(index + m_recvOffset) % UDPCAST_MAX_RECV_WINDOW].reset(new string(data.Data, data.Data + dataSize));
		flushRecvWindow();
	}
	
	if(index >= UDPCAST_MAX_RECV_WINDOW)
		++InvalidDataCount;

	if(data.KeepAlive) {
		m_keepAliveTime = timef();
		sendUpdate(data.Tick, data.MaxSeq);
	}
}

void UdpCastClient::gotKeepAlive(MclKeepAlivePacket& keepAlive, int size, const SockDesc& desc) {
	if(m_state != Data)
		return;

	m_keepAliveTime = timef();
	++KeepAliveCount;
	flushRecvWindow();
	sendUpdate(keepAlive.Tick, keepAlive.MaxSeq);
}

void UdpCastClient::gotLeaveResponse(MclLeaveResponsePacket& leave, int size, const SockDesc& desc) {
	if(m_state != Leave && m_state != Idle) {
		return;
	}

	Timer::cancel(TimerDelegate(this, &UdpCastClient::sendLeave));
	
	m_state = Idle;
	onLeave();
}

void UdpCastClient::onSockRead(const SockData& data) {
	MclPacket* packet = (MclPacket*)data.Data;

	switch(packet->Type) {
		case MclJoinResponse:
			gotJoinResponse(*(MclJoinResponsePacket*)data.Data, data.Size, data.Desc);
			break;
			
		case MclData:
			gotData(*(MclDataPacket2*)data.Data, data.Size, data.Desc);
			break;

		case MclKeepAlive:
			gotKeepAlive(*(MclKeepAlivePacket*)data.Data, data.Size, data.Desc);
			break;
			
		case MclLeaveResponse:
			gotLeaveResponse(*(MclLeaveResponsePacket*)data.Data, data.Size, data.Desc);
			break;
	}
}

void UdpCastClient::onFdTick() {
	flushRecvWindow(true);

#ifndef _DEBUG
	if(m_keepAliveTime > 0 && m_keepAliveTime + UDPCAST_SENDER_TIMEOUT < timef()) {
		m_state = Idle;
		onAliveTimeout();
	}
#endif
}
