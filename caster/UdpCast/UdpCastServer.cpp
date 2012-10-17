#include "../AsyncLib/AsyncLib.hpp"
#include "../AsyncLib/Log.hpp"
#include "../AsyncLib/ForEach.hpp"
#include "UdpCastPacket.hpp"
#include "UdpCastServer.hpp"

template<typename T>
inline void unique_merge(set<T>& c1, const set<T>& c2) {
	cFurEach(typename set<T>, c, c2) {
		c1.insert(*c);
	}
}

inline bool operator < (const SockDesc& a, const SockDesc& b) {
	return memcmp(&a, &b, sizeof(SockDesc)) < 0;
}

UdpCastServer::UdpCastServer() {
	m_updateTime = timef();
	MaxSize = 1400;
}

UdpCastServer::~UdpCastServer() {
	Timer::cancel(this);
}

double UdpCastServer::resendKeepAlive(unsigned count) {
	RTT *= 3;
	RTT = min(RTT, 3000000U);
	sendKeepAlive();
	return -1;
}

void UdpCastServer::sendKeepAliveAfter(unsigned time) {
	if(time > 0)
		Timer::after(TimerDelegate(this, &UdpCastServer::resendKeepAlive), time / 1000000.0, 1);
	else if(time < 0)
		Timer::cancel(TimerDelegate(this, &UdpCastServer::resendKeepAlive));
}

void UdpCastServer::sendKeepAlive(bool resetKeepAliveTimeout, Client* client) {
	MclKeepAlivePacket packet;
	packet.Type = MclKeepAlive;
	packet.Seq = m_seq;
	if(client && short(client->Seq - packet.Seq) > 0)
		packet.Seq = client->Seq;
	packet.MaxSeq = m_maxSeq;
	packet.Tick = nanotime();
	sendAllData(&packet, sizeof(packet), client ? &client->Sock : NULL);
	
	vdebugp(6, "udpserver", "sending keepAlive [seq=%i, maxSeq=%i, to=%s]", m_seq, m_maxSeq, client ? va(client->Sock).c_str() : NULL);
	
	if(!client) {
		sendKeepAliveAfter(RTT * 3);
	
		FurEach(ClientList, client, m_clientList) {
			client->second.KeepAliveTime = timef();
			if(resetKeepAliveTimeout)
				client->second.KeepAliveTimeout = 0;
			else
				++client->second.KeepAliveTimeout;
		}
	}
	else {
		client->KeepAliveTime = timef();
		if(resetKeepAliveTimeout)
			client->KeepAliveTimeout = 0;
		else
			++client->KeepAliveTimeout;
	}
	
	++KeepAliveCount;

	m_updateTime = timef();
}

void UdpCastServer::sendSeqData(unsigned short seq, bool sendKeepAlive) {
	short index = seq - m_seq;
	if(index < 0 || index >= UDPCAST_MAX_SEND_WINDOW)
		throw runtime_error("seq not within send window");

	vdebugp(7, "udpserver", "sending data [seq=%i]", seq);
		
	unsigned offset = (index + m_queueOffset) % UDPCAST_MAX_SEND_WINDOW;
		
	if(!m_queue[offset].get())
		throw runtime_error("seq doesn't exist");
		
	const string& content = *m_queue[offset];

	if(sendKeepAlive) {
		sendKeepAliveAfter(RTT * 3);
	
		FurEach(ClientList, client, m_clientList) {
			client->second.KeepAliveTime = timef();
			client->second.KeepAliveTimeout = 0;
		}
		//++KeepAliveCount;
	}
	
	if(short(seq - m_maxSeq) >= 0)
		m_maxSeq = seq+1;

	MclDataPacket2 packet;
	packet.Type = MclData;
	packet.Seq = seq;
	packet.MaxSeq = m_maxSeq;
	packet.KeepAlive = sendKeepAlive;
	packet.Tick = nanotime();
	memcpy(packet.Data, &content[0], content.size());
	sendAllData(&packet, sizeof(MclDataPacket) + content.size());
	m_nakList.erase(seq);
	SendLength += content.size();
	++SendCount;

	m_updateTime = timef();
}

unsigned UdpCastServer::disconnect(const SockDesc& desc) {
	unsigned count = 0;

	FurEach(ClientList, client, m_clientList) {
		if(client->first.sin_addr.s_addr != desc.sin_addr.s_addr)
			continue;

		SockDesc clientDesc(client->first);
		m_clientList.erase(client);
		sendLeave(&clientDesc);
		onLeave(clientDesc);
		++count;
	}

	return count;
}

void UdpCastServer::sendLeave(const SockDesc* desc) {
	MclLeaveResponsePacket packet;
	packet.Type = MclLeaveResponse;
	sendAllData(&packet, sizeof(packet), desc);
}

unsigned UdpCastServer::windowSize() const {
	float size = 0;
	cFurEach(ClientList, client, m_clientList) {
		size = max(client->second.Window, size);
	}
	//size = floorf(size/2 + 0.5f) * 2;
	return (unsigned)size;
}

