#ifndef _MMAP_H
#define _MMAP_H 1

#ifndef _WIN32

#include <sys/mman.h>

#ifndef O_BINARY
#define O_BINARY 0
#endif

#else /* _WIN32 */

#define WIN32_LEAN_AND_MEAN /* ::rolleyes:: */
#include <windows.h>

#define PROT_READ 1
#define PROT_WRITE 2

#define MAP_SHARED 1

static void* mmap(void* addr, size_t size, int prot, int flags, int fd, off_t offset)
{
   HANDLE hFile;
   HANDLE hMap;
   DWORD protect;
   DWORD access;
   void* data;
   hFile = (HANDLE)_get_osfhandle(fd);
   if (hFile == INVALID_HANDLE_VALUE) {
      fprintf(stderr, "FileSystem_MapFile: get_osfhandle failed\n");
      return (void*)-1;
   }
   protect = PAGE_READONLY;
   if (prot & PROT_WRITE) {
      protect = PAGE_READWRITE;
   }
   hMap = CreateFileMapping(hFile, NULL, protect, 0, size, NULL);
   if (hMap == NULL) {
      fprintf(stderr, "FileSystem_MapFile: CreateFileMapping failed\n");
      return (void*)-1;
   }
   access = FILE_MAP_READ;
   if (prot & PROT_WRITE) {
      access = FILE_MAP_WRITE;
   }
   data = MapViewOfFile(hMap, access, 0, 0, size);
   if (data == NULL) {
      fprintf(stderr, "FileSystem_MapFile: MapViewOfFile failed\n");
      CloseHandle(hMap);
      return (void*)-1;
   }
   CloseHandle(hMap);
   return data;
}

static int munmap(void *addr, size_t len)
{
   FlushViewOfFile(addr, len);
   UnmapViewOfFile(addr);
   return 0;
}

#endif /* _WIN32 */

#endif /* _MMAP_H */
