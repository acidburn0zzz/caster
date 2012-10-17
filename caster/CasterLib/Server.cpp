#define DEBUG_LEVEL SERVER_DEBUG_LEVEL
#include "CasterLib.hpp"
#include "Common.hpp"

const double STATS_INTERVAL = 1.0;

#ifdef _WIN32
//#define LOCALHOST
#endif

CasterServer::CasterServer(const CasterServerArgs& args) : CasterServerArgs(args) {
	debugp("server", "creating...");

	m_imageDesc = ImageDesc::loadImageFromFile(ImageName);

	createServer(Port, Address);

	m_castServer.reset(new CasterUdpServer(*this));//
#ifndef LOCALHOST
	m_castServer->createServer(Port, Address, false);
	m_castServer->setSendAddress(Port, Maddress);
#else
	m_castServer->createServer(Port+1, Address, false);
	m_castServer->setSendAddress(Port, Address);
#endif
	m_castServer->setBlocking(true);
	m_castServer->setLimitSendRate(Rate);
	m_castServer->setSendBufferSize(262144);
	m_castServer->SlownessFactor = 5;
	m_castServer->MaxSize = args.FragSize;

	Timer::after(TimerDelegate(this, &CasterServer::showServerStats), STATS_INTERVAL);
}

CasterServer::~CasterServer() {
	delete_all(m_sessionList.begin(), m_sessionList.end());

	debugp("server", "destroying...");

	Timer::cancel(TimerDelegate(this, &CasterServer::showServerStats));
}

void CasterServer::onSockAccept(const SocketType& type) {
	new CasterSession(*this, type);
}

void CasterServer::sendBlockInfo(unsigned id, Hash hash) {
	FurEach(CasterSessionSenderList, itor, m_senderList) {
		if(!*itor)
			continue;
		(*itor)->sendBlock(id, hash);
	}
}

void CasterServer::addBlockToSend(CasterSessionClient* owner, unsigned id) {
	if(!owner->m_multiCast) {
		return;
	}

	unsigned& count = m_blockList[id];

	// add to internal list
	if(count == 0) {
		++count;
	}

	// remove from internal list
	else if(count == 1) {
		++count;
	}

	// increase counter
	else {
		++count;
	}
}

void CasterServer::removeBlockFromSend(CasterSessionClient* owner, unsigned id) {
	if(!owner->m_multiCast)
		return;

	unsigned& count = m_blockList[id];

	assert(count != 0);

	// remove from internal list
	if(count == 1) {
		m_blockList.erase(id);
	}

	// add to internal list
	else if(count == 2) {
		--count;
	}

	// decrease counter
	else {
		--count;
	}
}

double CasterServer::showServerStats(unsigned) {
	unsigned queuedBlocks = 0;
	FurEach(CasterSessionClientList, sessionItor, m_clientList) {
		queuedBlocks += (*sessionItor)->m_blockList.size();
	}
	
	updatef("-- %5ikB/s -- %i [%i / %i] sessions -- %5ikB send length -- %i/%.2f%% -- ", 
		unsigned(m_castServer->sendRate() >> 10), queuedBlocks, m_blockList.size(),
		m_sessionList.size() + m_clientList.size() + m_senderList.size(), unsigned(m_castServer->SendLength >> 10), m_castServer->clientCount(),
		m_castServer->NakCount * 100.0f / m_castServer->SendCount);

	if(m_sessionList.empty() || !m_castServer->empty())
		return STATS_INTERVAL * 2;

	return STATS_INTERVAL;
}