void UdpCastServer::updateRTT() {
	RTT = 0;
	unsigned count = 0;
	
	cFurEach(ClientList, client, m_clientList) {
		if(client->second.RTT) {
			RTT += client->second.RTT;
			++count;
		}
	}
	
	if(count) {
		RTT /= count;
	}
	else {
		RTT = 500*1000;
	}
}

void UdpCastServer::gotUpdate(MclUpdatePacket2& update, int size, const SockDesc& desc) {
	++UpdateCount;
	
	if(m_clientList.find(desc) == m_clientList.end())
		return;
	
	m_updateTime = timef();

	Client& client = m_clientList[desc];
	client.KeepAliveTimeout = 0;
	client.UpdateTime = timef();
	client.Rate = update.Rate;

	vdebugp(6, "udpserver", "got update from %s [s=%i,ms=%i] [s=%i/%i,ms=%i]", 
		va(desc).c_str(), update.Seq, update.MaxSeq,
		client.Seq, m_seq, m_maxSeq);

	if(short(update.Seq - m_seq) < 0) {
		return;
	}

	unsigned confirmCount = 1;

	client.NakList.clear();

	if(update.Seq == client.Seq)
		++client.LostCount;
	else {
		while(short(update.Seq - client.Seq) > 0) {
			client.NakList.erase(client.Seq);
			++client.Seq;
			client.LostCount = 0;
			++confirmCount;
		}
	}
	
	if(update.Tick > 0) {
		int rtt = int(nanotime() - update.Tick);
		if(rtt > 0) {
			rtt = min(max((unsigned)rtt, 100U), 3000000U);
			client.RTT = unsigned(client.RTT * 0.9 + rtt * 0.1);
			unsigned maxWindowSize = 1000 * 1000 / client.RTT;
			if(client.Window > maxWindowSize) {
				client.Window /= 2;
				client.Window = max(client.Window, 1.0f);
			}
		}
	}

	int dataCount = (size - sizeof(MclUpdatePacket)) * 8;

	unsigned lostCount = 0;

	short seqCount = update.MaxSeq - update.Seq;

	for(short index = 0; index < seqCount; ++index) {
		if(short(update.Seq + index - m_maxSeq) >= 0)
			break;
		if(index >= dataCount || ~update.Data[index >> 3] & (1<<(index&7))) {
			client.NakList.insert(update.Seq + index);
			++lostCount;
		}
	}

	if(short(client.Seq - client.WinSeq) >= 0) {
		if(client.LostCount >= 3) {
			client.Window /= 2;
			client.Window = max(client.Window, 2.0f);
			client.LostCount = 0;

			unsigned short newWinSeq = client.Seq + (unsigned)client.Window;
			if(short(newWinSeq - client.WinSeq) > 0)
				client.WinSeq = newWinSeq;
		}
		else if(lostCount > 3) {
			client.Window -= sqrt(client.Window);
			client.Window = max(client.Window, 2.0f);
			client.WinSeq = client.Seq;
			client.LostCount = 0;
		}
		else if(client.Window > 0) {
			client.Window += 1/client.Window;
			client.Window = min<float>(client.Window, (float)UDPCAST_MAX_SEND_WINDOW);
			client.WinSeq = client.Seq;
		}
		else {
			client.Window = 1;
			client.WinSeq = client.Seq;
		}
	}
	
	client.NextKeepAliveTime = timef() + RTT * 10;

	updateRTT();
	
	m_update = true;
}

void UdpCastServer::gotJoin(MclJoinPacket& update, int size, const SockDesc& desc) {
	if(m_clientList.find(desc) == m_clientList.end()) {
		if(!onJoin(desc))
			return;
	}
	
	vdebugp(4, "udpserver", "got join");

	Client& client = m_clientList[desc];
	client.Seq = m_seq;
	client.WinSeq = m_seq;
	client.Sock = desc;
	client.UpdateTime = timef();
	client.ID = update.ID;

	MclJoinResponsePacket response;
	response.Type = MclJoinResponse;
	response.Tick = nanotime();
	response.Seq = m_seq;
	response.Accept = 1;
	response.ID = update.ID;
	sendAllData(&response, sizeof(response)); // send to all using multicast!

	m_updateTime = timef();
}

void UdpCastServer::gotJoinResponse(MclJoinResponsePacket& update, int size, const SockDesc& desc) {
	if(m_clientList.find(desc) == m_clientList.end())
		return;
		
	vdebugp(4, "udpserver", "got joinResponse");
		
	Client& client = m_clientList[desc];
	client.RTT = nanotime() - update.Tick;
	client.UpdateTime = timef();
}

void UdpCastServer::gotLeave(MclLeavePacket& update, int size, const SockDesc& desc) {
	if(m_clientList.find(desc) == m_clientList.end())
		return;
		
	vdebugp(4, "udpserver", "got leave");

	onLeave(desc);
	
	m_clientList.erase(desc);
	MclLeaveResponsePacket response;
	response.Type = MclLeaveResponse;
	sendAllData(&response, sizeof(response), &desc);

	m_updateTime = timef();
}

