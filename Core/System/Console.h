#ifndef HEADER_CORE_SYSTEM_CONSOLE
#define HEADER_CORE_SYSTEM_CONSOLE

#include "Core/System/String.h"

namespace Core {
	class Console final{
	public:
		// FUCK YOU WINDOWS
		static void Initialize();

		static void Print(const String& text);
		static void PrintLine(const String& text);
		static void PrintLine();
	private:
		// Actual printing happens here.
		static void PrintRaw(const char* text);
	};
}

#endif