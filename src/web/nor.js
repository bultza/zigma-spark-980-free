/* Two-pass binary protocol. Works on plain LAN HTTP without WebCrypto. */
async function readNorBackup(reader, progress=()=>{}) {
  let pending=new Uint8Array(0), position=0;
  async function take(n) {
    const out=new Uint8Array(n);let used=0;
    while(used<n){
      if(position===pending.length){const next=await reader.read();if(next.done)throw new Error('Incomplete backup. Nothing was saved.');pending=next.value;position=0;}
      const count=Math.min(n-used,pending.length-position);out.set(pending.subarray(position,position+count),used);used+=count;position+=count;
    }
    return out;
  }
  async function line(){const bytes=[];while(bytes.length<8192){const b=(await take(1))[0];if(b===10)return JSON.parse(new TextDecoder().decode(new Uint8Array(bytes)));bytes.push(b);}throw new Error('Invalid backup header.');}
  const header=await line(), size=32*1024*1024, block=65536;
  if(header.format!=='zigma-nor-v1'||header.size!==size||header.block_size!==block||header.passes!==2)throw new Error('Unsupported backup format.');
  const data=new Uint8Array(size);
  for(let offset=0;offset<size;offset+=block){data.set(await take(block),offset);progress('Reading',offset+block,size);}
  let different=false;
  for(let offset=0;offset<size;offset+=block){const check=await take(block);for(let i=0;i<check.length;i++)if(check[i]!==data[offset+i])different=true;progress('Verifying',offset+block,size);}
  const result=await line();
  if(position!==pending.length||!(await reader.read()).done)throw new Error('Unexpected backup suffix.');
  if(different||result.verified!==true||!(/^[a-f0-9]{64}$/).test(result.first_sha256)||result.first_sha256!==result.second_sha256)throw new Error('The two reads differ. Nothing was saved. Keep the robot idle and retry both readings.');
  return {data, manifest:{...header,...result,captured_at:new Date().toISOString(),verification:'Two complete reads compared byte for byte in the browser; SHA256 supplied by the reader.',note:'Live reads are not an atomic filesystem snapshot. Contains private credentials.'}};
}
if(typeof module!=='undefined')module.exports={readNorBackup};