void UdpCastServer::onSockRead(const SockData& data) {
	MclPacket* packet = (MclPacket*)data.Data;

	switch(packet->Type) {
		case MclJoin:
			gotJoin(*(MclJoinPacket*)data.Data, data.Size, data.Desc);
			break;
			
		case MclJoinResponse:
			gotJoinResponse(*(MclJoinResponsePacket*)data.Data, data.Size, data.Desc);
			break;
			
		case MclUpdate:
			gotUpdate(*(MclUpdatePacket2*)data.Data, data.Size, data.Desc);
			break;

		case MclLeave:
			gotLeave(*(MclLeavePacket*)data.Data, data.Size, data.Desc);
			break;
	}
}

void UdpCastServer::onSockWrite() {
	bool keepAlive = false;

	if(m_nakList.size()) {
		if(m_clientList.empty()) {
			m_nakList.clear();
		}
		else {
			cFurEach(set<unsigned short>, seq, m_nakList) {
				sendSeqData(*seq, true);
				++NakCount;
			}
		}
	}

	if(!allDataAccepted())
		return;

	unsigned window = windowSize();

	for(unsigned i = 0; i < window; ++i) {
		unsigned offset = (i + m_queueOffset) % UDPCAST_MAX_SEND_WINDOW;
		
		if(!m_queue[offset].get()) {
			string data;
			if(!onGetData(data, min<unsigned>(MaxSize, MaxPacketSize)))
				break;
			m_queue[offset].reset(new string(data));
			ContentLength += data.size();
		}

		if(short(m_seq + i - m_maxSeq) < 0)
			continue;

		if(i == window / 2) {
			keepAlive = true;
		}
		else if(i+1 == window) {
			keepAlive = true;
		}

		sendSeqData(m_seq + i, keepAlive);
		keepAlive = false;
	}

	if(keepAlive) {
		sendKeepAlive(true);
	}
}

double sqr(double v) {
	return v * v;
}

void UdpCastServer::onFdTick() {
	vdebugp(8, "udpserver", "fdTick [seq=%i, maxSeq=%i, nakList=%i, update=%i]", m_seq, m_maxSeq, m_nakList.size(), m_update);
	
	if(m_update) {
		m_nakList.clear();

		if(m_clientList.size()) {
			unsigned short maxSeq = m_maxSeq;
			double rate = 0;
			unsigned count = 0;
			
			FurEach(ClientList, client, m_clientList) {
				if(short(client->second.Seq - maxSeq) < 0)
					maxSeq = client->second.Seq;
				FurEach(UdpCastSeqNakList, nak, client->second.NakList) {
					m_nakList.insert(*nak);
				}

				if(client->second.Rate > 1000) {
					rate += client->second.Rate;
					++count;
				}
			}

			assert(short(m_seq - maxSeq) <= 0);

			while(m_seq != maxSeq) {
				m_queue[m_queueOffset].reset();
				m_queueOffset = (m_queueOffset + 1) % UDPCAST_MAX_SEND_WINDOW;
				++m_seq;
			}
		}

		m_update = false;
	}

	double currentTime = timef();

	FurEach(ClientList, client, m_clientList) {
#ifndef _DEBUG
		// client timeout
		if(client->second.UpdateTime + UDPCAST_CLIENT_TIMEOUT < currentTime) {
			SockDesc desc = client->first;
			m_clientList.erase(client);
			onTimeout(desc);
			continue;
		}
#endif

		if(client->second.KeepAliveTime + UDPCAST_KEEPALIVE_TIME < currentTime) {
			vdebugp(5, "udpserver", "sending periodic keepAlive");
			
			sendKeepAlive(false, &client->second);
		}
	}

	if(m_clientList.size() && SlownessFactor > 0 && 
		m_lastSlownessCheck > 0 && m_lastSlownessCheck + 3 < currentTime) 
	{
		double avgTraffic = 0;
		unsigned avgCount = 0;

		FurEach(ClientList, client, m_clientList) {
			avgTraffic += 1.0 / client->second.RTT; 
			++avgCount;
		}

		avgTraffic /= avgCount;

		double maxRTT = SlownessFactor / avgTraffic;
		
		FurEach(ClientList, client, m_clientList) {
			if(client->second.RTT > maxRTT && onTooSlow(client->first)) {
				onLeave(client->first);
				sendLeave(&client->first);
				m_clientList.erase(client->first);
			}
		}

		m_lastSlownessCheck = currentTime;
	}
}

bool UdpCastServer::allDataAccepted() const {
	cFurEach(ClientList, client, m_clientList) {
		if(short(m_maxSeq - client->second.Seq) > 0)
			return false;
	}
	return true;
}

bool UdpCastServer::waitForWrite() const {
	if(m_clientList.empty())
		return false;
	
	if(m_nakList.size())
		return true;

	return allDataAccepted() && short(m_seq + windowSize() - m_maxSeq) > 0;
}
