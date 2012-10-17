#define DEBUG_LEVEL 3
#include "CasterLib/CasterLib.hpp"
#include "CasterLib/Common.hpp"
#include "AsyncLib/Common.hpp"
#include "AsyncLib/Log.hpp"

#ifdef _WIN32
int optind=1;
char *optarg;

char getopt(int argc, char *argv[], const char* options)
{
	const char *loc;

  if (optind>=argc) {
    return -1;
  } else if (strlen(argv[optind])<=1) {
    return -1;
  } else if (argv[optind][0]!='-') {
    return -1;
  } else if (argv[optind][1]=='-') {
    optind++;
	return -1;
  } else if (!isalpha(argv[optind][1])) {
    return '?';
  } else if ((loc=strchr(options,argv[optind][1]))==NULL) {
	return '?';
  } else {
	optind++;
	if (loc[1]==':') {
      optarg=argv[optind++];
	}
	return loc[0];
  }
}
#endif // _WIN32

inline bool checkBlockSize(unsigned size) {
	return (MIN_BLOCK_SIZE <= size && size <= MAX_BLOCK_SIZE);
}

inline void assertBlockSize(unsigned size) {
	assert(checkBlockSize(size));
}

struct CmdFunc {
	const char* Name;
	const char* Args;
	int (*Exec)();
	const char* Desc;
};

// Global Arguments
unsigned short Port = 3000;
string Bind = "0.0.0.0";
string Image;
string FileName;
string Name, NewName;
unsigned BlockSize = DEFAULT_BLOCK_SIZE;
long long Rate = 0;
string Host;
string Multicast;
bool Compress = true;
long long LimitBytes = 0;
bool Update = false;
bool ShowHelp = false;
unsigned Retries = 10;
unsigned RetryTime = 5;
unsigned short FragSize = 1400;
bool ReadMBR = true;

static int doParseArgs(int argOffset, int argc, char* argv[], const char* argList) {
	// Wylosuj dane
	srand(unsigned(timef() * 10000));
	Multicast = va("239.255.6.%i", 1 + (rand() % 254));

	// Wczytaj stale srodowiskowe
	char* env;
	if(env = getenv("CASTERPORT"))
		Port = atoi(env);
	if(env = getenv("CASTERBIND"))
		Bind = env;
	if(env = getenv("CASTERIMAGE"))
		Image = env;
	if(env = getenv("CASTERFILE"))
		FileName = env;
	if(env = getenv("CASTERNAME"))
		Name = env;
	if(env = getenv("CASTERBLOCKSIZE"))
		BlockSize = unsigned(parseBytes(env));
	if(env = getenv("CASTERHOST"))
		Host = env;
	if(env = getenv("CASTERCOMPRESS"))
		Compress = atoi(env) != 0;
	if(env = getenv("CASTERMULTICAST"))
		Multicast = env;
	if(env = getenv("CASTERRATE"))
		Rate = parseBytes(env);
	if(env = getenv("CASTERBYTES"))
		LimitBytes = parseBytes(env);
	if(env = getenv("CASTERUPDATE"))
		Update = atoi(env) != 0;
	if(env = getenv("CASTERREADMBR"))
		ReadMBR = atoi(env) != 0;
	if(env = getenv("CASTERRETRIES"))
		Retries = max(atoi(env), 1);
	if(env = getenv("CASTERRETRYTIME"))
		RetryTime = max(atoi(env), 1);

	// Wczytaj argumenty
	optind = argOffset;
	while(1) {
		switch(getopt(argc, argv, argList)) {
			case 'c':
				Compress = true;
				break;

			case 'p':
				Port = atoi(optarg);
				break;

			case 'M':
				ReadMBR = atoi(optarg) != 0;
				break;

			case 'b':
				Bind = optarg;
				break;

			case 'i':
				Image = optarg;
				break;

			case 'f':
				FileName = optarg;
				break;

			case 'n':
				Name = optarg;
				break;

			case 'N':
				NewName = optarg;
				break;

			case 's':
				BlockSize = unsigned(parseBytes(optarg));
				break;

			case 'H':
				Host = optarg;
				break;

			case 'm':
				Multicast = optarg;
				break;

			case 'r':
				Rate = parseBytes(optarg);
				break;

			case 'B':
				LimitBytes = parseBytes(optarg);
				break;

			case 'u':
				Update = atoi(optarg) != 0;
				break;

			case 'R':
				Retries = atoi(optarg);
				break;

			case 'T':
				RetryTime = atoi(optarg);
				break;

			case 'V':
				Verbosity = atoi(optarg);
				break;

			case 'F':
				FragSize = atoi(optarg);
				break;

			case 'h':
				ShowHelp = true;
				break;

			case -1:
				goto checkArgs;

			default:
				return -1;
		}
	}

checkArgs:
	if(ShowHelp)
		return 0;
	if(strchr(argList, 'i'))
		if(!checkImageName(Image))
			throw invalid_argument("Image");
	if(strchr(argList, 'n'))
		if(!checkDeviceName(Name))
			throw invalid_argument("Name");
	if(strchr(argList, 'N'))
		if(!checkDeviceName(NewName))
			throw invalid_argument("NewName");
	if(strchr(argList, 's'))
		if(!checkBlockSize(BlockSize))
			throw invalid_argument("BlockSize");
	if(strchr(argList, 'H'))
		if(Host.empty())
			throw invalid_argument("Host");
	if(strchr(argList, 'F'))
		if(FragSize < 64 || FragSize > 65000)
			throw invalid_argument("FragSize");
	return 0;
}

