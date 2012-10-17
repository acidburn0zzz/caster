#define DEBUG_LEVEL CLIENT_DEBUG_LEVEL
#include "CasterLib.hpp"
#include "Common.hpp"
#include "../AsyncLib/Common.hpp"
#include <limits.h>

const double SENDER_PROGRESS_INTERVAL	= 0.5;
const CompressMethod SENDER_COMPRESSOR = CmFastLZ;

#pragma pack(1)
struct MBRPart {
	byte Status;
	byte Start[3];
	byte Type;
	byte End[3];
	unsigned LBA;
	unsigned Count;
};

struct MBR {
	byte Code[446];
	MBRPart Parts[4];
	unsigned short Sign;
};
#pragma pack()

static void parseMBR(FILE* file, map<long long,long long>& sizes) {
	MBR mbr;

	// Wyczytaj MBR i sygnature dysku
	if(fread(&mbr, sizeof(mbr), 1, file) != 1 || mbr.Sign != 0xAA55) {
		debugf("couldn't read MBR");
		return;
	}

	// Dodaj obszar MBRa do wyslania (wraz z drugim stagem GRUBa)
	sizes[0] = 32 * 512;

	// Load partitions
	for(unsigned i = 0; i < COUNT_OF(mbr.Parts); ++i) {
		MBRPart& part = mbr.Parts[i];
		if(part.Status != 0x00 && part.Status != 0x80)
			continue;
		if(part.Count == 0 || part.LBA == 0)
			continue;
		if(part.Type == 0x82) // ignore linux swap
			continue;

		infof("Sending %i partition from %i sector (size %iMB) [0x%02x]", i+1, part.LBA, unsigned((long long)part.Count * 512 >> 20), part.Type);

		sizes[(long long)part.LBA * 512] = (long long)part.Count * 512;
	}

	map<long long,long long>::iterator itor = sizes.begin();

	// remove overlapping regions
	while(itor != sizes.end())
	{
		map<long long,long long>::iterator prev = itor++;
		if(itor == sizes.end())
			break;
		long long endOffset = prev->first + prev->second;
		if(endOffset <= itor->first) // not overlapping
			continue;

		// overlapping, merge regions
		long long endCurOffset = itor->first + itor->second;
		prev->second = max(endOffset, endCurOffset) - prev->first;
		sizes.erase(itor);
		itor = ++prev;
	}
}

CasterSender::CasterSender(const CasterSenderArgs& args) 
	: CasterSenderArgs(args), m_blockHashList(10000)
{
	// Otworz plik
	debugp("sender", "sending %s...", FileName.c_str());
	m_file = fopen64(FileName.c_str(), "rb");
	assert(m_file);

	if(ReadMBR) {
		parseMBR(m_file, m_sendSizes);
	}

	if(m_sendSizes.empty()) {
		if(LimitBytes)
			m_sendSizes[0] = LimitBytes;
		else
			m_sendSizes[0] = LLONG_MAX;
	}
	m_sendOffset = 0;

	// Polacz sie z serwerem
	createClient(Port, Address);
	setBlocking(true);

	// Wyslij powitanie
	sendSendImage(DeviceName);
}

CasterSender::~CasterSender() {
	debugp("sender", "destroying...");

	if(m_state != Commit)
		debugp("sender", "Failed to send image!");

	Timer::cancel(this);

	fclose(m_file);
}

double CasterSender::onShowProgress(unsigned) {
	updatef("-- sent %5i blocks | %5iMB of data [%5ikB/s] --", m_blocksSent, unsigned(m_dataSent >> 20), sendRate() / 1024);
	return SENDER_PROGRESS_INTERVAL;
}

void CasterSender::onPacket(const void* data, unsigned size) {
	if(size == 0) {
		close();
	}

	const CasterPacket* packet = (const CasterPacket*)data;

	switch(packet->Type) {
		case SERVERPT_SendImage:
			assert(m_state == Waiting);
			m_state = Sending;
			Timer::after(TimerDelegate(this, &CasterSender::onShowProgress), SENDER_PROGRESS_INTERVAL);

		case SERVERPT_BlockList:
			onBlockListPacket(*(const CasterPacketSenderHashList*)packet);
			break;

		case SERVERPT_Finished:
			onFinishedPacket(*(const CasterPacketFinished*)packet);
			break;

		default:
			debugp("sender", "got unknown packet type from server");
			break;
	}
}

