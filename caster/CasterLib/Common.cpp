#include "CasterLib.hpp"
#include "Common.hpp"
#include "../AsyncLib/Common.hpp"

long long parseBytes(const char* data) {
	char* endptr = NULL;
	double value = strtod(data, &endptr);
	switch(endptr != NULL ? *endptr : 'b') {
		case 'G':		value *= 1024;
		case 'M':		value *= 1024;
		case 'K':
		case 'k':		value *= 1024;
		case '\0':
		case 'b':		break;
		default:		throw runtime_error(va("invalid bytes value: %s", data));
	}
	return (long long)value;
}

string formatBytes(long long value) {
	double vf = (double)value;

	if(vf < 1024)
		return va("%ld", value);

	vf /= 1024.0;
	if(vf < 1024)
		return va("%.1lfk", vf);

	vf /= 1024.0;
	if(vf < 1024)
		return va("%.1lfM", vf);

	vf /= 1024.0;
	return va("%.1lfG", vf);
}

long long getValueBytes(const char* name, long long value, long long minValue, long long maxValue) {
	char* env = getenv(name);
	if(env)	value = parseBytes(env);
	if(value < minValue) value = minValue;
	else if(value > maxValue) value = maxValue;
	return value;
}

unsigned getValue(const char* name, unsigned value, unsigned minValue, unsigned maxValue) {
	char* env = getenv(va("CASTER%s", name).c_str());
	if(env)	value = atol(env);
	if(value < minValue) value = minValue;
	else if(value > maxValue) value = maxValue;
	return value;
}

bool checkImageName(const string& imageName) {
	for(string::const_iterator itor = imageName.begin(); itor != imageName.end(); ++itor) {
		if(isalnum(*itor))
			continue;
		if(*itor == '_' || *itor == '-')
			continue;
		return false;
	}
	return imageName.size() != 0;
}

bool checkDeviceName(const string& deviceName) {
	for(string::const_iterator itor = deviceName.begin(); itor != deviceName.end(); ++itor) {
		if(isalnum(*itor))
			continue;
		if(*itor == '_' || *itor == '-' || *itor == ':' || *itor == '.')
			continue;
		return false;
	}
	return deviceName.size() != 0;
}