// Function Handlers
static int doCreate() {
	auto_ptr<ImageDesc> imageDesc(ImageDesc::newImage(Image));
	return 0;
}

static int doAdd() {
	auto_ptr<ImageDesc> imageDesc(ImageDesc::loadImageFromFile(Image));
	imageDesc->addImage(FileName, Name, BlockSize);
	return 0;
}

static int doShow() {
	auto_ptr<ImageDesc> imageDesc(ImageDesc::loadImageFromFile(Image));
	ImageStats stats;
	imageDesc->stats(stats);
	printf("Blocks: %i\n", stats.BlockCount);
	printf("Devices: %i\n", stats.DeviceCount);
	printf("DeviceBlocks: %i\n", stats.DeviceBlockCount);
	printf("CompressedSize: %s\n", formatBytes(stats.CompressedBlockSize).c_str());
	printf("RealSize: %s\n", formatBytes(stats.RealBlockSize).c_str());
	printf("UnusedBlocks: %i (%i%%)\n", stats.UnusedBlockCount, stats.BlockCount ? stats.UnusedBlockCount*100/stats.BlockCount : 0);
	printf("UnusedCompressedSize: %s (%i%%)\n", formatBytes(stats.UnusedCompressedBlockSize).c_str(), stats.CompressedBlockSize ? stats.UnusedCompressedBlockSize*100/stats.CompressedBlockSize : 0);
	printf("UnusedRealSize: %s (%i%%)\n", formatBytes(stats.UnusedRealBlockSize).c_str(), stats.RealBlockSize ? stats.UnusedRealBlockSize*100/stats.RealBlockSize : 0);
	printf("AvgBlockUsage: %.2f\n", stats.AvgBlockUsage);
	printf("\n");

	for(int i = 0; i < stats.DeviceList.size(); ++i) {
		DeviceStats& device = stats.DeviceList[i];
		printf("Device: %s\n", device.Name.c_str());
		printf("Id: %u\n", device.Id);
		printf("Blocks: %i\n", device.BlockCount);
		printf("Offsets: %i\n", device.OffsetCount);
		printf("CompressedSize: %s\n", formatBytes(device.CompressedSize).c_str());
		printf("RealSize: %s\n", formatBytes(device.RealSize).c_str());
		printf("AvgReuseCount: %.2f\n", device.AvgReuseCount);
		printf("\n");
	}
	return 0;
}

static int doShowDevices() {
	auto_ptr<ImageDesc> imageDesc(ImageDesc::loadImageFromFile(Image));
	ImageStats stats;
	imageDesc->stats(stats);
	for(int i = 0; i < stats.DeviceList.size(); ++i) {
		printf("%s\n", stats.DeviceList[i].Name.c_str());
	}
	return 0;
}

static int doOptimize() {
	printf("-- BEFORE --\n");
	doShow();

	{
		auto_ptr<ImageDesc> imageDesc(ImageDesc::loadImageFromFile(Image));
		imageDesc->removeUnusedBlocks();
	}

	printf("-- AFTER --\n");
	doShow();
	return 0;
}

