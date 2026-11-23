/* Read-only relo v2 decoder. The vendor Grid2D protobuf stores N uint16
   correspondence costs followed by N uint8 auxiliary cells in field 2.
   This is NOT upstream Cartographer's single-matrix serialization.
   No protobuf runtime, recursion, unbounded allocations or native IPC. */
struct map_pb {const unsigned char *p,*end;};
struct map_field {unsigned number,wire;uint64_t value;struct map_pb bytes;};
static bool map_varint(struct map_pb *b,uint64_t *v){
 *v=0;for(unsigned i=0;i<10;i++){if(b->p==b->end)return false;unsigned c=*b->p++;if(i==9&&c>1)return false;*v|=(uint64_t)(c&127)<<(7*i);if(!(c&128))return true;}return false;
}
static bool map_field(struct map_pb *b,struct map_field *f){
 uint64_t tag,n;if(!map_varint(b,&tag)||tag<8||tag>UINT32_MAX)return false;
 memset(f,0,sizeof(*f));f->number=(unsigned)(tag>>3);f->wire=tag&7;
 if(f->wire==0)return map_varint(b,&f->value);
 if(f->wire==1)n=8;else if(f->wire==5)n=4;else if(f->wire==2){if(!map_varint(b,&n))return false;}else return false;
 if(n>(uint64_t)(b->end-b->p))return false;
 f->bytes=(struct map_pb){b->p,b->p+(size_t)n};b->p+=(size_t)n;return true;
}
static bool v2_pair(struct map_pb b,struct map *m,bool dimensions){
 unsigned seen=0;struct map_field f;
 while(b.p<b.end){if(!map_field(&b,&f))return false;if(f.number>2)continue;
  unsigned mask=1u<<f.number;if(seen&mask)return false;seen|=mask;
  if(dimensions){if(f.wire!=0||!f.value||f.value>2048)return false;if(f.number==1)m->w=f.value;else m->h=f.value;}
  else{if(f.wire!=1)return false;double v=f64(f.bytes.p);if(!isfinite(v)||fabs(v)>1000000)return false;if(f.number==1)m->max_x=v;else m->max_y=v;}
 }
 /* proto3 zero coordinates may be omitted. Dimensions may not be zero. */
 return !dimensions||seen==6;
}
static bool v2_limits(struct map_pb b,struct map *m){
 unsigned seen=0;struct map_field f;
 while(b.p<b.end){if(!map_field(&b,&f))return false;if(f.number>3)continue;
  unsigned mask=1u<<f.number;if(seen&mask)return false;seen|=mask;
  if(f.number==1){if(f.wire!=1)return false;m->resolution=f64(f.bytes.p);}
  else if(f.wire!=2||!v2_pair(f.bytes,m,f.number==3))return false;
 }
 return seen==14&&isfinite(m->resolution)&&m->resolution>0&&m->resolution<=1&&(uint64_t)m->w*m->h<=1000000;
}
static bool v2_cells(const struct map *m,unsigned char *raw){
 struct map_pb b={m->data+m->grid,m->data+m->grid+m->packed};struct map_field f;size_t n=0,cells=(size_t)m->w*m->h;
 while(b.p<b.end){if(!map_field(&b,&f))return false;if(f.number!=2)continue;
  if(f.wire!=0&&f.wire!=2)return false;
  struct map_pb values=f.bytes;bool single=f.wire==0;
  while(single||values.p<values.end){uint64_t v=f.value;if(!single&&!map_varint(&values,&v))return false;
   if(n>=2*cells||v>(n<cells?32767u:255u))return false;
   if(raw&&n<cells){raw[n*2]=(unsigned char)v;raw[n*2+1]=(unsigned char)(v>>8);}n++;if(single)break;
  }
 }
 return n==2*cells;
}
static bool v2_block(const struct map *m,size_t *off,struct map_pb *b){
 if(*off>m->size||m->size-*off<4)return false;
 size_t n=u32(m->data+*off);*off+=4;
 if(n>m->size-*off)return false;
 *b=(struct map_pb){m->data+*off,m->data+*off+n};*off+=n;return true;
}
static bool map_v2(struct map *m){
 if(m->size<160)return false;
 uint32_t sum=0;for(size_t i=0;i<m->size;i++)if(i<152||i>=156)sum+=m->data[i];if(sum!=u32(m->data+152))return false;
 size_t off=156;struct map_pb zones,extra,grid,tail;
 if(!v2_block(m,&off,&zones)||(size_t)(zones.end-zones.p)<12)return false;
 uint64_t zone_size=12+(uint64_t)u32(zones.p+4)*16+(uint64_t)u32(zones.p+8)*32;
 if(zone_size!=(uint64_t)(zones.end-zones.p))return false;
 /* Verified v2 framing: zone config, 16-byte metadata, Grid2D, relocation. */
 if(!v2_block(m,&off,&extra)||extra.end-extra.p!=16||!v2_block(m,&off,&grid)||!v2_block(m,&off,&tail)||off!=m->size)return false;
 m->grid=(size_t)(grid.p-m->data);m->packed=(uint32_t)(grid.end-grid.p);m->protobuf=true;
 unsigned seen=0;struct map_field f;
 while(grid.p<grid.end){if(!map_field(&grid,&f))return false;
  if(f.number==1){if((seen&1)||f.wire!=2||!v2_limits(f.bytes,m))return false;seen|=1;}
  else if(f.number==4){if((seen&2)||f.wire!=2||f.bytes.p!=f.bytes.end)return false;seen|=2;}
  else if(f.number==5)return false; /* TSDF is not an occupancy map. */
 }
 return seen==3&&v2_cells(m,NULL);
}
