#define DEBUG_LEVEL CLIENT_DEBUG_LEVEL
#include "CasterLib.hpp"
#include "Common.hpp"
#include "../AsyncLib/Common.hpp"

ClientBlockCloneDesc* ClientBlockCloneDesc::alloc(long long offset, unsigned size, unsigned rangeCount, long long* ranges) {
	ClientBlockCloneDesc* desc;
	desc = new(sizeof(long long) * rangeCount) ClientBlockCloneDesc();
	desc->Offset = offset;
	desc->Size = size;
	desc->RangeCount = rangeCount;
	std::copy(&ranges[0], &ranges[rangeCount], &desc->Ranges[0]);
	return desc;
}

void CasterClient::removeExistingBlocks() {
	infof("Checking existing data...");
	
	MutexLock lock(m_mutex);

	unsigned totalCount = 0;
	long long totalSize = 0;
	unsigned blockCount = 0;
	long long dataSize = 0, realSize = 0;

	// Oblicz ilosc obszarow do zapisu
	FurEach(ClientBlockList, block, m_blockList) {
		ClientBlockDesc& desc = block->second;

		// Gdy blok jest pusty usun go!
		if(desc.RealSize == 0 || desc.Ranges.empty()) {
			m_blockList.erase(block);
			continue;
		}

		totalCount += desc.Ranges.size();
		totalSize += desc.Ranges.size() * desc.RealSize;
		blockCount++;
		dataSize += desc.DataSize;
		realSize += desc.RealSize;
	}

	long long toRecvDataSize = dataSize, toRecvRealSize = realSize;

	unsigned processedIndex = 0;
	long long processedData = 0;
	Hash hash;
	Rate rate;
	string data;

	if(!Update)
		goto noUpdate;

	// Sprawdz dysk
	FurEach(ClientBlockList, block, m_blockList) {
		ClientBlockDesc& desc = block->second;
		
		assert(desc.RealSize <= MAX_BLOCK_SIZE);

		long long dataOffset = ~0ULL;
		unsigned rangeCount = desc.Ranges.size();
		
		// Znajdz prawidlowy blok
		for(unsigned i = desc.Ranges.size(); i-- > 0; ) {
			long long offset = desc.Ranges[i];

			++processedIndex;

			if(fseeko64(m_file, offset, SEEK_SET))
				continue;

			// Wczytaj dane
			data.resize(desc.RealSize);
			int readed = fread((void*)data.c_str(), 1, desc.RealSize, m_file);
			if(readed > 0) {
				rate.addBytes(readed);
				if(rate.manualUpdate()) {
					updatef("-- %2i%% -- %3i of %3i blocks -- %iMB of %iMB processed -- %3iMB/s --   ", processedIndex * 100 / totalCount, processedIndex, totalCount, unsigned(processedData >> 20), unsigned(totalSize >> 20), rate.CurrentRate >> 20);
				}
			}
			if(readed < 0 || readed != desc.RealSize)
				continue;

			// Uaktualnij dane
			processedData += readed;

			// Sprawdz czy blok sie zgadza
			if(readed == desc.RealSize && 
				desc.Hash == Hash::calculateHash(data.c_str(), desc.RealSize)) 
			{
				dataOffset = offset;
				fast_erase(desc.Ranges, desc.Ranges.begin()+i);
			}
		}
		
		// Sklonuj obszar, gdy mamy poprawne przesuniecie
		if(dataOffset != ~0ULL && desc.Ranges.size()) {
			// Statystyki
			blockCount--;
			dataSize -= desc.DataSize * rangeCount;
			realSize -= desc.RealSize * rangeCount;

			debugp("client", "creating CloneDesc [block=%i, ranges=%i]", desc.Id, desc.Ranges.size());
			// Dodaj obszar do sklonowania
			m_blockCloneList.push_back(ClientBlockCloneDesc::alloc(dataOffset, desc.RealSize, desc.Ranges.size(), &desc.Ranges[0]));
			m_blockList.erase(block);	
			continue;
		}

		// Usun blok, wszystkie bloky sa poprawne!
		else if(desc.Ranges.empty()) {
			blockCount--;
			dataSize -= desc.DataSize * rangeCount;
			realSize -= desc.RealSize * rangeCount;
			m_blockList.erase(block);
			continue;
		}

		toRecvDataSize += desc.DataSize * rangeCount;
		toRecvRealSize += desc.RealSize * rangeCount;
	}

noUpdate:
	// Komunikat
	infof("Receiving %i blocks (%iMB|%iMB)...", m_blockList.size(), unsigned(toRecvDataSize >> 20), unsigned(toRecvRealSize >> 20));
}