static CasterServer* createServer() {
	CasterServerArgs args;
	args.ImageName = Image;
	args.Port = Port;
	args.Address = Bind;
	args.Maddress = Multicast;
	args.Rate = Rate;
	args.FragSize = FragSize;
	return new CasterServer(args);
}

static int doServer() {
	CasterServer* server = createServer();
	while(true) {
		try {
			while(Fd::waitForEvents(5000));
			break;
		}
		catch(SockException& e) {
			delete e.m_sock;
			if(e.m_sock == server)
				break;
		}
	}
	return 0;
}

static int doClient() {
	CasterClientArgs args;
	args.FileName = FileName;
	args.Compress = Compress;
	args.Address = Host;
	args.Port = Port;
	args.DeviceName = Name;
	args.BindAddress = Bind;
	args.Update = Update;

	for(unsigned i = 0; i < Retries; ++i) {
		try {
			new CasterClient(args);
			while(Fd::waitForEvents());
			break;
		}
		catch(SockException& e) {
			printf("-- RESTARTING (%s) --\n", e.what());
			delete e.m_sock;
			usleep(RetryTime * 1000 * 1000);
		}
		catch(std::exception& e) {
			printf("-- RESTARTING (%s) --\n", e.what());
			usleep(RetryTime * 1000 * 1000);
		}
	}
	return 0;
}

static int doClientLoop() {
	createServer();
	return doClient();
}

static int doSend() {
	CasterSenderArgs args;
	args.FileName = FileName;
	args.Compress = Compress;
	args.Address = Host;
	args.Port = Port;
	args.DeviceName = Name;
	args.LimitBytes = LimitBytes;
	args.BlockSize = BlockSize;
	args.ReadMBR = ReadMBR;

	for(unsigned i = 0; i < Retries; ++i) {
		try {
			new CasterSender(args);
			while(Fd::waitForEvents());
			break;
		}
		catch(SockException& e) {
			printf("-- RESTARTING (%s) --\n", e.what());
			delete e.m_sock;
			usleep(RetryTime * 1000 * 1000);
		}
		catch(std::exception& e) {
			printf("-- RESTARTING (%s) --\n", e.what());
			usleep(RetryTime * 1000 * 1000);
		}
	}
	return 0;
}

static int doSendLoop() {
	createServer();
	return doSend();
}

static int doRemove() {
	auto_ptr<ImageDesc> imageDesc(ImageDesc::loadImageFromFile(Image));
	DeviceDesc deviceDesc(imageDesc->findDevice(Name));
	deviceDesc.remove();
	return 1;
}

static int doClone() {
	auto_ptr<ImageDesc> imageDesc(ImageDesc::loadImageFromFile(Image));
	DeviceDesc desc = imageDesc->findDevice(Name);
	if(!desc)
		throw std::invalid_argument("Device not found");
	DeviceBlockOffsetList offsetList;
	desc.blockOffsetList(offsetList);
	DeviceDesc newDesc = imageDesc->addDevice(NewName, offsetList);
	if(!newDesc)
		return 1;
	return 0;
}

CmdFunc CmdFuncList[] = {
	{"create", "i:V:h", doCreate, "create new image"},
	{"add", "i:f:n:s:V:h", doAdd, "add file to image"},
	{"optimize", "i:V:h", doOptimize, "optimize image disk usage"},
	{"show", "i:V:h", doShow, "show image statistics"},
	{"showdevices", "i:V:h", doShowDevices, "show image devices"},
	{"server", "i:p:b:m:r:V:F:h", doServer, "start an server"},
	{"client", "f:cH:p:n:b:R:T:V:u:h", doClient, "start an client"},
	{"send", "f:cH:p:n:B:s:R:T:V:hM:", doSend, "send a file to remote server"},
#ifdef _DEBUG
	{"clientloop", "i:b:m:r:F:f:cH:p:n:b:R:T:V:u:h", doClientLoop, "start an client in loop"},
	{"sendloop", "i:b:m:r:F:f:cH:p:n:B:s:R:T:V:hM:", doSendLoop, "send a file to server in loop"},
#endif // _DEBUG
	{"remove", "i:n:V:h", doRemove, "remove a device"},
	{"clone", "i:n:N:V:h", doClone, "clone a device"}
};

