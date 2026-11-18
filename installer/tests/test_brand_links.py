"""Exercise brand detection through real filesystem symlinks and cached targets."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2] / 'src/robot/src'

class BrandLinks(unittest.TestCase):
    def test_native_paths_and_cache(self):
        source = r'''
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
static int hashes;
static char input[1024];
#define ZIGMA_BRAND_FILE input
/* Isolate path/cache behavior; the production digest routine stays unchanged. */
static bool release_digest(const char *path,char out[65]){
 struct stat st;assert(!lstat(path,&st)&&S_ISREG(st.st_mode));hashes++;
 int fd=open(path,O_RDONLY|O_NOFOLLOW);if(fd<0)return false;
 char marker;ssize_t n=read(fd,&marker,1);close(fd);if(n!=1)return false;
 strcpy(out,marker=='P'?"784fbcda505db72675bf01a82f5b02f6eca8cb313a7916176ed7bbf10d7dd912":"unknown");return true;
}
#include "zigma_brand.h"
static void file(const char *name,const char *value){FILE *f=fopen(name,"w");assert(f);assert(fputs(value,f)>=0);assert(!fclose(f));}
int main(int argc,char **argv){
 assert(argc==2&&!chdir(argv[1]));strcpy(input,"native");
 file("native","P");assert(!strcmp(firmware_brand(),"puron"));assert(hashes==1);
 assert(!strcmp(firmware_brand(),"puron"));assert(hashes==1);
 assert(!rename("native","target"));assert(!symlink("target","native"));
 assert(!strcmp(firmware_brand(),"puron"));int cached_hashes=hashes;
 assert(!strcmp(firmware_brand(),"puron"));assert(hashes==cached_hashes);
 file("replacement","Z");assert(!rename("replacement","target"));
 assert(!strcmp(firmware_brand(),"zigma"));assert(hashes==cached_hashes+1);
 file("puron","P");assert(!unlink("native"));assert(!symlink("puron","native"));
 assert(!strcmp(firmware_brand(),"puron"));
 assert(!unlink("puron"));assert(!strcmp(firmware_brand(),"zigma"));
 assert(!unlink("native"));assert(!symlink("native","native"));assert(!strcmp(firmware_brand(),"zigma"));
 assert(!unlink("native"));assert(!mkdir("native",0700));assert(!strcmp(firmware_brand(),"zigma"));
 puts("PASS: regular file, relative link, target replacement, retargeting, broken/cyclic links and nonregular target");
}
'''
        with tempfile.TemporaryDirectory() as directory:
            base=Path(directory)
            (base/'test.c').write_text(source)
            subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-I'+str(ROOT),str(base/'test.c'),'-o',str(base/'test')],check=True)
            subprocess.run([str(base/'test'),str(base)],check=True)

if __name__ == '__main__':
    unittest.main()
