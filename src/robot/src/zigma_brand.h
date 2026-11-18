#ifndef ZIGMA_BRAND_H
#define ZIGMA_BRAND_H
#ifndef ZIGMA_BRAND_FILE
#define ZIGMA_BRAND_FILE "/root/app_server/wifi_app_server_grit"
#endif
/* Cosmetic firmware identity only; never a capability or security decision. */
static const char *firmware_brand(void){
 static struct stat previous;
 static bool cached=false;
 static const char *brand="zigma";
 /* Overlay firmware exposes the native executable through a symlink.
  * Resolve it here; keep release inventory hashing strict (O_NOFOLLOW). */
 char *file=realpath(ZIGMA_BRAND_FILE,NULL);
 if(!file)return "zigma";
 struct stat current;
 if(lstat(file,&current)||!S_ISREG(current.st_mode)){free(file);return "zigma";}
 if(cached&&current.st_dev==previous.st_dev&&current.st_ino==previous.st_ino&&
    current.st_size==previous.st_size&&current.st_mtim.tv_sec==previous.st_mtim.tv_sec&&
    current.st_mtim.tv_nsec==previous.st_mtim.tv_nsec&&current.st_ctim.tv_sec==previous.st_ctim.tv_sec&&
    current.st_ctim.tv_nsec==previous.st_ctim.tv_nsec){free(file);return brand;}
 char hash[65];
 bool hashed=release_digest(file,hash);free(file);
 if(!hashed)return "zigma";
 brand=!strcmp(hash,"784fbcda505db72675bf01a82f5b02f6eca8cb313a7916176ed7bbf10d7dd912")?"puron":"zigma";
 previous=current;cached=true;
 return brand;
}
#endif
