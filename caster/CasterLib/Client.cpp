#define DEBUG_LEVEL CLIENT_DEBUG_LEVEL
#include "CasterLib.hpp"
#include "Common.hpp"
#include "../AsyncLib/Common.hpp"

const double CLIENT_PROGRESS_INTERVAL = 0.5;

CasterCastClient::CasterCastClient(CasterClient& client) : Client(client) {
}

void CasterCastClient::onJoinTimeout() {
}

void CasterCastClient::onJoin() {
}

bool CasterCastClient::onConsumeData(const string& data) {
	// hold on stream till end of finish
	if(!Client.waitForRead())
		return false;

	// receive data
	if(!data.empty()) {
		Data += data;
		return true;
	}

	// no data to intercept
	if(Data.empty())
		return true;

	// invalid data
	if(Data.size() < sizeof(CasterPacketBlockData)) {
InvalidData:
		Data.clear();
		return true;
	}

	// check data
	CasterPacketBlockData* block = (CasterPacketBlockData*)&Data[0];
	if(block->Type != SERVERPT_BlockData)
		goto InvalidData;
	if(block->size() != Data.size())
		goto InvalidData;

	// intercept data
	Client.onBlockDataPacket(*block);
	Data.clear();
	return true;
}

void CasterCastClient::onLeave() {

}

void CasterCastClient::onAliveTimeout() {

}

CasterClient::CasterClient(const CasterClientArgs& args) : CasterClientArgs(args) {
	m_blockCloneList.reserve(64);

	infof("Receiving %s...", FileName.c_str());

	// Otworz obraz
	m_file = fopen64(FileName.c_str(), "r+b");
	if(!m_file)
		m_file = fopen64(FileName.c_str(), "w+b");
	assert(m_file);
	
	// Polacz sie z serwerem
	createClient(Port, Address);

	// Wyslij powitanie
	sendGetImage(DeviceName);
}


bool CasterClient::waitForRead() const {
	return m_blockFinishList.size() <= MAX_BLOCKS_IN_WRITE_QUEUE;
}

CasterClient::~CasterClient() {
	// Anuluj timery
	Timer::cancel(TimerDelegate(this, &CasterClient::onShowProgress));	

	try {
		if(m_castClient.get()) {
			m_castClient->sendLeave();
		}
	}
	catch(...) {
	}

	// Czekaj na zakonczenie watku
	if(m_worker) {
		infof("Waiting for worker...");

		// Czekaj az worker dokonczy prace
		while(m_blockFinishList.size()) {
			m_workerCond.signal();
			usleep(300 * 1000);
		}

		// Wyczysc liste blokow
		MutexMe(m_mutex, m_blockList.clear());

		// Zamknij workera
		m_workerCond.signal();
		m_worker.join();
	}

	// Finalizuj obraz
	if(m_state == Commit)
		finishImage();

	// Usun wszystkie klonowane bloki
	delete_all(m_blockCloneList.begin(), m_blockCloneList.end());
			
	// Zamknij plik
	fclose(m_file);
}

void CasterClient::onImagePacket(const CasterPacketImage& image) {
	assert(m_state == Idle);
	m_state = Image;

	// Zniszcz stara liste fragmentow
	m_blockList.clear();

	// Odbierz nazwe serwera
	m_version = image.Version;
	m_imageName.assign(image.ImageName, strnlen(image.ImageName, COUNT_OF(image.ImageName)));
	m_maddress = image.Multicast;

	debugp("client", "image [version=%i, name=%s, multicast=%08x]", m_version, m_imageName.c_str(), m_maddress);
}

void CasterClient::onBlockDataPacket(const CasterPacketBlockData& block) {
	assert(m_state == Ready);

	// znajdz blok
	ClientBlockList::iterator itor = m_blockList.find(block.Id);
	if(itor == m_blockList.end())
		return;
	ClientBlockDesc& desc = itor->second;
	if(desc.DataSize != block.DataSize)
		return;

	MutexLock mutex(m_mutex);

	// dodaj blok do finalizacji
	m_blockFinishList.push_back(ClientBlockData::alloc(desc, block.data(), block.DataSize));
	m_blockList.erase(itor);
	m_workerCond.signal();
}

void CasterClient::onBlockPacket(const CasterPacketBlock& block) {
	assert(m_state == Image);

	MutexLock lock(m_mutex);

	// wczytaj blok
	ClientBlockDesc& desc = m_blockList[block.Id];
	desc.Id = block.Id;
	desc.DataSize = block.DataSize;
	desc.RealSize = block.RealSize;
	desc.Hash = block.Hash;
	desc.Ranges.assign(block.List, block.List + block.Count);
}