static int showHelp(const char* self, const char* target = NULL, const char* argList = NULL) {
	fprintf(stderr, CASTERLIB_INFO "\n");
	fprintf(stderr, "usage: %s {target} [options]\n", self);
	if(target == NULL) {
		fprintf(stderr, "targets:\n");
		for(unsigned i = 0; i < COUNT_OF(CmdFuncList); ++i)
			fprintf(stderr, "  %s - %s\n", CmdFuncList[i].Name, CmdFuncList[i].Desc);
	}
	else {
		fprintf(stderr, "%s options:\n", target);
		if(strchr(argList, 'p'))
			fprintf(stderr, "  -p <port> : port : %i\n", Port);
		if(strchr(argList, 'b'))
			fprintf(stderr, "  -b <address> : bind address : %s\n", Bind.c_str());
		if(strchr(argList, 'i'))
			fprintf(stderr, "  -i <image> : image name : %s\n", Image.c_str());
		if(strchr(argList, 'f'))
			fprintf(stderr, "  -f <file> : file name : %s\n", FileName.c_str());
		if(strchr(argList, 'n'))
			fprintf(stderr, "  -n <name> : device name : %s\n", Name.c_str());
		if(strchr(argList, 'N'))
			fprintf(stderr, "  -N <newName> : new device name : %s\n", NewName.c_str());
		if(strchr(argList, 's'))
			fprintf(stderr, "  -s <size> : block size : %s\n", formatBytes(BlockSize).c_str());
		if(strchr(argList, 'H'))
			fprintf(stderr, "  -H <host> : server host name : %s\n", Host.c_str());
		if(strchr(argList, 'c'))
			fprintf(stderr, "  -c : whatever send a compressed data : %i\n", Compress);
		if(strchr(argList, 'u'))
			fprintf(stderr, "  -u : update image or get a new one: %i\n", Update);
		if(strchr(argList, 'm'))
			fprintf(stderr, "  -m <address> : server multicast address : %s\n", Multicast.c_str());
		if(strchr(argList, 'r'))
			fprintf(stderr, "  -r <rate> : limit multicast send rate (0 - disable) : %s\n", formatBytes(Rate).c_str());
		if(strchr(argList, 'B'))
			fprintf(stderr, "  -B <bytes> : limit sending bytes (0 - disable) : %s\n", formatBytes(LimitBytes).c_str());
		if(strchr(argList, 'R'))
			fprintf(stderr, "  -R : number of failure retries : %i\n", Retries);
		if(strchr(argList, 'T'))
			fprintf(stderr, "  -T : wait seconds before connection retry: %i\n", RetryTime);
		if(strchr(argList, 'V'))
			fprintf(stderr, "  -V : verbosity : 0 - quiet : %i\n", Verbosity);
		if(strchr(argList, 'F'))
			fprintf(stderr, "  -F : multicast fragment size in bytes (affect multicast performance): %i\n", FragSize);
		if(strchr(argList, 'M'))
			fprintf(stderr, "  -M : read mbr: %i\n", ReadMBR);
		if(strchr(argList, 'h'))
			fprintf(stderr, "  -h : show this help\n");
		fprintf(stderr, ".\n");
	}
	return 1;
}

CmdFunc* cmdFunc(const char* name) {
	for(unsigned i = 0; i < COUNT_OF(CmdFuncList); ++i) {
		if(strcmp(name, CmdFuncList[i].Name))
			continue;
		return &CmdFuncList[i];
	}
	return NULL;
}

int main(int argc, char* argv[]) {
	if(argc >= 2) {
		CmdFunc* func = cmdFunc(argv[1]);

		try {
			if(doParseArgs(2, argc, argv, func->Args) || ShowHelp)
				return showHelp(argv[0], argv[1], func->Args);

			return func->Exec();
		}
		catch(exception& e) {
			infof("-- got exception (%s): %s --", typeid(e).name(), e.what());
			return -2;
		}
		catch(Fd* fd) {
			infof("Disconnected!");
			delete fd;
			return 0;
		}
		catch(...) {
			infof("-- got unhandled exception --");
			return -1;
		}
	}

	return showHelp(argv[0], NULL, NULL);
}
