#pragma once

class CasterSession : public PacketSock
{
	CasterServer& Server;

	// Constructor
public:
	CasterSession(CasterServer& server, SocketType socket);

	// Destructor
public:
	~CasterSession();

	// Handlers
private:
	void onPacket(const void* data, unsigned size);

	// Methods
private:
	//! Pobiera aktywny obraz
	ImageDesc& imageDesc();
};