void CasterClient::onReadyPacket() {
	assert(m_state <= Image);
	m_state = Ready;

	// Sprawdz aktualne dane
	removeExistingBlocks();
	m_blockCount = m_blockList.size();

	// Wyslij informacje o brakujacych blokach
	if(sendGetRemainingData()) {
		// Utworz gniazdo
		if(m_maddress) {
			m_castClient.reset(new CasterCastClient(*this));
			m_castClient->createServer(Port, BindAddress, false);
			m_castClient->setBlocking(true);
			if((m_maddress&0xFFFF)==0xffef) {
				m_castClient->addGroupAddress(m_maddress);
			}
			m_castClient->setRecvBufferSize(262144);
			m_castClient->setSendAddress(Port, Address);
			m_castClient->sendJoin();
		}

		// Pokaz postep
		Timer::after(TimerDelegate(this, &CasterClient::onShowProgress), 1.0);

		// Utworz watek pracownika
		m_worker.start(ThreadDelegate(this, &CasterClient::onWorkerThread));
		return;
	}

	// Zmien stan na odebrane
	infof("No data to receive!");
	m_state = Commit;
	close();
}

void CasterClient::onPingPacket(const CasterPacketPing& ping) {
	debugp("client", "got ping, responding with pong");

	// send pong to server
	CasterPacketGetPong pong;
	pong.Type = CLIENTPT_GetPong;
	sendPacket(&pong, sizeof(pong));
}

void CasterClient::onFinishedPacket(const CasterPacketFinished& finished) {
	switch(finished.ErrorCode) {
		case Finished:
			{
			assert(m_state <= Ready);

			// wypelnij kolejke
			if(sendGetRemainingData())
				return;

			// koniec transmisji
			infof("Got all data!");
			m_state = Commit;
			}
			break;

		case NoDeviceFound:
			infof("Get failed: No device found");
			break;

		default:
			infof("Unknown error code");
			break;
	}

	close();
}

void CasterClient::onPacket(const void* data, unsigned size) {
	// Zamknij polaczenie
	if(size == 0) {
		if(m_state == Commit)
			close();
		else
			hang();
		return;
	}

	const CasterPacket* packet = (const CasterPacket*)data;

	// Przetworz pakiet
	switch(packet->Type) {
		case SERVERPT_Image:
			onImagePacket(*(const CasterPacketImage*)data);
			break;

		case SERVERPT_Block:
			onBlockPacket(*(const CasterPacketBlock*)data);
			break;

		case SERVERPT_BlockData:
			onBlockDataPacket(*(const CasterPacketBlockData*)data);
			break;

		case SERVERPT_Ready:
			onReadyPacket();
			break;

		case SERVERPT_Finished:
			onFinishedPacket(*(const CasterPacketFinished*)data);
			break;

		case SERVERPT_Ping:
			onPingPacket(*(const CasterPacketPing*)data);
			break;

		default:
			debugp("client", "got unknown packet type from server");
			break;
	}
}

void CasterClient::sendGetImage(const string& deviceName) {
	infof("Requesting %s...", deviceName.c_str());

	// wyslij zadanie sciagniecia obrazu
	CasterPacketGetImage image;
	image.Type = CLIENTPT_GetImage;
	strncpy(image.DeviceName, deviceName.c_str(), COUNT_OF(image.DeviceName));
	sendPacket(&image, sizeof(image));
}

void CasterClient::sendGetData(const vector<unsigned>& blockList) {
	infof("Requesting %i blocks...", blockList.size());

	// Brak blokow do pobrania?
	if(blockList.empty())
		return;

	// wyslij pakiet
	auto_ptr<CasterPacketGetData> data(CasterPacketGetData::alloc(blockList.size()));
	data->Type = CLIENTPT_GetData;
	std::copy(blockList.begin(), blockList.end(), &data->List[0]);
	sendPacket(data.get(), data->size());
}

bool CasterClient::sendGetRemainingData() {
	// Dodaj wszystkie pozostale bloki
	vector<unsigned> blockList;

	{
		MutexLock lock(m_mutex);
		
		blockList.reserve(m_blockList.size());
		cFurEach(ClientBlockList, itor, m_blockList) {
			blockList.push_back(itor->first);
		}

		// Wyslij liste blokow
		if(m_blockList.empty()) {
			// Anuluj postep
			Timer::cancel(TimerDelegate(this, &CasterClient::onShowProgress));		
			return false;
		}
	}
	
	// Pokaz postep
	Timer::after(TimerDelegate(this, &CasterClient::onShowProgress), 1.0);	

	// Wyslij liste blokow
	sendGetData(blockList);
	return true;
}

double CasterClient::onShowProgress(unsigned id) {
	if(m_state != Ready)
		return DESTROY_TIMER;

	updatef("-- %i/%i blocks [%5ikB/s] -- %i in write --", 
		m_blockCount - m_blockList.size(), m_blockCount,
		unsigned((m_castClient->recvRate() + recvRate()) / 1024),  m_blockFinishList.size());

	// Uaktualnij w kazdej sekundzie
	return CLIENT_PROGRESS_INTERVAL;
}
