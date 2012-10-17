#define DEBUG_LEVEL SERVER_DEBUG_LEVEL
#include "CasterLib.hpp"
#include "Common.hpp"
#include <limits.h>

const double PING_TIME = 10.0;
const int PING_TIMEOUTS = 3;

CasterSessionClient::CasterSessionClient(CasterServer& server, CasterSession& session) : PacketSock(session), Server(server) {
	Server.m_clientList.push_back(this);

	infof("Client %s connected.", sockName().c_str());
}

CasterSessionClient::~CasterSessionClient() {
	// cleanup all sector usage
	if(m_multiCast) {
		FurEach(SessionBlockList, itor, m_blockList)
			Server.removeBlockFromSend(this, *itor);
		m_blockList.clear();
	}

	// force multicast disconnect
	Server.m_castServer->disconnect(desc());

	// remove session pointer
	CasterSessionClientList::iterator itor = std::find(Server.m_clientList.begin(), Server.m_clientList.end(), this);
	if(itor != Server.m_clientList.end())
		Server.m_clientList.erase(itor);

	Timer::cancel(this);

	infof("Client %s disconnected.", sockName().c_str());
}

double CasterSessionClient::sendPeriodicPing(unsigned) {
	if(++m_timedOut > PING_TIMEOUTS) {
		// client timedout
		infof("Client %s timedout!", sockName().c_str());
		close();
	}
	else {
		// send ping packet
		CasterPacketPing ping;
		ping.Type = SERVERPT_Ping;
		sendPacket(&ping, sizeof(ping));
	}
	return PING_TIME;
}

void CasterSessionClient::onGetImage(const CasterPacketGetImage& image) {
	string name(image.DeviceName, strnlen(image.DeviceName, COUNT_OF(image.DeviceName)));

	infof("Session %s requested %s.", sockName().c_str(), name.c_str());

	// Znajdz opis urzadzenia
	DeviceDesc device = imageDesc().findDevice(name);
	if(!device) {
		sendFinished(NoDeviceFound);
		return;
	}

	// Wyslij opis obrazy
	CasterPacketImage imageInfo;
	imageInfo.Type = SERVERPT_Image;
	imageInfo.Version = VERSION;
	strncpy(imageInfo.ImageName, imageDesc().name().c_str(), COUNT_OF(imageInfo.ImageName));
	imageInfo.Multicast = inet_addr(Server.Maddress.c_str());
	sendPacket(&imageInfo, sizeof(imageInfo));

	// Utworz liste blokow
	vector<BlockInfo> blockList;
	vector<long long> blockOffsetList;
	device.blockList(blockList);
	
	// Wyslij opisy bloków
	FurEach(vector<BlockInfo>, block, blockList) {
		// Znajdz blok
		device.blockOffsetList(block->Id, blockOffsetList);

		// wygeneruj pakiet
		auto_ptr<CasterPacketBlock> blockPacket(CasterPacketBlock::alloc(blockOffsetList.size()));
		blockPacket->Type = SERVERPT_Block;
		blockPacket->DataSize = block->DataSize;
		blockPacket->RealSize = block->RealSize;
		blockPacket->Hash = block->Hash;
		blockPacket->Id = block->Id;
		blockPacket->DataCrc32 = 0;
		std::copy(blockOffsetList.begin(), blockOffsetList.end(), &blockPacket->List[0]);
		sendPacket(blockPacket.get(), blockPacket->size());
	}

	CasterPacket ready;
	ready.Type = SERVERPT_Ready;
	sendPacket(&ready, sizeof(ready));

	debugp("session", "sending Image [blocks=%i]", blockList.size());
}

void CasterSessionClient::onGetPong(const CasterPacketGetPong& pong) {
	m_timedOut = 0;
	debugp("session", "received pong");
}

