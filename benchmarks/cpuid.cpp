#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cstdlib>

#include "cpuid.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// See http://msdn.microsoft.com/en-us/library/windows/desktop/ms683194(v=vs.85).aspx
typedef BOOL (WINAPI *LPFN_GLPI)(PSYSTEM_LOGICAL_PROCESSOR_INFORMATION, PDWORD);

// Helper function to count set bits in the processor mask.
int countBitsSet(ULONG_PTR bitMask)
{
	int result = 0;
	while (bitMask != 0) {
		result += (int)(bitMask & 1);
		bitMask >>= 1;
	}
	return result;
}

bool getProcessorInfoFromOS(int& cpus, int& cores, int& logicalCores, double& clockSpeed)
{
	cpus = 0;
	cores = 0;
	logicalCores = 0;
	clockSpeed = 0;
	
	// Clock speed
	HKEY hKey;
	if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, TEXT("HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0"), 0, KEY_EXECUTE, &hKey) == ERROR_SUCCESS) {
		DWORD type = REG_DWORD;
		DWORD val;
		DWORD cbData = sizeof(val);
		if (RegQueryValueEx(hKey, TEXT("~MHz"), NULL, &type, (LPBYTE)&val, &cbData) == ERROR_SUCCESS) {
			if (type == REG_DWORD && cbData == sizeof(DWORD)) {
				clockSpeed = val / 1000.0;
			}
		}
		
	}
	if (clockSpeed == 0) {
		// Can't access registry, try QueryPerformanceFrequency (nearly always same speed as CPU)
		LARGE_INTEGER f;
		if (!QueryPerformanceFrequency(&f)) {
			return false;
		}
		clockSpeed = f.QuadPart / 1000.0 / 1000.0;
	}
	
	// Everything else
	LPFN_GLPI glpi;
	glpi = (LPFN_GLPI)GetProcAddress(GetModuleHandle(TEXT("kernel32")), "GetLogicalProcessorInformation");
	if (glpi == NULL) {
		return false;
	}
	
	PSYSTEM_LOGICAL_PROCESSOR_INFORMATION buffer = NULL;
	DWORD bufferLength = 0;
	if (glpi(buffer, &bufferLength) == TRUE) {
    	return false;
    }
    
	while (GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
		if (buffer != NULL) {
			std::free(buffer);
		}
		buffer = (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION)std::malloc(bufferLength);
		if (buffer == NULL) {
			return false;
		}
		if (glpi(buffer, &bufferLength) == TRUE) {
			if (bufferLength / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION) * sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION) != bufferLength) {
				// sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION) must have changed (different from at compile time)
				std::free(buffer);
				return false;
			}
			
			auto end = (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION)((char*)buffer + bufferLength);
			for (auto ptr = buffer; ptr != end; ++ptr) {
				switch (ptr->Relationship) {
				case RelationProcessorCore:
					++cores;
					logicalCores += countBitsSet(ptr->ProcessorMask);
					break;
				case RelationProcessorPackage:
					++cpus;
					break;
				default:
					break;
				}
			}
			
			std::free(buffer);
			return true;
		}
	}
	if (buffer != NULL) {
		std::free(buffer);
	}
	return false;
}
#else
// TODO
bool getProcessorInfoFromOS(int& cpus, int& cores, int& logicalCores, double& clockSpeed)
{
	return false;
}
#endif


#if defined(__x86_64__) || defined(_M_AMD64) || defined(__amd64__) || defined (_M_X64) || defined(_M_IX86) || defined(__i386__)
#define MOODYCAMEL_X86_OR_X64
#endif

#ifdef MOODYCAMEL_X86_OR_X64
struct CPUIDInfo
{
	std::uint32_t data[4];
};

#ifdef _MSC_VER
#include <intrin.h>

inline CPUIDInfo cpuid(std::uint32_t eax)
{
	CPUIDInfo info;
	__cpuidex((int*)&info.data[0], eax, 0);
	return info;
}
#else
// Assume GCC-compatible inline assembly syntax
inline CPUIDInfo cpuid(std::uint32_t eax)
{
	CPUIDInfo info;
	asm volatile("cpuid"
		: "=a" (info.data[0]), "=b" (info.data[1]), "=c" (info.data[2]), "=d" (info.data[3])
		: "a" (eax), "c" (0));
	return info;
}
#endif
#endif		// MOODYCAMEL_X86_OR_X64

namespace moodycamel
{
	const char* getCPUString()
	{
		// TODO: Support non-x86/-x64 architectures
#ifdef MOODYCAMEL_X86_OR_X64
		static char buf[128] = { 0 };
		if (buf[0] != 0) {
			return buf;
		}
		
		CPUIDInfo info = cpuid(0x80000000);
		std::uint32_t ex = info.data[0];
		for (std::uint32_t i = 0; i + 0x80000002 <= ex && i != 3; ++i) {
			*(reinterpret_cast<CPUIDInfo*>(buf) + i) = cpuid(i + 0x80000002);
		}
		
		if (buf[0] == 0) {
			strcpy(buf, UNKNOWN_CPU_STRING);
			return buf;
		}
		
		info = cpuid(0);
		if (info.data[0] < 1) {
			// cpuid(1) not supported
			return buf;
		}
		
		// Add number of CPUs, cores, HT, and GHz
		info = cpuid(1);
		bool ht = ((info.data[3] >> 28) & 1) == 1;	// Note: This is also 1 on most multi-core systems, even if there's no HT
		int cpus, cores, logicalCores;
		double clockSpeed;
		if (!getProcessorInfoFromOS(cpus, cores, logicalCores, clockSpeed)) {
			return buf;
		}
		// Strip @ nGHz if any, since we re-calculate this ourselves
		int atIndex;
		for (atIndex = (int)std::strlen(buf) - 1; atIndex != -1; --atIndex) {
			if (buf[atIndex] == '@') {
				if (atIndex > 0 && buf[atIndex - 1] == ' ') {
					--atIndex;
				}
				buf[atIndex] = '\0';
				break;
			}
		}
		// Strip trailing spaces if any
		for (char* s = buf + std::strlen(buf); s != buf && s[-1] == ' '; --s)
			s[-1] = '\0';
		char* str = buf + std::strlen(buf);
		if (cpus > 1) {
			// Assume identical CPUs
			logicalCores /= cpus;
			cores /= cpus;
			std::sprintf(str, " x%d", cpus);
			str += strlen(str);
		}
		ht = ht && logicalCores != cores;
		std::sprintf(str, " with %d core%s%s @ %.1fGHz%s", cores, cores == 1 ? "" : "s", ht ? " (HyperThreaded)" : "", clockSpeed, cpus > 1 ? " each" : "");
		
		return buf;
#else
		return UNKNOWN_CPU_STRING;
#endif
	}
}