#ifdef USE_DISK_FILE
bool CasterServer::getNextBlock(unsigned& id, FILE*& data, unsigned& dataSize, Hash& hash) {
#else
bool CasterServer::getNextBlock(unsigned& id, string& data, unsigned& dataSize, Hash& hash) {
#endif
	if(m_blockList.empty())
		return false;

	unsigned maxCount = 0;
	vector<unsigned> sendList;
	sendList.reserve(1000);

	// find block with max usage count
	cFurEach(CasterServerBlockUsage, block, m_blockList) {
		if(m_castServer->id() == block->first)
			continue;

		if(block->second < maxCount)
			continue;
		
		// just append block to sendList
		if(block->second == maxCount) {
			if(sendList.size() < sendList.capacity())
				sendList.push_back(block->first);
			continue;
		}

		maxCount = block->second;
		sendList.resize(0);
		sendList.push_back(block->first);

		// if found block with max clients, send it first!
		if(maxCount == m_clientList.size())
			break;
	}

	// randomize send block
	if(sendList.empty())
		return false;
	id = sendList[rand() % sendList.size()];

	if(maxCount == 1) {
		// schedules local send!
		FurEach(CasterSessionClientList, client, m_clientList) {
			(*client)->m_sendLocally = true;
		}
	}
	else if(maxCount) {
		// check sector data
		BlockDesc desc(*m_imageDesc, id);
		if(!desc.valid()) {
			m_blockList.erase(id);
			return false;
		}

		// schedules one send! (amazing)
		FurEach(CasterSessionClientList, client, m_clientList) {
			if((*client)->wantsBlock(id))
				(*client)->m_sendLocally = false;
			else
				(*client)->m_sendLocally = true;
		}

#ifdef USE_DISK_FILE
		data = desc.dataOpen();
#else
		data = desc.data();
#endif
		dataSize = desc.dataSize();
		hash = desc.hash();
		return true;
	}
	return false;
}

void CasterServer::finishedBlock(unsigned id) {
	debugp("server", "finished block [id=%i]", id);
	
	FurEach(CasterSessionClientList, sessionItor, m_clientList) {
		(*sessionItor)->removeBlockFromList(id);
	}
}
	
CasterSessionClient* CasterServer::findClient(const SockDesc& desc) {
	FurEach(CasterSessionClientList, sessionItor, m_clientList) {
		if((*sessionItor)->desc().sin_addr.s_addr == desc.sin_addr.s_addr)
			return *sessionItor;
	}
	return NULL;
}

CasterUdpServer::CasterUdpServer(CasterServer& server) : Server(server) {
}

CasterUdpServer::~CasterUdpServer() {
	onReset();
}

void CasterUdpServer::onReset() {
	if(Id) {
		Server.finishedBlock(Id);
	}

#ifdef USE_DISK_FILE
	if(Data) {
		fclose(Data);
		Data = NULL;
	}
#else
	Offset = 0;
	Data.clear();
#endif
}

bool CasterUdpServer::onGetData(string& data, unsigned maxSize) {
	if(!Id) {
		// get next block
		CasterPacketBlockData packet;
		if(!Server.getNextBlock(packet.Id, Data, packet.DataSize, packet.Hash))
			return false;
			
		debugp("udpserver", "sending block [id=%i]", packet.Id);

		// send header
		Id = packet.Id;
		packet.Type = SERVERPT_BlockData;
		data.assign((const char*)&packet, (const char*)(&packet+1));
#ifndef USE_DISK_FILE
		Offset = 0;
#endif
		return true;
	}

	// send part of data
#ifdef USE_DISK_FILE
	if(!feof(Data)) {
		data.resize(maxSize);
		data.resize(fread(&data[0], 1, maxSize, Data));
		return true;
	}

	// end transmission
	fclose(Data);
	Data = NULL;
#else
	if(Offset < Data.size()) {
		data = Data.substr(Offset, maxSize);
		Offset += data.size();
		return true;
	}

	// end transmission
	Data.clear();
	Offset = 0;
#endif

	// finish block sending
	Server.finishedBlock(Id);
	Id = 0;

	// send null data
	data.clear();
	return true;	
}

bool CasterUdpServer::onJoin(const SockDesc& desc) {
	CasterSessionClient* session = Server.findClient(desc);

	// only first time allow to connect
	if(!session || session->m_gotGetData > 1) {
		debugp("udpserver", "disallow join [%s]", va(desc).c_str());
		return false;
	}

	debugp("udpserver", "join [%s]", va(desc).c_str());

	// update block usage
	if(session && session->m_multiCast == false) {
		session->m_multiCast = true;
		session->m_sendLocally = false;
		FurEach(SessionBlockList, itor, session->m_blockList)
			Server.addBlockToSend(session, *itor);
	}
	return true;
}

void CasterUdpServer::onLeave(const SockDesc& desc) {
	CasterSessionClient* session = Server.findClient(desc);

	debugp("udpserver", "leave [%s]", va(desc).c_str());
		
	// update block usage
	if(session && session->m_multiCast == true) {
		FurEach(SessionBlockList, itor, session->m_blockList)
			Server.removeBlockFromSend(session, *itor);
		session->m_multiCast = false;
		session->m_sendLocally = true;
	}
}

void CasterUdpServer::onTimeout(const SockDesc& desc) {
	onLeave(desc);
}