void CasterSender::onBlockListPacket(const CasterPacketSenderHashList& hashList) {
	// Obraz wyslany?
	if(m_state > Sending)
		return;

	// Wczytaj liste blokow
	for(unsigned i = 0; i < hashList.Count; ++i)
		m_blockHashList.addHash(hashList.List[i].Hash, hashList.List[i].Id);
}

void CasterSender::onFinishedPacket(const CasterPacketFinished& finished) {
	switch(finished.ErrorCode) {
		case Finished:
			assert(m_state == Commit);
			infof("Sending finished!");
			break;

		case InvalidDeviceName:
			infof("Send failed: Invalid device name");
			break;

		case InvalidBlockID:
			infof("Send failed: Invalid block index");
			break;

		case InvalidBlockData:
			infof("Send failed: Invalid block data sent");
			break;

		default:
			infof("Unknown error code");
			break;
	}

	close();
}

void CasterSender::onSockWrite() {
	PacketSock::onSockWrite();

	double startTime = timef();

	// wysylaj az do zapelnienia kolejki wysylania
	while(!isWriteQueueFull() && m_state == Sending) {
		sendNextData();

		if(timef() - startTime > 0.5)
			break;
	}
}

void CasterSender::sendNextData() {
	assert(m_file);
	assert(m_state != Waiting);

	// Koniec transmisji
	if(feof(m_file) || m_sendSizes.empty()) {
		if(m_state == Sending)
			sendSendCommit();
		return;
	}

	// Pobierz kolejny obszar odczytu
	map<long long,long long>::iterator sendItor = m_sendSizes.upper_bound(m_sendOffset);
	if(sendItor != m_sendSizes.begin())
		--sendItor;
	long long endOffset = sendItor->second + sendItor->first;
	while(m_sendOffset >= endOffset) {
		// Koniec transmisji
		if(++sendItor == m_sendSizes.end()) {
			sendSendCommit();
			return;
		}
		m_sendOffset = sendItor->first;
		endOffset = sendItor->first + sendItor->second;
	}

	// Ustaw pozycje odczytu
	if(m_sendOffset != ftello64(m_file))
		assert(!fseeko64(m_file, m_sendOffset, SEEK_SET));

	// wczytaj obszar pliku
	long long length = std::min<long long>(endOffset - m_sendOffset, BlockSize);
	string data(length, 0);
	length = fread((void*)data.c_str(), 1, length, m_file);
	data.resize(length);

	// Koniec transmisji
	if(length <= 0) {
		sendSendCommit();
		return;
	}

	// znajdz blok
	Hash hash = Hash::calculateHash(data.c_str(), data.size());
	unsigned blockId = m_blockHashList.findHash(hash);

	// wyslij dane bloka
	if(blockId == 0) {
		++m_blocksSent;

		// przygotuj opis bloka
		CasterPacketSenderBlockData block;
		block.Type = CLIENTPT_SendData;
		block.Hash = hash;
		block.Offset = m_sendOffset;
		block.RealSize = length;
		string compressed = Compressor::compress(data.c_str(), data.size(), Compress ? SENDER_COMPRESSOR : CmNone);
		block.DataSize = compressed.size();
		const void* datas[] = {&block, compressed.c_str()};
		unsigned sizes[COUNT_OF(datas)] = {sizeof(block), compressed.size()};
		block.DataCrc32 = Hash::crc32(compressed.c_str(), compressed.size());

		// wyslij blok synchronicznie
		sendPacket(COUNT_OF(datas), datas, sizes);
	}

	// wyslij indeks bloka
	else {
		CasterPacketSenderBlock block;
		block.Type = CLIENTPT_SendBlock;
		block.Offset = m_sendOffset;
		block.Id = blockId;
		sendPacket(&block, sizeof(block));
	}

	// Przesun pozycje
	m_sendOffset += length;
	m_dataSent += length;
}

void CasterSender::sendSendImage(const string& deviceName) {
	assert(m_state == Waiting);

	setBlocking(true);

	debugp("sender", "sending SendImage [name=%s]", deviceName.c_str());

	// Utworz pakiet
	CasterPacketSendImage image;
	image.Type = CLIENTPT_SendImage;
	strncpy(image.DeviceName, deviceName.c_str(), COUNT_OF(image.DeviceName));
	sendPacket(&image, sizeof(image));
}

void CasterSender::sendSendCommit() {
	assert(m_state == Sending);

	Timer::cancel(this);

	// wyslij pakiet
	CasterPacket packet;
	packet.Type = CLIENTPT_SendCommit;
	sendPacket(&packet, sizeof(packet));

	// Zniszcz klienta
	m_state = Commit;
}
