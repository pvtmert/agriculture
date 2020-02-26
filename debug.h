
#ifndef _DEBUG_H_
#define _DEBUG_H_

#define MPRINTF Serial.printf

static void
debug(const char *mod, const char *fmt, ...) {
	char *buffer = (char*) malloc(strlen(fmt) + 32);
	sprintf(buffer, "[ %8ld ] %8s | %s\r\n", time(NULL), mod, fmt, NULL);
	va_list args;
	va_start(args, fmt);
	char large[256];
	vsprintf(large, buffer, args);
	MPRINTF(large);
	//vprintf(buffer, args);
	va_end(args);
	return;
}

#endif
