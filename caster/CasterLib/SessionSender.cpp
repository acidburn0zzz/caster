#define DEBUG_LEVEL SERVER_DEBUG_LEVEL
#include "CasterLib.hpp"

CasterSessionSender::CasterSessionSender(CasterServer& server, CasterSession& session) : PacketSock(session), Server(server) {
	Server.m_senderList.push_back(this);

	infof("Sender %s connected.", sockName().c_str());
}

CasterSessionSender::~CasterSessionSender() {
	CasterSessionSenderList::iterator itor = std::find(Server.m_senderList.begin(), Server.m_senderList.end(), this);
	if(itor != Server.m_senderList.end())
		Server.m_senderList.erase(itor);

	infof("Sender %s disconnected.", sockName().c_str());
}

void CasterSessionSender::onPacket(const void* data, unsigned size) {
	if(size == 0) {
		close();
	}

	const CasterPacket* packet = (const CasterPacket*)data;

	switch(packet->Type) {
		case CLIENTPT_SendImage:
			onSendImage(*(const CasterPacketSendImage*)data);
			break;

		case CLIENTPT_SendData:
			onSendData(*(const CasterPacketSenderBlockData*)data);
			break;

		case CLIENTPT_SendBlock:
			onSendBlock(*(const CasterPacketSenderBlock*)data);
			break;

		case CLIENTPT_SendCommit:
			onSendCommit();
			break;

		default:
			debugp("session", "got unknown packet type from client");
			break;
	}
}

ImageDesc& CasterSessionSender::imageDesc() {
	return *Server.m_imageDesc;
}

void CasterSessionSender::onSendImage(const CasterPacketSendImage& image) {
	// Wczytaj nazwe obrazu
	m_deviceName.assign(image.DeviceName, strnlen(image.DeviceName, COUNT_OF(image.DeviceName)));
	debugp("session", "got SendImage [name=%s]", m_deviceName.c_str());

	// Sprawdz nazwe obrazu
	if(!checkDeviceName(m_deviceName)) {
		sendFinished(InvalidDeviceName);
		return;
	}

	// Odeslij liste blokow
	sendSendImage();
}

void CasterSessionSender::onSendBlock(const CasterPacketSenderBlock& block) {
	// Wyszukaj blok
	BlockDesc desc(imageDesc(), block.Id);
	if(!desc.valid()) {
		sendFinished(InvalidBlockID);
		return;
	}

	// Wygeneruj opis bloka
	m_offsetList[desc.id()].push_back(block.Offset);
}

void CasterSessionSender::onSendData(const CasterPacketSenderBlockData& data) {
	if(data.RealSize == 0)
		return;

	// sprawdz sume kontrolna
	unsigned crc32 = Hash::crc32(&data+1, data.DataSize);
	assert(crc32 == data.DataCrc32);

	// znajdz blok
	BlockDesc desc = imageDesc().findBlock(data.Hash);
	if(!desc) {
		string in((char*)(&data+1), data.DataSize);

		// validate date
		try {
			string dein = Compressor::decompress(in.c_str(), in.size(), data.RealSize);
			Hash hash = Hash::calculateHash(dein.c_str(), dein.size());
			if(hash != data.Hash)	{
				debugp("session", "invalid block hash");
				sendFinished(InvalidBlockData);
				return;
			}

			const CompressMethod method = CmZlib; // default server compression method
			
			if(Compressor::method(in.c_str(), in.size()) != method)
				in = Compressor::compress(dein.c_str(), dein.size(), method);
		}
		catch(exception& e)	{
			debugp("session", "invalid block data : %s", e.what());
			sendFinished(InvalidBlockData);
			return;
		}

		desc = imageDesc().addBlock(in.c_str(), in.size(), data.RealSize, data.Hash);
		Server.sendBlockInfo(desc.id(), data.Hash);
	}

	// Wygeneruj opis bloka
	m_offsetList[desc.id()].push_back(data.Offset);
}

void CasterSessionSender::onSendCommit() {
	debugp("session", "got SendCommit");

	// Zapisz nowe urzadzenie
	if(imageDesc().addDevice(m_deviceName, m_offsetList))
		infof("Session %s replaced device %s.", sockName().c_str(), m_deviceName.c_str());
	else
		infof("Session %s added device %s.", sockName().c_str(), m_deviceName.c_str());
	
	sendFinished();
}

void CasterSessionSender::sendFinished(CasterFinishedErrorCode errorCode) {
	infof("Session %s finished with '%s'.", sockName().c_str(), va(errorCode).c_str());

	CasterPacketFinished packet;
	packet.Type = SERVERPT_Finished;
	packet.ErrorCode = errorCode;
	sendPacket(&packet, sizeof(packet));
}

void CasterSessionSender::sendSendImage() {
	debugp("session", "sending SendImage");

	vector<BlockDesc> blockList;
	imageDesc().blockList(blockList);

	auto_ptr<CasterPacketSenderHashList> hashList(CasterPacketSenderHashList::alloc(blockList.size()));
	hashList->Type = SERVERPT_SendImage;
	hashList->Count = blockList.size();

	for(unsigned i = 0; i < blockList.size(); ++i) {
		hashList->List[i].Id = blockList[i].id();
		hashList->List[i].Hash = blockList[i].hash();
	}
	sendPacket(hashList.get(), hashList->size());
}

void CasterSessionSender::sendBlock(unsigned id, Hash hash) {
	CasterPacketSenderHashList hashList;
	hashList.Type = SERVERPT_BlockList;
	hashList.Count = 1;
	hashList.List[0].Id = id;
	hashList.List[0].Hash = hash;
	sendPacket(&hashList, hashList.size());
}
