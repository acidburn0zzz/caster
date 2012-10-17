#pragma once

#include "../AsyncLib/AsyncLib.hpp"
#include "../AsyncLib/Log.hpp"
#include "../AsyncLib/ForEach.hpp"
#include "../UdpCast/UdpCastServer.hpp"
#include "../UdpCast/UdpCastClient.hpp"
#include "../HashLib/Hash.hpp"
#include "../CompressLib/Compress.hpp"
#include "../ImageLib/Image.hpp"

#define VERSION 1
#define CASTERLIB_INFO	"Caster date:" __DATE__ " " __TIME__

const unsigned MIN_BLOCK_SIZE = 4 * 1024; // 4kB
const unsigned MAX_BLOCK_SIZE = 64 * 1024 * 1024; // 64MB
const unsigned DEFAULT_BLOCK_SIZE = 1024 * 1024; // 1MB
const int MAX_DEVICE_NAME = 32;
const int MAX_IMAGE_NAME = 32;
const int MAX_BLOCKS_IN_WRITE_QUEUE = 10;

#define CLIENT_DEBUG_LEVEL 4
#define SERVER_DEBUG_LEVEL 4
#define IMAGE_DEBUG_LEVEL 5

bool checkImageName(const string& imageName);
bool checkDeviceName(const string& deviceName);

#include "Packet.hpp"
#include "Client.hpp"
#include "Sender.hpp"
#include "SessionClient.hpp"
#include "SessionSender.hpp"
#include "Session.hpp"
#include "Server.hpp"
