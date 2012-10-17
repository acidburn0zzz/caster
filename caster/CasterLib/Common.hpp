#pragma once

long long getValueBytes(const char* name, long long value, long long minValue = 0, long long maxValue = ~0);
unsigned getValue(const char* name, unsigned value, unsigned minValue = 0, unsigned maxValue = ~0);

long long parseBytes(const char* data);
string formatBytes(long long value);
