#pragma once

struct CasterSenderArgs
{
	//! Nazwa pliku
	string FileName;

	//! Kompresja wysylanych danych
	bool Compress;

	//! Port serwera
	unsigned Port;

	//! Adres serwera
	string Address;

	//! Wielkosc bloka odczytu
	unsigned BlockSize;

	//! Nazwa urzadzenia
	string DeviceName;

	//! Limit danych do odczytu
	long long LimitBytes;

	//! Odczytaj MBR dysku
	bool ReadMBR;
};

class CasterSender : public PacketSock, public CasterSenderArgs
{
	enum State {
		Waiting,
		Sending,
		Commit
	};

	// Fields
private:
	//! Aktualny stan
	State m_state;

	//! Plik docelowy
	FILE* m_file;

	// Sending Fields
private:
	//! Lista blokow na serwerze
	HashList m_blockHashList;
	map<long long, long long> m_sendSizes;
	long long m_sendOffset;

	//! Ilosc danych i blokow wyslanych
	unsigned m_blocksSent;
	long long m_dataSent;
	

	// Constructor
public:
	CasterSender(const CasterSenderArgs& args);

	// Destructor
public:
	~CasterSender();

	// Helpers
private:
	double onShowProgress(unsigned);
	void sendNextData();

	// Handlers
private:
	void onPacket(const void* data, unsigned size);
	void onBlockListPacket(const CasterPacketSenderHashList& stream);
	void onFinishedPacket(const CasterPacketFinished& finished);
	void onSockWrite();

	// Sending Methods
public:
	void sendSendImage(const string& deviceName);
	void sendSendCommit();

	bool waitForWrite() const {
		return m_state == Sending || PacketSock::waitForWrite();
	}
};
