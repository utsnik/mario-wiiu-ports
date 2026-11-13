#include <dolphin/card.h>
#include <dolphin/dvd.h>
#include <dolphin/os.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

extern "C" {
DVDDiskID* DVDGetCurrentDiskID() { return 0; }
OSTime OSGetTime() { return 0; }
void port_log(const char* fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
}
}

#include "../card.cpp"

static int failed;
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); failed++; } } while (0)

static char* temp_dir()
{
	char* p = strdup("/tmp/sms-card-test-XXXXXX");
	return mkdtemp(p);
}

static void write_bytes(const char* path, const void* data, size_t size)
{
	FILE* f = fopen(path, "wb");
	CHECK(f != 0);
	if (f) {
		CHECK(fwrite(data, 1, size, f) == size);
		fclose(f);
	}
}

static int replace_existing()
{
	char* dir = temp_dir();
	CHECK(dir != 0);
	setenv("SMS_SAVE_DIR", dir, 1);
	CARDFileInfo fi;
	CHECK(CARDMount(0, 0, 0) == CARD_RESULT_READY);
	char name[] = "save";
	CHECK(CARDCreate(0, name, 4, &fi) == CARD_RESULT_READY);
	char first[] = { 1, 2, 3, 4 };
	char second[] = { 9, 8, 7, 6 };
	CHECK(CARDWrite(&fi, first, sizeof first, 0) == CARD_RESULT_READY);
	CHECK(CARDWrite(&fi, second, sizeof second, 0) == CARD_RESULT_READY);
	char path[256];
	snprintf(path, sizeof path, "%s/save.dat", dir);
	FILE* f = fopen(path, "rb");
	CHECK(f != 0);
	char got[sizeof second] = {};
	if (f) {
		CHECK(fread(got, 1, sizeof got, f) == sizeof got);
		fclose(f);
	}
	CHECK(!memcmp(got, second, sizeof got));
	free(dir);
	return failed;
}

static int failure_propagation()
{
	char* dir = temp_dir();
	CHECK(dir != 0);
	char path[256];
	snprintf(path, sizeof path, "%s/not-a-directory", dir);
	write_bytes(path, "x", 1);
	setenv("SMS_SAVE_DIR", path, 1);
	CHECK(CARDMount(0, 0, 0) == CARD_RESULT_IOERROR);
	free(dir);
	return failed;
}

static int recover_tmp()
{
	char* dir = temp_dir();
	CHECK(dir != 0);
	setenv("SMS_SAVE_DIR", dir, 1);
	char index_path[256], data_path[256], stat_path[256];
	snprintf(index_path, sizeof index_path, "%s/index.txt", dir);
	snprintf(data_path, sizeof data_path, "%s/save.dat.tmp", dir);
	snprintf(stat_path, sizeof stat_path, "%s/save.stat.tmp", dir);
	write_bytes(index_path, "save\n", 5);
	CARDStat stat = {};
	strncpy(stat.fileName, "save", CARD_FILENAME_MAX);
	stat.length = 4;
	memcpy(stat.gameName, "GMSE", 4);
	memcpy(stat.company, "01", 2);
	char data[] = { 5, 4, 3, 2 };
	write_bytes(data_path, data, sizeof data);
	write_bytes(stat_path, &stat, sizeof stat);
	CARDFileInfo fi;
	CHECK(CARDMount(0, 0, 0) == CARD_RESULT_READY);
	char name[] = "save";
	CHECK(CARDOpen(0, name, &fi) == CARD_RESULT_READY);
	char got[sizeof data] = {};
	CHECK(CARDRead(&fi, got, sizeof got, 0) == CARD_RESULT_READY);
	CHECK(!memcmp(got, data, sizeof got));
	CHECK(access(data_path, F_OK) != 0);
	CHECK(access(stat_path, F_OK) != 0);
	free(dir);
	return failed;
}

int main(int argc, char** argv)
{
	if (argc != 2 || (strcmp(argv[1], "replace") && strcmp(argv[1], "failure") && strcmp(argv[1], "recover"))) {
		fprintf(stderr, "usage: %s replace|failure|recover\n", argv[0]);
		return 2;
	}
	if (!strcmp(argv[1], "replace"))
		return replace_existing();
	if (!strcmp(argv[1], "failure"))
		return failure_propagation();
	return recover_tmp();
}
