/* RoboJet f793e93a: ftok("/",14), type 27. ARM receiver 0x27b06e
 * reads ID at message+8 (mtype, length, ID); 0x29e630 removes that map.
 * Active ID follows the native idle/reset branch; zero is never permitted.
 * The app has ClearMap but no del_local_map JSON handler. */
static bool native_is_robojet(const char *pid){int fd=native_map_fd(pid);if(fd<0)return false;bool yes=map_profile_fd(fd)==&robojet_map_profile;close(fd);return yes;}
static bool robojet_delete_message(uint32_t target,uint32_t active,bool clear,unsigned char text[4096]){
 if(!target||target>INT32_MAX||active>INT32_MAX||(clear?target!=active:target==active))return false;
 memset(text,0,4096);text[0]=8;for(int i=0;i<4;i++)text[4+i]=(unsigned char)(target>>(8*i));return true;
}
static bool robojet_map_queue_owner(int q,uint32_t pid){
 struct msqid_ds info;char path[96];struct stat st;
 if(msgctl(q,IPC_STAT,&info)||info.msg_lrpid<=0)return false;
 snprintf(path,sizeof path,"/proc/%u/task/%d",pid,info.msg_lrpid);
 return !stat(path,&st)&&S_ISDIR(st.st_mode);
}
static int robojet_map_dispatch(const char *pid,uint32_t target,uint32_t active,bool clear){
 struct {long type;unsigned char text[4096];} message={27,{0}};
 if(!robojet_delete_message(target,active,clear,message.text))return lib_error("Unsafe RoboJet map identity; nothing sent");
 uint32_t now,navpid,again;
 if(!native_is_robojet(pid)||!robojet_navigation_identity_process(&now,&navpid)||now!=active)return lib_error("RoboJet native identity changed; nothing sent");
 key_t key=ftok("/",14);int q=key==(key_t)-1?-1:msgget(key,0);
 if(q<0||!robojet_map_queue_owner(q,navpid))return lib_error("RoboJet navigation receiver unavailable; nothing sent");
 if(!native_identity(pid,&now)||now!=active||!native_idle(pid)||!robojet_navigation_identity_process(&now,&again)||again!=navpid||now!=active||!robojet_map_queue_owner(q,navpid))return lib_error("Robot or map changed; nothing sent");
 if(msgsnd(q,&message,sizeof message.text,IPC_NOWAIT))return lib_error("Deletion not dispatched");
 cJSON *o=cJSON_CreateObject();cJSON_AddBoolToObject(o,"accepted",true);cJSON_AddStringToObject(o,"result","delivered_to_robojet_navigation_queue");return print_json(o);
}