void CasterClient::finishImage() {
	MutexLock lock(m_mutex);

	if(!m_file) {
		assert(m_blockCloneList.empty());
		return;
	}

	infof("Finalizing image...\n");

	unsigned totalCount = 0;
	unsigned index = 0;
	
	FurEach(ClientBlockCloneList, blockCloneItor, m_blockCloneList) {
		totalCount += (*blockCloneItor)->RangeCount;
	}

	string data;

	FurEach(ClientBlockCloneList, blockCloneItor, m_blockCloneList) {
		ClientBlockCloneDesc* desc = *blockCloneItor;
		
		// Wczytaj dane
		data.resize(desc->Size);
		assert(!fseeko64(m_file, desc->Offset, SEEK_SET));
		assert(fread((void*)data.c_str(), desc->Size, 1, m_file) == 1);

		// Zapisz do obszarow
		for(unsigned i = 0; i < desc->RangeCount; ++i) {
			++index;
			updatef("-- %3i%% -- %3i of %3i range(s) --", 100 * index / totalCount, index, totalCount);
			assert(!fseeko64(m_file, desc->Ranges[i], SEEK_SET));
			assert(fwrite(data.c_str(), desc->Size, 1, m_file) == 1);
		}
		fflush(m_file);
	}

	if(m_blockCloneList.size())
		infof("");

	delete_all(m_blockCloneList.begin(), m_blockCloneList.end());
}

template<typename Type>
static Type popFromQueue(deque<Type>& q) {
	Type v = q.front();
	q.pop_front();
	return v;
}

static void onWorkerThreadException() {
	try {
		throw;
	}
	catch(exception& e) {
		infof("-- worker -- got exception (%s): %s --", typeid(e).name(), e.what());
		exit(-2);
	}
	catch(...) {
		infof("-- worker -- got unhandled exception --");
		exit(-1);
	}
}

void* CasterClient::onWorkerThread(void*) {
	Mutex mutex;
	MutexLock lock(mutex);

	set_unexpected(onWorkerThreadException);
	set_terminate(onWorkerThreadException);

	assert(m_file);

	while(m_blockList.size() || m_blockFinishList.size()) {
		try {
		// Uspij mnie
		if(m_blockFinishList.empty()) {
			m_workerCond.wait(mutex);
			continue;
		}

#ifndef _DEBUG
		if(getenv("CASTERWORKER_SLEEP"))
			usleep(atoi(getenv("CASTERWORKER_SLEEP")) * 1000);
#endif

		// Przetworz kolejny blok
		auto_ptr<ClientBlockData> desc;
		{
			MutexLock lock(m_mutex);
			desc.reset(m_blockFinishList.front());
			m_blockFinishList.pop_front();
		}

		// Dekompresuj dane
		string data = Compressor::decompress(desc->Data, desc->DataSize, desc->RealSize);
		assert(data.size() == desc->RealSize);

		// Zapisz tylko do okreslonej ilosci obszarow
		unsigned written = 0;

		for( ; written < desc->Ranges.size(); ++written) {
			// przerwij jeœli iloœæ bloków w kolejce dojdzie do limitu
			if(written > 1 && m_blockFinishList.size() >= MAX_BLOCKS_IN_WRITE_QUEUE/2)
				break;

			assert(!fseeko64(m_file, desc->Ranges[written], SEEK_SET));
			assert(fwrite(data.c_str(), data.size(), 1, m_file) == 1);
		}
		
		fflush(m_file);

		// Dodaj do listy obszarow do klonowania
		if(desc->Ranges.size() > written) {
			debugp("client", "creating CloneDesc [block=%i, ranges=%i]", desc->Id, desc->Ranges.size() - written);
			m_blockCloneList.push_back(ClientBlockCloneDesc::alloc(desc->Ranges[0], desc->RealSize, desc->Ranges.size() - written, &desc->Ranges[written]));
		}

		// Timeout!
		debugp("clientworker", "finished block [block=%i]", desc->Id);
		}
		catch(exception& e) {
			infof("-- worker -- got exception (%s): %s --", typeid(e).name(), e.what());
			//exit(-2);
		}
		catch(...) {
			infof("-- worker -- got unhandled exception --");
			//exit(-1);
		}
	}
	return NULL;
}
