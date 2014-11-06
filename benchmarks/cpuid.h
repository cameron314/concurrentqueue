#pragma once

namespace moodycamel
{
	static const char UNKNOWN_CPU_STRING[] = "unknown processor";
	
	// Returns a string representing the system's CPU info.
	// Assumes an x86/x64 architecture (returns UNKNOWN_CPU_STRING otherwise).
	// Returned string is valid in perpetuity.
	// Not thread safe.
	const char* getCPUString();
}