void CasterSessionClient::onGetData(const CasterPacketGetData& data) {
	++m_gotGetData;

	// send periodic pings
	Timer::after(TimerDelegate(this, &CasterSessionClient::sendPeriodicPing), PING_TIME);

	// force multicast disconnect
	if(m_gotGetData > 1) {
		Server.m_castServer->disconnect(desc());
	}

	// add data to receive
	for(unsigned i = 0; i < data.Count; ++i) {
		BlockDesc desc(imageDesc(), data.List[i]);
		if(!desc.valid())
			continue;
		if(m_blockList.find(data.List[i]) != m_blockList.end())
			continue;
		m_blockList.insert(data.List[i]);
		Server.addBlockToSend(this, data.List[i]);
	}

	debugp("session", "got GetData [blocks=%i]", m_blockList.size());
}

void CasterSessionClient::onPacket(const void* data, unsigned size) {
	if(size == 0) {
		close();
	}

	const CasterPacket* packet = (const CasterPacket*)data;

	switch(packet->Type) {
		case CLIENTPT_GetImage:
			onGetImage(*(const CasterPacketGetImage*)data);
			break;

		case CLIENTPT_GetData:
			onGetData(*(const CasterPacketGetData*)data);
			break;

		case CLIENTPT_GetPong:
			onGetPong(*(const CasterPacketGetPong*)data);
			break;

		default:
			debugp("session", "got unknown packet type from client");
			break;
	}
}

void CasterSessionClient::onSockWrite() {
	PacketSock::onSockWrite();

	if(isWriteQueueFull())
		return;

	if(m_blockList.empty())
		return;

	// no multicast send first one
	if(!m_multiCast) {
		sendBlockData(*m_blockList.begin());
	}
	// choose sector with lowest usage count
	else if(m_sendLocally) {
		unsigned maxCount = UINT_MAX;
		unsigned id = 0;

		FurEach(SessionBlockList, blockId, m_blockList) {
			// get sector usage count
			unsigned count = Server.m_blockList[*blockId];
			if(count >= maxCount)
				continue;

			// save current selection
			maxCount = count;
			id = *blockId;

			// if only by this send it!
			if(maxCount <= 1)
				break;
		}

		if(id) {
			sendBlockData(id);
		}
	}
}

//! Pobiera aktywny obraz
ImageDesc& CasterSessionClient::imageDesc() {
	return *Server.m_imageDesc;
}

bool CasterSessionClient::wantsBlock(unsigned id) {
	return m_blockList.find(id) != m_blockList.end();
}

bool CasterSessionClient::removeBlockFromList(unsigned id) {
	// Blok jest na liscie?
	if(m_blockList.find(id) == m_blockList.end())
		return false;

	// Usun blok z listy blokow
	Server.removeBlockFromSend(this, id);

	// Usun z listy
	m_blockList.erase(id);

	// Jesli lista jest pusta wysylamy Finished
	if(m_blockList.empty())
		sendFinished(Finished);
	return true;
}

void CasterSessionClient::sendFinished(CasterFinishedErrorCode errorCode) {
	infof("Session %s finished with '%s'.", sockName().c_str(), va(errorCode).c_str());

	CasterPacketFinished packet;
	packet.Type = SERVERPT_Finished;
	packet.ErrorCode = errorCode;
	sendPacket(&packet, sizeof(packet));
}

void CasterSessionClient::sendBlockData(unsigned id) {
	BlockDesc desc(imageDesc(), id);
	assert(desc.valid());

	if(!removeBlockFromList(id))
		return;

	// request sector data
	string data = desc.data();

	// build data descriptor
	CasterPacketBlockData block;
	block.Type = SERVERPT_BlockData;
	block.Id = id;
	block.Hash = desc.hash();
	block.DataSize = data.size();

	// send sector data
	const void* parts[] = {&block, data.size() ? &data[0] : ""};
	unsigned sizes[] = {sizeof(block), data.size()};
	sendPacket(COUNT_OF(parts), parts, sizes);
}
