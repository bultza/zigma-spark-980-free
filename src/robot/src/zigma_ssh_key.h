/* Shared OpenSSH wire-format validation. */
static bool ssh_field(const unsigned char **p,size_t *remaining,const unsigned char **field,size_t *n){if(*remaining<4)return false;const unsigned char *b=*p;*n=(size_t)b[0]<<24|(size_t)b[1]<<16|(size_t)b[2]<<8|b[3];if(*n>*remaining-4)return false;*field=b+4;*p=b+4+*n;*remaining-=4+*n;return true;}
static bool public_key_valid(const char *type,const unsigned char *b,size_t len){
 const unsigned char *f,*p=b;size_t n,left=len;if(!ssh_field(&p,&left,&f,&n)||n!=strlen(type)||memcmp(type,f,n))return false;
 if(!strcmp(type,"ssh-ed25519"))return ssh_field(&p,&left,&f,&n)&&n==32&&!left;
 if(!strcmp(type,"ssh-rsa")){if(!ssh_field(&p,&left,&f,&n)||n<1||n>8||!(f[n-1]&1))return false;if(!ssh_field(&p,&left,&f,&n)||n<256||n>1025||left)return false;return n>256||(f[0]&128);}
 if(!strcmp(type,"ecdsa-sha2-nistp256")){if(!ssh_field(&p,&left,&f,&n)||n!=8||memcmp(f,"nistp256",8))return false;return ssh_field(&p,&left,&f,&n)&&n==65&&f[0]==4&&!left;}
 return false;
}
